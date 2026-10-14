#include "ConditionEngine/Availability.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Conditions/CallbackCondition.h"
#include "ConditionEngine/Stats.h"
#include "ConditionEngine/Value.h"

namespace ConditionEngine {
    namespace {
        using json = nlohmann::json;
        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }
        void Merge(AvailabilityStatus& into, const AvailabilityStatus& other) {
            if (other.Ready()) return;
            if (into.Ready() || other.state == AvailabilityState::Unavailable) into.state = other.state;
            if (!other.reason.empty()) {
                if (!into.reason.empty()) into.reason += "; ";
                into.reason += other.reason;
            }
        }
    }

    namespace {
        // Set while checking a tree that references host-resolved named conditions (e.g.
        // DressCode rules): hosts change those without moving the revision, so such
        // results are not cached
        thread_local bool t_uncacheable = false;
    }

    AvailabilityStatus detail::CachedCheck(std::atomic<AvailabilityMemo*>& slot,
                                           const std::function<AvailabilityStatus()>& compute) {
        auto* memo = slot.load(std::memory_order_acquire);
        if (!memo) {
            auto* created = new AvailabilityMemo();
            if (slot.compare_exchange_strong(memo, created, std::memory_order_acq_rel)) {
                memo = created;
            } else {
                delete created;  // another thread won; `memo` now holds its pointer
            }
        }
        // Read the revision first: a change during compute() leaves the memo stale
        const auto revision = Availability::Get().Revision();
        {
            std::lock_guard lock(memo->mutex);
            if (memo->revision == revision) return memo->status;
        }
        const bool outer = t_uncacheable;
        t_uncacheable = false;
        auto status = compute();
        const bool uncacheable = t_uncacheable;
        t_uncacheable = outer || uncacheable;
        std::lock_guard lock(memo->mutex);
        memo->revision = uncacheable ? ~0ull : revision;
        memo->status = status;
        return status;
    }

    json AvailabilityStatus::ToJson() const {
        return {{"state", state == AvailabilityState::Available ? "available" :
                          state == AvailabilityState::Pending ? "pending" : "unavailable"},
                {"available", Ready()}, {"reason", reason}};
    }

    Availability& Availability::Get() { static Availability instance; return instance; }

    AvailabilityStatus Availability::PluginStatus(const std::string& plugin) const {
        std::lock_guard lock(mutex_);
        if (!pluginsReady_) return {AvailabilityState::Pending, "Waiting for loaded plugins: " + plugin};
        if (!plugins_.contains(Lower(plugin))) return {AvailabilityState::Unavailable, "Plugin not loaded: " + plugin};
        return {};
    }

    AvailabilityStatus Availability::CheckType(const TypeInfo& type) const {
        AvailabilityStatus result;
        {
            std::lock_guard lock(mutex_);
            if (const auto it = types_.find(type.name); it != types_.end()) result = it->second;
        }
        for (const auto& plugin : type.requiresPlugins) Merge(result, PluginStatus(plugin));
        return result;
    }

    AvailabilityStatus Availability::Check(const Condition& condition) const {
        const auto start = std::chrono::steady_clock::now();
        std::unordered_set<std::string> names;
        auto status = CheckNode(condition.ToJsonComplete(), names, 0);
        Stats::RecordAvailabilityCheck(std::chrono::steady_clock::now() - start);
        return status;
    }

    AvailabilityStatus Availability::CheckValue(const Value& value) const {
        const auto start = std::chrono::steady_clock::now();
        std::unordered_set<std::string> names;
        auto status = CheckValueNode(value.ToJson(), names, 0);
        Stats::RecordAvailabilityCheck(std::chrono::steady_clock::now() - start);
        return status;
    }

    AvailabilityStatus Availability::CheckValueNode(const json& node, std::unordered_set<std::string>& names,
                                                   std::size_t depth) const {
        if (depth > 64) return {AvailabilityState::Unavailable, "Value dependency nesting exceeds 64 levels"};
        if (!node.is_object()) return {};  // literals, ${param}
        const auto type = Lower(node.value("type", std::string()));
        AvailabilityStatus result;
        if (type == "math") {
            if (node.contains("args") && node["args"].is_array()) {
                for (const auto& arg : node["args"]) Merge(result, CheckValueNode(arg, names, depth + 1));
            }
            return result;
        }
        if (type == "truth") {
            return node.contains("condition") ? CheckNode(node["condition"], names, depth + 1) : result;
        }
        if (type == "namedvalue") {
            const auto name = node.value("name", std::string());
            const auto key = "$" + Lower(name);
            if (!names.insert(key).second) return {AvailabilityState::Unavailable, "Circular named value: " + name};
            if (auto def = Registry::Get().FindNamedValue(name)) {
                if (auto providers = Registry::Get().ProvidersOf(name); !providers.empty()) {
                    // Combined: available when any source (or its own expression) is
                    AvailabilityStatus none{AvailabilityState::Unavailable, ""};
                    bool any = false;
                    for (const auto& source : providers) {
                        auto status = CheckValueNode(json{{"type", "NamedValue"}, {"name", source->name}}, names, depth + 1);
                        if (status.Ready()) {
                            any = true;
                            break;
                        }
                        Merge(none, status);
                    }
                    if (!any && def->value) {
                        auto status = CheckValueNode(def->value->ToJson(), names, depth + 1);
                        any = status.Ready();
                        Merge(none, status);
                    }
                    names.erase(key);
                    if (any) return result;
                    if (none.reason.empty()) none.reason = "no source available for " + name;
                    return none;
                }
                for (const auto& plugin : def->requiresPlugins) Merge(result, PluginStatus(plugin));
                for (const auto& dll : def->requiresDlls) {
                    if (!GetModuleHandleA(dll.c_str())) Merge(result, {AvailabilityState::Unavailable, dll + " is not loaded"});
                }
                if (def->value) Merge(result, CheckValueNode(def->value->ToJson(), names, depth + 1));
                if (def->enabledWhen) Merge(result, CheckNode(def->enabledWhen->ToJsonComplete(), names, depth + 1));
            } else {
                Merge(result, {AvailabilityState::Unavailable, "no value named " + name});
            }
            names.erase(key);
            return result;
        }
        return CheckNode(node, names, depth);  // a numeric type
    }

    AvailabilityStatus Availability::CheckNode(const json& node, std::unordered_set<std::string>& names,
                                              std::size_t depth) const {
        if (depth > 64) return {AvailabilityState::Unavailable, "Condition dependency nesting exceeds 64 levels"};
        if (!node.is_object()) return {};
        const auto* type = Registry::Get().FindType(node.value("type", std::string()));
        if (!type) return {AvailabilityState::Unavailable, "Condition type is not registered: " + node.value("type", std::string())};
        auto result = CheckType(*type);
        for (const auto& field : type->fields) {
            if (!node.contains(field.key)) continue;
            const auto& value = node[field.key];
            if (field.type == FieldType::Conditions) {
                if (value.is_array()) for (const auto& child : value) Merge(result, CheckNode(child, names, depth + 1));
                else if (value.is_object()) Merge(result, CheckNode(value, names, depth + 1));
            } else if (field.type == FieldType::Value) {
                Merge(result, CheckValueNode(value, names, depth + 1));
            } else if (field.type == FieldType::Form || field.type == FieldType::FormList) {
                // Portable form keys name their owner plugin explicitly. Bare EDIDs cannot establish this dependency.
                auto checkForm = [&](const json& v) {
                    if (!v.is_string()) return;
                    const auto key = v.get<std::string>();
                    if (key.rfind("0x", 0) != 0 && key.rfind("0X", 0) != 0) return;
                    const auto bar = key.find('|');
                    if (bar != std::string::npos && bar + 1 < key.size()) Merge(result, PluginStatus(key.substr(bar + 1)));
                };
                if (value.is_array()) for (const auto& v : value) checkForm(v);
                else if (field.type == FieldType::FormList && value.is_string()) {
                    // Factories also accept comma-separated form lists.
                    for (const auto& key : ReadStringList(node, field.key)) checkForm(json(key));
                } else checkForm(value);
            }
        }
        if (type->name == "Rule") {
            auto key = node.value("ruleUUID", std::string());
            if (key.empty()) key = node.value("ruleName", std::string());
            if (!names.insert(key).second) {
                Merge(result, {AvailabilityState::Unavailable, "Circular named condition: " + key});
            } else {
                if (!Registry::Get().IsEngineNamed(key)) t_uncacheable = true;
                if (auto named = Registry::Get().FindNamed(key)) {
                    const auto definition = named->ToJsonComplete();
                    // Callback-backed named conditions use a self-reference as their serialization.
                    auto serializedKey = definition.value("ruleUUID", std::string());
                    if (serializedKey.empty()) serializedKey = definition.value("ruleName", std::string());
                    const bool opaqueSelfReference = dynamic_cast<const CallbackCondition*>(named.get()) &&
                        definition.value("type", std::string()) == "Rule" && serializedKey == key;
                    if (!opaqueSelfReference) Merge(result, CheckNode(definition, names, depth + 1));
                } else Merge(result, {AvailabilityState::Unavailable, "Named condition not found: " + key});
                names.erase(key);
            }
        }
        return result;
    }

    bool Availability::SetTypeStatus(const std::string& name, AvailabilityStatus status) {
        const auto* type = Registry::Get().FindType(name);
        if (!type) return false;
        if (status.Ready()) status.reason.clear();
        else if (status.reason.empty()) status.reason = type->label + " is " +
            (status.state == AvailabilityState::Pending ? "not ready yet" : "unavailable");
        {
            std::lock_guard lock(mutex_);
            const auto it = types_.find(type->name);
            const auto before = it == types_.end() ? AvailabilityStatus{} : it->second;
            if (before == status) return true;
            types_[type->name] = std::move(status);
        }
        Publish();
        return true;
    }

    void Availability::SetLoadedPlugins(const std::vector<std::string>& plugins) {
        std::unordered_set<std::string> next;
        for (const auto& plugin : plugins) next.insert(Lower(plugin));
        {
            std::lock_guard lock(mutex_);
            if (pluginsReady_ && plugins_ == next) return;
            plugins_ = std::move(next);
            pluginsReady_ = true;
        }
        Publish();
    }

    void Availability::RefreshLoadedPlugins() {
        const auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) return;
        std::vector<std::string> plugins;
        auto add = [&](const RE::TESFile* const* files, std::size_t count) {
            for (std::size_t i = 0; files && i < count; ++i) if (files[i]) plugins.emplace_back(files[i]->GetFilename());
        };
        add(data->GetLoadedMods(), data->GetLoadedModCount());
        add(data->GetLoadedLightMods(), data->GetLoadedLightModCount());
        SetLoadedPlugins(plugins);
    }

    std::uint64_t Availability::Revision() const { return revision_.load(std::memory_order_acquire); }
    std::uint32_t Availability::Subscribe(ChangedFn fn) {
        if (!fn) return 0;
        // Initialize builtins before installing a listener that may re-enter Registry::Get().
        (void)Registry::Get();
        std::lock_guard lock(mutex_);
        const auto id = nextSubscription_++;
        subscribers_.emplace(id, std::move(fn));
        return id;
    }
    void Availability::Unsubscribe(std::uint32_t id) { std::lock_guard lock(mutex_); subscribers_.erase(id); }
    void Availability::Publish() {
        std::vector<ChangedFn> callbacks;
        std::uint64_t revision;
        {
            std::lock_guard lock(mutex_);
            revision = revision_.fetch_add(1, std::memory_order_acq_rel) + 1;
            for (const auto& [id, fn] : subscribers_) { (void)id; callbacks.push_back(fn); }
        }
        for (const auto& fn : callbacks) fn(revision);
    }
}
