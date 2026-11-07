#include "Service.h"
#include "CEApi.h"
#include "OarValues.h"
#include "Settings.h"

#include <format>
#include <fstream>
#include <sstream>
#include <thread>

namespace ConditionEngine::Dials {

    namespace {
        constexpr const char* kOwner = "Dials";
        constexpr const char* kChangedEvent = "Dials_Changed";

        thread_local std::string tl_sendingDial;  // see Service::SendingDial

        std::string Hex(ActorKey key) { return std::format("0x{:08X}", key); }

        nlohmann::json ParseFile(const std::filesystem::path& path, std::string& error) {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                error = "cannot open";
                return nullptr;
            }
            std::stringstream ss;
            ss << in.rdbuf();
            try {
                return nlohmann::json::parse(ss.str(), nullptr, true, /*ignore_comments*/ true);
            } catch (const std::exception& e) {
                error = e.what();
                return nullptr;
            }
        }

        // A preset file's "dials" section (files without one are ConditionEngine presets only)
        const nlohmann::json* DialsOf(const nlohmann::json& file) {
            if (!file.is_object()) return nullptr;
            if (auto it = file.find("dials"); it != file.end() && it->is_object()) return &*it;
            return nullptr;
        }

        const nlohmann::json* SectionOf(const nlohmann::json& file, const char* name) {
            if (!file.is_object()) return nullptr;
            if (auto it = file.find(name); it != file.end() && it->is_object()) return &*it;
            return nullptr;
        }

        constexpr const char* kUserOwner = "User";

        std::atomic<bool> g_tickQueued{false};
    }

    nlohmann::json Outcome::ToJson() const { return {{"ok", ok}, {"errors", errors}, {"warnings", warnings}}; }

    Service& Service::Get() {
        static Service instance;
        return instance;
    }

    Service::Service() : engine_(host_) {}

    // ── Start-up ─────────────────────────────────────────────────────────────

    bool Service::Connect() {
        if (ready_) return true;
        if (!CE::Load(why_)) {
            SKSE::log::error("Dials: {}. No dials will run.", why_);
            return false;
        }
        const auto& ce = CE::Get();
        ce.RegisterSource(kOwner, "Dials");
        char error[256] = {};
        ce.RegisterCategory(R"({"id": "dials", "label": "Dials", "icon": "dial", "order": 80,
                               "description": "Values made with Dials: by mods, scripts and the player"})",
                            kOwner, error, sizeof(error));
        RegisterEventValueType();
        ready_ = true;
        SKSE::log::info("Dials: connected to ConditionEngine");
        return true;
    }

    void Service::Start() {
        if (started_) return;
        if (!ready_ && !Connect()) return;
        started_ = true;
        const auto& settings = GetSettings();
        engine_.SetTrackInterval(settings.trackInterval);
        engine_.SetMaxCatchUpHours(settings.maxCatchUpHours);
        ReloadFiles();

        const auto interval = std::chrono::milliseconds(settings.tickMs);
        std::thread([this, interval]() {
            for (;;) {
                std::this_thread::sleep_for(interval);
                if (!session_.load()) continue;
                if (g_tickQueued.exchange(true)) continue;  // one queued tick at a time
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([this]() {
                        Tick();
                        g_tickQueued.store(false);
                    });
                } else {
                    g_tickQueued.store(false);
                }
            }
        }).detach();
    }

    // ── Files ────────────────────────────────────────────────────────────────

    std::map<std::string, nlohmann::json> Service::ReadDialFiles(std::vector<std::string>& errors,
                                                                 std::map<std::string, std::string>& sources) {
        std::map<std::string, nlohmann::json> out;
        std::error_code ec;
        const auto dir = DataDirectory();
        if (!std::filesystem::is_directory(dir, ec)) return out;
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.is_regular_file() && Lower(entry.path().extension().string()) == ".json") files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        for (const auto& path : files) {
            std::string error;
            const auto file = ParseFile(path, error);
            const auto name = path.filename().string();
            if (file.is_null()) {
                errors.push_back(name + ": " + error);
                continue;
            }
            const auto* dials = DialsOf(file);
            if (!dials) continue;
            for (const auto& [id, j] : dials->items()) {
                if (id.starts_with('_') || id.starts_with('$')) continue;
                const auto key = Lower(id);
                if (auto it = sources.find(key); it != sources.end()) {
                    errors.push_back(name + ": dial '" + id + "' replaces the one in " + it->second + ".json");
                }
                out[key] = j;
                out[key]["$id"] = id;
                sources[key] = path.stem().string();
            }
        }
        return out;
    }

    void Service::LoadUserFile() {
        for (auto& [_, layer] : layers_) layer.user.reset();
        CE::Get().UnregisterOwner(kUserOwner);
        userConditions_.clear();
        userValues_.clear();
        editorPrefs_ = nlohmann::json::object();
        std::error_code ec;
        if (!std::filesystem::exists(UserFile(), ec)) return;
        std::string error;
        const auto file = ParseFile(UserFile(), error);
        if (file.is_null()) {
            fileErrors_.push_back("User/User.json: " + error);
            return;
        }
        if (const auto* editor = SectionOf(file, "editor")) editorPrefs_ = *editor;
        if (const auto* dials = DialsOf(file)) {
            for (const auto& [id, j] : dials->items()) {
                if (id.starts_with('_') || id.starts_with('$')) continue;
                auto& layer = layers_[Lower(id)];
                if (layer.id.empty()) layer.id = id;
                layer.user = j;
            }
        }
        // The player's own named conditions and values
        for (const bool isValue : {false, true}) {
            auto& store = isValue ? userValues_ : userConditions_;
            const auto* section = SectionOf(file, isValue ? "values" : "conditions");
            if (!section) continue;
            for (const auto& [name, j] : section->items()) {
                if (name.starts_with('_')) continue;
                std::vector<std::string> errors;
                RegisterUserNamed(isValue, name, j, errors);
                for (const auto& e : errors) fileErrors_.push_back("User/User.json: " + e);
                store[name] = j;
            }
        }
    }

    bool Service::WriteUserFile(std::string& error) {
        nlohmann::json dials = nlohmann::json::object();
        for (const auto& [_, layer] : layers_) {
            if (layer.user) dials[layer.id] = *layer.user;
        }
        nlohmann::json conditions = nlohmann::json::object();
        for (const auto& [name, j] : userConditions_) conditions[name] = j;
        nlohmann::json values = nlohmann::json::object();
        for (const auto& [name, j] : userValues_) values[name] = j;
        nlohmann::json file = {{"_comment", "Dials, conditions and values made or edited in the ConditionEngine editor. "
                                            "Safe to edit by hand; the editor rewrites it on save."},
                               {"dials", dials},
                               {"conditions", conditions},
                               {"values", values}};
        if (!editorPrefs_.empty()) file["editor"] = editorPrefs_;
        std::error_code ec;
        std::filesystem::create_directories(UserFile().parent_path(), ec);
        const auto temp = UserFile().string() + ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) {
                error = "cannot write " + UserFile().string();
                return false;
            }
            out << file.dump(2);
        }
        std::filesystem::rename(temp, UserFile(), ec);
        if (ec) {
            error = "cannot replace " + UserFile().string() + ": " + ec.message();
            return false;
        }
        return true;
    }

    void Service::ReloadFiles() {
        std::lock_guard lock(mutex_);
        fileErrors_.clear();
        std::map<std::string, std::string> sources;
        auto files = ReadDialFiles(fileErrors_, sources);
        for (auto& [key, layer] : layers_) {
            if (layer.baseFromFile && !files.contains(key)) {
                layer.base.reset();
                layer.baseFromFile = false;
            }
        }
        for (auto& [key, j] : files) {
            auto& layer = layers_[key];
            if (layer.base && !layer.baseFromFile) {
                fileErrors_.push_back(sources[key] + ".json: dial '" + key + "' is defined by " + layer.baseSource +
                                      ", which wins");
                continue;
            }
            layer.id = j.value("$id", key);
            j.erase("$id");
            layer.base = std::move(j);
            layer.baseSource = sources[key];
            layer.baseFromFile = true;
        }
        LoadUserFile();
        std::vector<std::string> keys;
        for (const auto& [key, _] : layers_) keys.push_back(key);
        std::size_t live = 0;
        for (const auto& key : keys) {
            auto outcome = Apply(key);
            if (outcome.ok && engine_.Exists(key)) ++live;
            for (const auto& e : outcome.errors) SKSE::log::warn("Dials: {}", e);
        }
        std::erase_if(layers_, [](const auto& e) { return !e.second.base && !e.second.user; });
        for (const auto& e : fileErrors_) SKSE::log::warn("Dials: {}", e);
        SKSE::log::info("Dials: {} dials live ({} from files, {} edited in the editor)", live, files.size(),
                        std::count_if(layers_.begin(), layers_.end(), [](const auto& e) { return e.second.user.has_value(); }));
        RequestPublish();
    }

    // ── Definitions ──────────────────────────────────────────────────────────

    Outcome Service::Prepare(const std::string& id, const nlohmann::json& json, const std::string& source,
                             std::optional<DialDef>& out) {
        Outcome o;
        auto parsed = ParseDial(id, json, source);
        o.errors = std::move(parsed.errors);
        o.warnings = std::move(parsed.warnings);
        if (!parsed.def) return o;
        auto& def = *parsed.def;
        const auto& ce = CE::Get();

        // $id must be ours to take
        const auto list = CE::ReadString([&](char* b, std::uint32_t n) { return ce.ListNamedValues(b, n); });
        try {
            for (const auto& v : nlohmann::json::parse(list)) {
                if (!v.is_object() || !IEquals(v.value("name", ""), def.id)) continue;
                const auto owner = v.value("source", v.value("owner", ""));
                if (!IEquals(owner, kOwner)) {
                    o.errors.push_back("a value named $" + def.id + " already exists (from " + owner +
                                       "); name the dial differently, or feed it with \"provides\": [\"" + def.id + "\"]");
                }
            }
        } catch (...) {
        }

        // @Conditions: compile, and the name must be free (or already this dial's)
        for (const auto& [name, text] : def.conditions) {
            const auto expr = def.ConditionExpression(text);
            char error[512] = {};
            const auto h = ce.Compile(expr.c_str(), error, sizeof(error));
            if (!h) {
                o.errors.push_back("dial '" + def.id + "' condition @" + name + " '" + expr + "': " + error);
                continue;
            }
            ce.Release(h);
            bool ours = false;
            std::string otherDial;
            for (const auto& [dialKey, names] : conditionNames_) {
                for (const auto& n : names) {
                    if (!IEquals(n, name)) continue;
                    if (dialKey == Lower(def.id)) ours = true;
                    else otherDial = dialKey;
                }
            }
            std::uint32_t deps = 0;
            if (!otherDial.empty()) {
                o.errors.push_back("condition @" + name + " already belongs to dial '" + otherDial + "'");
            } else if (!ours && ce.LookupNamed(name.c_str(), &deps)) {
                o.errors.push_back("a condition named @" + name + " already exists; pick another name");
            }
        }
        if (o.errors.empty()) {
            o.ok = true;
            out = std::move(def);
        }
        return o;
    }

    void Service::RegisterNames(const DialDef& def, std::vector<std::string>& warnings) {
        const auto& ce = CE::Get();
        nlohmann::json value = {
            {"pushed", {{"default", def.Exposed(def.defaultValue)}}},
            {"label", def.label},
            {"range", {def.min, def.max}},
            {"integer", def.integer},
            {"scope", def.scope == Scope::Actor ? "actor" : "global"},
            {"category", def.category},
            {"description", def.description.empty() ? "Dial: " + def.label : def.description},
            {"tags", {"dial"}},
            {"owner", kOwner},
        };
        if (!def.unit.empty()) value["unit"] = def.unit;
        if (!def.icon.empty()) value["icon"] = def.icon;
        if (!def.provides.empty()) {
            value["provides"] = def.provides;
            value["priority"] = def.priority;
            // Actors without the dial fall through to the next source of the combined value
            if (def.scope == Scope::Actor) value["enabledWhen"] = def.track.empty() ? "IsPlayer" : def.track;
        }
        char error[512] = {};
        if (ce.RegisterNamedValue(def.id.c_str(), value.dump().c_str(), kOwner, error, sizeof(error)) != CE_OK) {
            warnings.push_back("$" + def.id + " not registered with ConditionEngine: " + std::string(error));
        }

        const auto key = Lower(def.id);
        std::vector<std::string> names;
        for (const auto& [name, text] : def.conditions) {
            nlohmann::json cond = {{"expr", def.ConditionExpression(text)},
                                   {"label", name},
                                   {"category", def.category},
                                   {"description", "Dial " + def.label + ": " + text},
                                   {"tags", {"dial"}},
                                   {"owner", kOwner}};
            std::fill(std::begin(error), std::end(error), '\0');
            if (ce.RegisterNamed(name.c_str(), cond.dump().c_str(), kOwner, error, sizeof(error)) == CE_OK) {
                names.push_back(name);
            } else {
                warnings.push_back("@" + name + " not registered: " + std::string(error));
            }
        }
        for (const auto& old : conditionNames_[key]) {
            if (std::none_of(names.begin(), names.end(), [&](const auto& n) { return IEquals(n, old); })) {
                ce.UnregisterNamed(old.c_str());
            }
        }
        conditionNames_[key] = std::move(names);
    }

    void Service::UnregisterNames(const std::string& id) {
        const auto& ce = CE::Get();
        ce.UnregisterNamedValue(id.c_str());
        const auto key = Lower(id);
        for (const auto& name : conditionNames_[key]) ce.UnregisterNamed(name.c_str());
        conditionNames_.erase(key);
    }

    Outcome Service::Apply(const std::string& id) {
        const auto key = Lower(id);
        auto& layer = layers_[key];
        if (layer.id.empty()) layer.id = id;
        Outcome o;
        const nlohmann::json* effective = layer.user ? &*layer.user : layer.base ? &*layer.base : nullptr;
        if (!effective) {
            if (engine_.Exists(key)) {
                engine_.Remove(key);
                UnregisterNames(layer.id);
            }
            OarValues::Get().Remove(key);
            layer.errors.clear();
            RefreshUsage();
            o.ok = true;
            return o;
        }
        std::optional<DialDef> def;
        o = Prepare(layer.id, *effective, layer.user ? "User" : layer.baseSource, def);
        if (def) {
            auto defined = engine_.Define(*def);
            o.errors.insert(o.errors.end(), defined.errors.begin(), defined.errors.end());
            o.ok = defined.ok;
            if (defined.ok) {
                RegisterNames(*def, o.warnings);
                OarValues::Get().Define(*def, engine_.Values(def->id));
            }
        }
        if (!o.ok && engine_.Exists(key)) {
            engine_.Remove(key);
            UnregisterNames(layer.id);
        }
        if (!o.ok) OarValues::Get().Remove(key);
        layer.errors = o.errors;
        layer.warnings = o.warnings;
        RefreshUsage();
        RequestPublish();
        DefinitionsChanged();
        return o;
    }

    Outcome Service::Define(const std::string& id, const nlohmann::json& json, const std::string& source) {
        if (!ready_) return {false, {why_.empty() ? "Dials is not running" : why_}, {}};
        if (!IsValidId(id)) return {false, {"'" + id + "' is not a valid dial name (letters, digits and _)"}, {}};
        std::lock_guard lock(mutex_);
        auto& layer = layers_[Lower(id)];
        const auto previous = layer;
        layer.id = id;
        layer.base = json;
        layer.baseSource = source.empty() ? "a mod" : source;
        layer.baseFromFile = false;
        if (layer.user) {
            // The player's edit stays in force: check the new base without applying it
            auto o = Validate(id, json);
            if (!o.ok) layer = previous;
            else o.warnings.push_back("the player edited '" + id + "'; their version stays in force");
            return o;
        }
        auto o = Apply(id);
        if (!o.ok) {
            layer = previous;
            Apply(id);  // whatever was live before comes back
        }
        return o;
    }

    Outcome Service::DefineText(const std::string& id, const std::string& jsonText, const std::string& source) {
        try {
            return Define(id, nlohmann::json::parse(jsonText, nullptr, true, true), source);
        } catch (const std::exception& e) {
            return {false, {std::string("not JSON: ") + e.what()}, {}};
        }
    }

    bool Service::Remove(const std::string& id) {
        std::lock_guard lock(mutex_);
        auto it = layers_.find(Lower(id));
        if (it == layers_.end() || !it->second.base) return false;
        it->second.base.reset();
        it->second.baseFromFile = false;
        Apply(id);
        if (!it->second.user) layers_.erase(it);
        return true;
    }

    std::size_t Service::RemoveSource(const std::string& source) {
        std::vector<std::string> ids;
        {
            std::lock_guard lock(mutex_);
            for (const auto& [key, layer] : layers_) {
                if (layer.base && !layer.baseFromFile && IEquals(layer.baseSource, source)) ids.push_back(key);
            }
        }
        for (const auto& id : ids) Remove(id);
        return ids.size();
    }

    Outcome Service::Validate(const std::string& id, const nlohmann::json& json) {
        if (!ready_) return {false, {why_}, {}};
        std::lock_guard lock(mutex_);
        std::optional<DialDef> def;
        auto o = Prepare(id, json, "User", def);
        if (def) {
            Engine scratch(host_);
            auto defined = scratch.Define(*def);
            o.errors.insert(o.errors.end(), defined.errors.begin(), defined.errors.end());
            o.ok = defined.ok;
        }
        return o;
    }

    Outcome Service::SaveUser(const std::string& id, const nlohmann::json& json, const std::string& previousId) {
        auto o = Validate(id, json);
        if (!o.ok) return o;
        std::lock_guard lock(mutex_);
        if (!previousId.empty() && !IEquals(previousId, id)) {
            // A renamed dial the player made: the old name goes
            if (auto it = layers_.find(Lower(previousId)); it != layers_.end() && !it->second.base) {
                it->second.user.reset();
                Apply(previousId);
                layers_.erase(Lower(previousId));
            }
        }
        auto& layer = layers_[Lower(id)];
        const auto previous = layer;
        layer.id = id;
        layer.user = json;
        std::string error;
        if (!WriteUserFile(error)) {
            layer = previous;
            return {false, {error}, {}};
        }
        auto applied = Apply(id);
        applied.warnings.insert(applied.warnings.begin(), o.warnings.begin(), o.warnings.end());
        return applied;
    }

    bool Service::RevertUser(const std::string& id) {
        std::lock_guard lock(mutex_);
        auto it = layers_.find(Lower(id));
        if (it == layers_.end() || !it->second.user) return false;
        it->second.user.reset();
        std::string error;
        if (!WriteUserFile(error)) SKSE::log::warn("Dials: {}", error);
        Apply(id);
        if (!it->second.base) layers_.erase(it);
        return true;
    }

    void Service::RefreshUsage() {
        std::uint32_t mask = 0;
        std::vector<std::uint32_t> keys;
        for (const auto& def : engine_.Definitions()) {
            if (!def.enabled) continue;
            for (const auto& r : def.rules) {
                if (!r.enabled) continue;
                if (r.trigger == TriggerKind::Game) mask |= 1u << static_cast<int>(r.game);
                if (r.trigger == TriggerKind::Key) keys.push_back(r.key);
            }
        }
        usedEvents_.store(mask);
        std::lock_guard lock(keysMutex_);
        usedKeys_ = std::move(keys);
    }

    bool Service::UsesKey(std::uint32_t scanCode) const {
        auto& self = const_cast<Service&>(*this);
        std::lock_guard lock(self.keysMutex_);
        return std::find(usedKeys_.begin(), usedKeys_.end(), scanCode) != usedKeys_.end();
    }

    // ── Runtime ──────────────────────────────────────────────────────────────

    void Service::Tick() {
        if (!ready_ || !session_.load()) return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Is3DLoaded()) return;

        const auto now = std::chrono::steady_clock::now();
        double realDt = lastTick_.time_since_epoch().count() == 0
                            ? 0.0
                            : std::chrono::duration<double>(now - lastTick_).count();
        lastTick_ = now;
        realDt = std::min(realDt, 1.0);  // a hitch or a loading screen is not a second of rain
        if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) realDt = 0.0;

        auto* calendar = RE::Calendar::GetSingleton();
        const double gameHours = calendar ? static_cast<double>(calendar->GetCurrentGameTime()) * 24.0 : 0.0;

        std::vector<ActorKey> loaded;
        loaded.push_back(kPlayer);
        if (auto* lists = RE::ProcessLists::GetSingleton()) {
            for (auto& handle : lists->highActorHandles) {
                auto actor = handle.get();
                if (actor && !actor->IsDeleted() && actor->GetFormID() != kPlayer) loaded.push_back(actor->GetFormID());
            }
        }
        if (const auto dropped = engine_.DroppedSends()) {
            SKSE::log::warn("Dials: dropped {} mod events that rules sent: at most {} in one tick", dropped, kMaxSendsPerTick);
        }
        FlushSends();
        engine_.Tick(realDt, gameHours, loaded);
        Publish();
        if (onTick) onTick();
    }

    const std::string& Service::SendingDial() { return tl_sendingDial; }

    void Service::QueueSends() {
        auto sends = engine_.TakeSends();
        if (sends.empty()) return;
        std::lock_guard lock(outboxMutex_);
        for (auto& s : sends) outbox_.push_back(std::move(s));
    }

    // Main thread, at the start of a tick: the mod events rules sent since the last one
    void Service::FlushSends() {
        std::vector<Send> sends;
        {
            std::lock_guard lock(outboxMutex_);
            sends.swap(outbox_);
        }
        if (sends.empty()) return;
        auto* source = SKSE::GetModCallbackEventSource();
        if (!source) return;
        const bool verbose = GetSettings().verbose;
        for (const auto& s : sends) {
            if (verbose) SKSE::log::info("Dials: {} sends {} ({}, {}) for {}", s.dial, s.name, s.text, s.number, ActorName(s.actor));
            SKSE::ModCallbackEvent event{RE::BSFixedString(s.name), RE::BSFixedString(s.text), static_cast<float>(s.number),
                                         s.actor ? ActorOf(s.actor) : nullptr};
            tl_sendingDial = s.dial;  // the mod event sink reads it while SendEvent runs
            source->SendEvent(&event);
            tl_sendingDial.clear();
        }
    }

    void Service::Publish() {
        QueueSends();
        auto changes = engine_.TakeChanges();
        if (changes.empty()) {
            if (auto notes = engine_.TakeNotes(); !notes.empty() && onNotes) onNotes(std::move(notes));
            return;
        }
        const auto& ce = CE::Get();
        const bool verbose = GetSettings().verbose;
        std::vector<std::pair<ChangedFn, void*>> subscribers;
        {
            std::lock_guard lock(subscribersMutex_);
            for (const auto& [_, s] : subscribers_) subscribers.push_back(s);
        }
        auto* source = SKSE::GetModCallbackEventSource();
        auto& oar = OarValues::Get();
        for (const auto& c : changes) {
            oar.Update(c.id, c.actor, c.value);
            RE::Actor* actor = c.actor ? ActorOf(c.actor) : nullptr;
            if (c.actor == 0) {
                ce.SetGlobalValue(c.id.c_str(), c.value, 0.0f);
            } else if (actor) {
                ce.SetValue(actor, c.id.c_str(), c.value, 0.0f);
            }
            if (c.notify) {
                if (verbose) {
                    SKSE::log::info("Dials: {} {} {} -> {} ({})", c.id, ActorName(c.actor), c.previous, c.value, c.cause);
                }
                if (source) {
                    SKSE::ModCallbackEvent event{kChangedEvent, RE::BSFixedString(c.id), static_cast<float>(c.value), actor};
                    source->SendEvent(&event);
                }
            }
            for (const auto& [fn, user] : subscribers) fn(c.id.c_str(), actor, c.value, c.previous, user);
        }
        if (auto notes = engine_.TakeNotes(); !notes.empty() && onNotes) onNotes(std::move(notes));
        if (onChanged) onChanged();
    }

    void Service::RequestPublish() {
        if (publishQueued_.exchange(true)) return;
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([this]() {
                publishQueued_.store(false);
                Publish();
            });
        } else {
            publishQueued_.store(false);
        }
    }

    void Service::OnSessionEnd() {
        session_.store(false);
        engine_.ClearValues();
        OarValues::Get().ClearValues();
        std::lock_guard lock(outboxMutex_);
        outbox_.clear();
    }

    void Service::OnSessionStart() {
        lastTick_ = {};
        engine_.OnSessionStart();
        session_.store(true);
        Publish();
    }

    // ── Information ──────────────────────────────────────────────────────────

    nlohmann::json Service::Snapshot() {
        std::lock_guard lock(mutex_);
        nlohmann::json dials = nlohmann::json::array();
        for (const auto& [key, layer] : layers_) {
            nlohmann::json d;
            d["id"] = layer.id;
            d["origin"] = layer.user ? (layer.base ? "override" : "user") : (layer.baseFromFile ? "file" : "mod");
            d["source"] = layer.base ? layer.baseSource : "User";
            d["json"] = layer.user ? *layer.user : layer.base ? *layer.base : nlohmann::json::object();
            if (layer.base) d["base"] = *layer.base;
            d["errors"] = layer.errors;
            d["warnings"] = layer.warnings;
            const auto def = engine_.Find(key);
            d["live"] = def.has_value();
            if (def) {
                d["label"] = def->label;
                d["category"] = def->category;
                d["description"] = def->description;
                d["kind"] = def->kind == Kind::Toggle ? "toggle" : def->kind == Kind::Choice ? "choice" : "number";
                d["scope"] = def->scope == Scope::Global ? "global" : def->track.empty() ? "player" : "actor";
                d["min"] = def->min;
                d["max"] = def->max;
                d["integer"] = def->integer;
                d["unit"] = def->unit;
                d["enabled"] = def->enabled;
                nlohmann::json options = nlohmann::json::object();
                for (const auto& [v, label] : def->options) options[std::to_string(v)] = label;
                d["options"] = options;
                d["value"] = engine_.Get(key, 0).value_or(def->defaultValue);
                nlohmann::json actors = nlohmann::json::array();
                if (def->scope == Scope::Actor && !def->track.empty()) {
                    std::size_t n = 0;
                    for (const auto& [actor, v] : engine_.Values(key)) {
                        if (++n > 50) break;
                        actors.push_back({{"key", Hex(actor)}, {"name", ActorName(actor)}, {"value", v}});
                    }
                    d["tracked"] = engine_.TrackedActors(key).size();
                }
                d["actors"] = actors;
                nlohmann::json conditions = nlohmann::json::array();
                for (const auto& [name, text] : def->conditions) {
                    conditions.push_back({{"name", name}, {"expr", def->ConditionExpression(text)}});
                }
                d["conditions"] = conditions;
            } else {
                d["label"] = d["json"].value("label", layer.id);
                d["category"] = d["json"].value("category", "dials");
            }
            dials.push_back(std::move(d));
        }
        nlohmann::json events = nlohmann::json::array();
        for (std::size_t i = 0; i < kGameEventCount; ++i) {
            const auto e = static_cast<GameEvent>(i);
            events.push_back({{"name", GameEventName(e)}, {"help", GameEventHelp(e)}});
        }
        nlohmann::json out;
        out["ready"] = ready_;
        out["why"] = why_;
        out["inGame"] = session_.load();
        out["dials"] = std::move(dials);
        out["events"] = std::move(events);
        out["fileErrors"] = fileErrors_;
        out["editorKey"] = ComboName(GetSettings().editorKey);  // the editor replaces it with the live binding
        out["log"] = LiveValues()["log"];
        nlohmann::json userConditions = nlohmann::json::object();
        for (const auto& [name, j] : userConditions_) userConditions[name] = j;
        nlohmann::json userValues = nlohmann::json::object();
        for (const auto& [name, j] : userValues_) userValues[name] = j;
        out["userConditions"] = std::move(userConditions);
        out["userValues"] = std::move(userValues);
        return out;
    }

    nlohmann::json Service::LiveValues() {
        nlohmann::json values = nlohmann::json::object();
        nlohmann::json actors = nlohmann::json::object();
        for (const auto& def : engine_.Definitions()) {
            values[def.id] = engine_.Get(def.id, 0).value_or(def.defaultValue);
            if (def.scope == Scope::Actor) {
                nlohmann::json list = nlohmann::json::array();
                std::size_t n = 0;
                for (const auto& [actor, v] : engine_.Values(def.id)) {
                    if (++n > 50) break;
                    list.push_back({{"key", Hex(actor)}, {"name", ActorName(actor)}, {"value", v}});
                }
                actors[def.id] = std::move(list);
            }
        }
        nlohmann::json log = nlohmann::json::array();
        for (const auto& e : engine_.RecentLog(40)) {
            log.push_back({{"t", e.time}, {"id", e.id}, {"actor", ActorName(e.actor)}, {"from", e.from}, {"to", e.to},
                           {"cause", e.cause}});
        }
        return {{"values", values}, {"actors", actors}, {"log", log}, {"inGame", session_.load()}};
    }

    nlohmann::json Service::Describe(const std::string& id) {
        auto snap = Snapshot();
        for (auto& d : snap["dials"]) {
            if (IEquals(d.value("id", ""), id)) return d;
        }
        return nullptr;
    }

    std::string Service::Explain(const std::string& expression, ActorKey actor, bool isValue) {
        if (!ready_) return why_;
        const auto& ce = CE::Get();
        char error[512] = {};
        auto* a = ActorOf(actor);
        if (isValue) {
            const auto h = ce.CompileValue(expression.c_str(), error, sizeof(error));
            if (!h) return std::string("does not compile: ") + error;
            auto text = CE::ReadString([&](char* b, std::uint32_t n) { return ce.ExplainValue(h, a, 1, b, n); });
            ce.ReleaseValue(h);
            return text;
        }
        const auto h = ce.Compile(expression.c_str(), error, sizeof(error));
        if (!h) return std::string("does not compile: ") + error;
        auto text = CE::ReadString([&](char* b, std::uint32_t n) { return ce.Explain(h, a, 1, b, n); });
        ce.Release(h);
        return text;
    }

    std::uint32_t Service::Subscribe(ChangedFn fn, void* user) {
        if (!fn) return 0;
        std::lock_guard lock(subscribersMutex_);
        const auto id = nextSubscriber_++;
        subscribers_[id] = {fn, user};
        return id;
    }

    void Service::Unsubscribe(std::uint32_t id) {
        std::lock_guard lock(subscribersMutex_);
        subscribers_.erase(id);
    }

    // ── The player's own named conditions and values ─────────────────────────

    void Service::RegisterUserNamed(bool isValue, const std::string& name, const nlohmann::json& json,
                                    std::vector<std::string>& errors) {
        const auto& ce = CE::Get();
        const auto bare = name.starts_with(isValue ? '$' : '@') ? name.substr(1) : name;
        if (!IsValidId(bare)) {
            errors.push_back("'" + name + "' is not a valid name (letters, digits and _)");
            return;
        }
        nlohmann::json def = json.is_string() ? nlohmann::json{{"expr", json}} : json;
        if (!def.is_object()) {
            errors.push_back("'" + name + "': expected an expression or {\"expr\": ...}");
            return;
        }
        def.erase("knowledge");  // ours, for SkyrimNet
        def["owner"] = kUserOwner;
        if (!def.contains("tags")) def["tags"] = {"user"};
        char error[512] = {};
        if (isValue) {
            const auto list = CE::ReadString([&](char* b, std::uint32_t n) { return ce.ListNamedValues(b, n); });
            try {
                for (const auto& v : nlohmann::json::parse(list)) {
                    if (!v.is_object() || !IEquals(v.value("name", ""), bare)) continue;
                    const auto owner = v.value("source", v.value("owner", ""));
                    if (!IEquals(owner, kUserOwner) && !IEquals(owner, "implicit")) {
                        errors.push_back("a value named $" + bare + " already exists (from " + owner + ")");
                        return;
                    }
                }
            } catch (...) {
            }
            if (ce.RegisterNamedValue(bare.c_str(), def.dump().c_str(), kUserOwner, error, sizeof(error)) != CE_OK) {
                errors.push_back("$" + bare + ": " + error);
            }
        } else {
            std::uint32_t deps = 0;
            const bool known = ce.LookupNamed(bare.c_str(), &deps) != 0;
            const bool ours = std::any_of(userConditions_.begin(), userConditions_.end(),
                                          [&](const auto& e) { return IEquals(e.first, bare); });
            if (known && !ours) {
                errors.push_back("a condition named @" + bare + " already exists; pick another name");
                return;
            }
            if (ce.RegisterNamed(bare.c_str(), def.dump().c_str(), kUserOwner, error, sizeof(error)) != CE_OK) {
                errors.push_back("@" + bare + ": " + error);
            }
        }
    }

    Outcome Service::SaveUserNamed(bool isValue, const std::string& rawName, const nlohmann::json& json,
                                   const std::string& previousName) {
        std::lock_guard lock(mutex_);
        const auto name = rawName.starts_with(isValue ? '$' : '@') ? rawName.substr(1) : rawName;
        auto& store = isValue ? userValues_ : userConditions_;
        Outcome o;
        RegisterUserNamed(isValue, name, json, o.errors);
        if (!o.errors.empty()) return o;
        const auto& ce = CE::Get();
        auto erase = [&](const std::string& n, bool unregister) {
            for (auto it = store.begin(); it != store.end(); ++it) {
                if (!IEquals(it->first, n)) continue;
                if (unregister) {
                    if (isValue) ce.UnregisterNamedValue(it->first.c_str());
                    else ce.UnregisterNamed(it->first.c_str());
                }
                store.erase(it);
                return;
            }
        };
        if (!previousName.empty() && !IEquals(previousName, name)) erase(previousName, true);  // renamed
        erase(name, false);
        store[name] = json;
        std::string error;
        if (!WriteUserFile(error)) o.warnings.push_back(error);
        o.ok = true;
        DefinitionsChanged();
        return o;
    }

    bool Service::DeleteUserNamed(bool isValue, const std::string& name) {
        std::lock_guard lock(mutex_);
        auto& store = isValue ? userValues_ : userConditions_;
        for (auto it = store.begin(); it != store.end(); ++it) {
            if (!IEquals(it->first, name)) continue;
            if (isValue) CE::Get().UnregisterNamedValue(it->first.c_str());
            else CE::Get().UnregisterNamed(it->first.c_str());
            store.erase(it);
            std::string error;
            if (!WriteUserFile(error)) SKSE::log::warn("Dials: {}", error);
            DefinitionsChanged();
            return true;
        }
        return false;
    }

    nlohmann::json Service::EditorPrefs() {
        std::lock_guard lock(mutex_);
        return editorPrefs_;
    }

    bool Service::SetEditorPrefs(const nlohmann::json& prefs, std::string& error) {
        std::lock_guard lock(mutex_);
        for (const auto& [key, value] : prefs.items()) editorPrefs_[key] = value;
        return WriteUserFile(error);
    }

    void Service::DefinitionsChanged() {
        if (onDefinitionsChanged) onDefinitionsChanged();
    }

    // ── For SkyrimNet ────────────────────────────────────────────────────────

    nlohmann::json Service::DialsFor(ActorKey actor) {
        nlohmann::json out = nlohmann::json::object();
        for (const auto& def : engine_.Definitions()) {
            const double v = engine_.Get(def.id, actor).value_or(def.defaultValue);
            switch (def.kind) {
                case Kind::Toggle: out[def.id] = v >= 0.5; break;
                case Kind::Choice: {
                    const auto label = def.OptionLabel(v);
                    if (label.empty()) out[def.id] = v;
                    else out[def.id] = label;
                    break;
                }
                default: out[def.id] = def.integer ? std::round(v) : std::round(v * 100.0) / 100.0; break;
            }
        }
        return out;
    }

    std::vector<Service::Knowledge> Service::KnowledgeEntries() {
        std::vector<Knowledge> out;
        for (const auto& def : engine_.Definitions()) {
            for (const auto& [name, text] : def.knowledge) out.push_back({name, text});
        }
        std::lock_guard lock(mutex_);
        for (const auto& [name, j] : userConditions_) {
            if (j.is_object() && j.contains("knowledge") && j["knowledge"].is_string() &&
                !j["knowledge"].get<std::string>().empty()) {
                out.push_back({name, j["knowledge"].get<std::string>()});
            }
        }
        return out;
    }

    std::vector<Service::AiAction> Service::AiActions() {
        std::vector<AiAction> out;
        for (const auto& def : engine_.Definitions()) {
            if (!def.enabled) continue;
            for (std::size_t i = 0; i < def.rules.size(); ++i) {
                const auto& r = def.rules[i];
                if (!r.IsAiAction()) continue;
                const auto name = r.AiActionName();
                auto it = std::find_if(out.begin(), out.end(), [&](const auto& a) { return IEquals(a.name, name); });
                if (it == out.end()) {
                    out.push_back({name, r.description, {}});
                    it = std::prev(out.end());
                }
                it->rules.emplace_back(def.id, i);
            }
        }
        return out;
    }

    std::vector<std::string> Service::SkyrimNetEventTypes() {
        std::vector<std::string> out;
        for (const auto& def : engine_.Definitions()) {
            if (!def.enabled) continue;
            for (const auto& r : def.rules) {
                if (r.trigger != TriggerKind::Event || !r.event.starts_with("SkyrimNet:")) continue;
                const auto type = Lower(r.event.substr(10));
                if (std::find(out.begin(), out.end(), type) == out.end()) out.push_back(type);
            }
        }
        return out;
    }

} // namespace ConditionEngine::Dials
