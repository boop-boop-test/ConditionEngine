#include "Bridge.h"

#include "../Dials/CEApi.h"
#include "../Dials/GameHost.h"
#include "../Dials/Service.h"
#include "../Dials/Settings.h"
#include "ConditionEngine/ConditionEngineAPI.h"

// SkyrimNet's devkit header: defines the function pointers, so it is included here only
#include "SkyrimNetPublicAPI.h"

#include <condition_variable>
#include <format>
#include <unordered_set>

namespace ConditionEngine::SkyrimNet {

    namespace {
        using Dials::ActorKey;
        using Dials::kPlayer;
        using Dials::Service;
        using Clock = std::chrono::steady_clock;

        constexpr const char* kDialsDecorator = "get_dials";
        constexpr const char* kConditionsDecorator = "condition_engine";
        constexpr const char* kKnowledgePrefix = "ConditionEngine: ";
        constexpr auto kForgetAfter = std::chrono::seconds(30);  // an actor nobody asked about for this long
        constexpr std::size_t kMaxActors = 16;
        constexpr auto kFirstAnswerWait = std::chrono::milliseconds(300);

        bool g_available = false;
        int g_version = 0;
        std::atomic<bool> g_definitionsDirty{true};
        std::atomic<bool> g_knowledgeDirty{true};
        Clock::time_point g_lastRefresh{};
        Clock::time_point g_lastDefinitionsChange{};
        std::vector<std::string> g_problems;

        // ── The cache SkyrimNet's threads read ───────────────────────────────
        struct Entry {
            std::string text = R"({"conditions": {}, "values": {}, "ready": false})";
            std::unordered_map<std::string, bool> eligible;  // AI action name -> offered
            bool ready = false;
            bool queued = false;
            Clock::time_point askedAt{};
        };
        std::mutex g_mutex;
        std::condition_variable g_ready;
        std::unordered_map<RE::FormID, Entry> g_cache;

        // Names worth answering, and compiled values: rebuilt when definitions change (main thread)
        std::vector<std::string> g_conditionNames;
        std::vector<std::pair<std::string, CE_ValueHandle>> g_values;
        std::vector<Service::AiAction> g_actions;

        std::unordered_set<std::string> g_eventTypes;  // callbacks registered
        std::unordered_set<std::string> g_actionNames;  // actions registered

        RE::FormID Fid(RE::Actor* actor) { return actor ? actor->GetFormID() : kPlayer; }

        void Problem(std::string text) {
            SKSE::log::warn("SkyrimNet bridge: {}", text);
            g_problems.push_back(std::move(text));
            if (g_problems.size() > 20) g_problems.erase(g_problems.begin());
        }

        // ── Main thread: what condition_engine() answers ─────────────────────
        void RebuildNames() {
            for (auto& [_, h] : g_values) ConditionEngine_ReleaseValue(h);
            g_values.clear();
            g_conditionNames.clear();
            const auto catalog = Dials::CE::ReadString([](char* b, std::uint32_t n) {
                return ConditionEngine_GetCatalog(R"({"include": ["values", "conditions"], "offered": false})", b, n);
            });
            try {
                const auto j = nlohmann::json::parse(catalog);
                for (const auto& c : j.value("namedConditions", nlohmann::json::array())) {
                    if (auto id = c.value("id", std::string()); !id.empty()) g_conditionNames.push_back(id);
                }
                for (const auto& v : j.value("namedValues", nlohmann::json::array())) {
                    const auto name = v.value("name", std::string());
                    if (name.empty()) continue;
                    char error[256] = {};
                    if (const auto h = ConditionEngine_CompileValue(name.c_str(), error, sizeof(error))) g_values.emplace_back(name, h);
                }
            } catch (const std::exception& e) {
                Problem(std::string("catalog not readable: ") + e.what());
            }
            g_actions = Service::Get().AiActions();
        }

        void Refresh(RE::FormID fid) {
            auto* actor = Dials::ActorOf(fid);
            nlohmann::json conditions = nlohmann::json::object();
            nlohmann::json values = nlohmann::json::object();
            std::unordered_map<std::string, bool> eligible;
            if (actor) {
                for (const auto& name : g_conditionNames) {
                    const auto r = ConditionEngine_EvaluateNamed(name.c_str(), actor);
                    if (r >= 0) conditions[name] = r == 1;
                }
                for (const auto& [name, handle] : g_values) {
                    CE_ValueSample sample{};
                    sample.structSize = sizeof(sample);
                    if (ConditionEngine_EvaluateValue(handle, actor, &sample, nullptr, 0) == CE_OK &&
                        sample.status == CE_VALUE_OK) {
                        values[name] = std::round(sample.value * 100.0) / 100.0;
                    }
                }
                auto& engine = Service::Get().GetEngine();
                for (const auto& action : g_actions) {
                    bool any = false;
                    for (const auto& [dial, rule] : action.rules) any = any || engine.Eligible(dial, rule, fid);
                    eligible[action.name] = any;
                }
            }
            const nlohmann::json answer = {{"conditions", std::move(conditions)}, {"values", std::move(values)}, {"ready", actor != nullptr}};
            {
                std::lock_guard lock(g_mutex);
                auto& e = g_cache[fid];
                e.text = answer.dump();
                e.eligible = std::move(eligible);
                e.ready = true;
                e.queued = false;
            }
            g_ready.notify_all();
        }

        // Any thread: queue a main-thread refresh (once)
        void Queue(RE::FormID fid) {
            {
                std::lock_guard lock(g_mutex);
                auto& e = g_cache[fid];
                if (e.queued) return;
                e.queued = true;
            }
            if (auto* tasks = SKSE::GetTaskInterface()) tasks->AddTask([fid]() { Refresh(fid); });
        }

        // SkyrimNet's thread
        std::string AnswerConditions(RE::Actor* actor) {
            const auto fid = Fid(actor);
            std::unique_lock lock(g_mutex);
            if (g_cache.size() >= kMaxActors * 4 && !g_cache.contains(fid)) g_cache.clear();
            auto& e = g_cache[fid];
            e.askedAt = Clock::now();
            if (!e.ready) {
                lock.unlock();
                Queue(fid);
                lock.lock();
                g_ready.wait_for(lock, kFirstAnswerWait, [&] { return g_cache[fid].ready; });
            }
            return g_cache[fid].text;
        }

        bool AnswerEligible(const std::string& action, RE::Actor* actor) {
            const auto fid = Fid(actor);
            std::unique_lock lock(g_mutex);
            auto& e = g_cache[fid];
            e.askedAt = Clock::now();
            if (!e.ready) {
                lock.unlock();
                Queue(fid);
                return false;
            }
            auto it = e.eligible.find(action);
            return it != e.eligible.end() && it->second;
        }

        // ── Registration ─────────────────────────────────────────────────────
        void RegisterDecorators() {
            if (!PublicRegisterDecorator) return;
            const bool dials = PublicRegisterDecorator(
                kDialsDecorator,
                "ConditionEngine dials: every dial's value for this actor, as an object. Numbers, toggles as "
                "true / false, choice dials as their option label: {{ get_dials(npc.UUID).wetness }}, "
                "{% if get_dials(npc.UUID).manic %}. Global dials read the same for everyone; player-only dials "
                "read their default for anyone else. Make dials in the ConditionEngine editor (ce ui).",
                [](RE::Actor* actor) -> std::string { return Service::Get().DialsFor(Fid(actor)).dump(); });
            const bool conditions = PublicRegisterDecorator(
                kConditionsDecorator,
                "ConditionEngine: every named condition (true / false) and named value (a number) for this actor: "
                "{% if default(condition_engine(npc.UUID).conditions.Soaked, false) %}, "
                "{{ default(condition_engine(npc.UUID).values.hunger, 0) }}. Kept fresh every second or two; "
                "\"ready\" is false until the first read for an actor arrives. Wrap names in default(): a "
                "condition whose mod is missing is left out.",
                [](RE::Actor* actor) -> std::string { return AnswerConditions(actor); });
            if (!dials) Problem(std::string("decorator ") + kDialsDecorator + " not registered (name taken?)");
            if (!conditions) Problem(std::string("decorator ") + kConditionsDecorator + " not registered (name taken?)");
            SKSE::log::info("SkyrimNet bridge: decorators {} {}, {} {}", kDialsDecorator, dials ? "on" : "failed",
                            kConditionsDecorator, conditions ? "on" : "failed");
        }

        void RegisterEventCallbacks() {
            if (!PublicRegisterEventCallback) return;
            for (const auto& type : Service::Get().SkyrimNetEventTypes()) {
                if (g_eventTypes.contains(type)) continue;
                const auto id = PublicRegisterEventCallback(type.c_str(), [type](const char* raw) {
                    // SkyrimNet's ThreadPool: copy what we need, run the rules on the main thread
                    Dials::EventInfo info;
                    try {
                        const auto j = nlohmann::json::parse(raw ? raw : "{}");
                        info.actor = j.value("originatingActorFormId", 0u);
                        info.other = j.value("targetActorFormId", 0u);
                        if (auto data = j.find("data"); data != j.end() && data->is_string()) {
                            const auto d = nlohmann::json::parse(data->get<std::string>(), nullptr, false);
                            if (d.is_object()) info.text = d.value("text", d.value("line", std::string()));
                        }
                    } catch (...) {
                        return;
                    }
                    if (auto* tasks = SKSE::GetTaskInterface()) {
                        tasks->AddTask([type, info]() {
                            auto& s = Service::Get();
                            if (!s.InSession()) return;
                            s.GetEngine().OnModEvent("SkyrimNet:" + type, info);
                            s.Publish();
                        });
                    }
                });
                if (id) {
                    g_eventTypes.insert(type);
                    SKSE::log::info("SkyrimNet bridge: listening for '{}' events", type);
                } else {
                    Problem("could not listen for SkyrimNet '" + type + "' events");
                }
            }
        }

        void RegisterActions(bool late) {
            if (!PublicRegisterCPPAction) return;
            for (const auto& action : Service::Get().AiActions()) {
                if (g_actionNames.contains(Dials::Lower(action.name))) continue;
                const auto name = action.name;
                const bool ok = PublicRegisterCPPAction(
                    name, action.description, [name](RE::Actor* actor) { return AnswerEligible(name, actor); },
                    [name](RE::Actor* actor, std::string) {
                        if (!actor) return false;
                        Dials::EventInfo info;
                        info.actor = actor->GetFormID();
                        const std::string event = "SkyrimNetAction:" + name;
                        if (auto* tasks = SKSE::GetTaskInterface()) {
                            tasks->AddTask([event, info]() {
                                auto& s = Service::Get();
                                s.GetEngine().OnModEvent(event, info);
                                s.Publish();
                            });
                        }
                        return true;
                    },
                    "", "Social", 50, "{}", "", "", "conditionengine,dials");
                if (ok) {
                    g_actionNames.insert(Dials::Lower(name));
                    SKSE::log::info("SkyrimNet bridge: AI action '{}'{}", name,
                                    late ? " (added after start-up: SkyrimNet may only offer it after a restart)" : "");
                } else {
                    Problem("AI action '" + name + "' not registered (name taken?)");
                }
            }
        }

        // World knowledge: ours carry a display name with our prefix; every sync replaces them all
        void SyncKnowledge() {
            if (!PublicAddWorldKnowledge || !PublicGetWorldKnowledge || !PublicRemoveWorldKnowledge) return;
            if (PublicIsMemorySystemReady && !PublicIsMemorySystemReady()) return;  // try again next tick
            g_knowledgeDirty.store(false);
            std::size_t removed = 0, added = 0;
            try {
                const auto existing = nlohmann::json::parse(PublicGetWorldKnowledge(1000));
                for (const auto& k : existing) {
                    if (!k.is_object() || !k.value("display_name", std::string()).starts_with(kKnowledgePrefix)) continue;
                    if (PublicRemoveWorldKnowledge(k.value("id", 0))) ++removed;
                }
            } catch (const std::exception& e) {
                Problem(std::string("world knowledge not readable: ") + e.what());
            }
            for (const auto& k : Service::Get().KnowledgeEntries()) {
                const auto condition = std::format("default(condition_engine(actorUUID).conditions.{}, false)", k.condition);
                const auto display = kKnowledgePrefix + k.condition;
                if (PublicAddWorldKnowledge(k.text.c_str(), condition.c_str(), true, 0.7f, display.c_str()) > 0) ++added;
                else Problem("world knowledge for @" + k.condition + " not added");
            }
            if (removed || added) SKSE::log::info("SkyrimNet bridge: world knowledge synced ({} replaced, {} now)", removed, added);
        }

        std::string Fill(std::string text, const Dials::Note& note) {
            auto replace = [&](std::string_view key, const std::string& with) {
                for (auto pos = text.find(key); pos != std::string::npos; pos = text.find(key, pos + with.size())) {
                    text.replace(pos, key.size(), with);
                }
            };
            auto* actor = Dials::ActorOf(note.actor);
            const char* name = actor ? actor->GetName() : nullptr;
            replace("{name}", name && *name ? name : "Someone");
            replace("{value}", std::format("{:g}", note.value));
            replace("{label}", note.label);
            return text;
        }

        void Tell(std::vector<Dials::Note>&& notes) {
            if (!PublicRegisterEvent) return;
            for (const auto& note : notes) {
                auto* actor = Dials::ActorOf(note.actor);
                if (!actor || !actor->Is3DLoaded()) continue;  // SkyrimNet only takes loaded actors
                const auto text = Fill(note.text, note);
                const char* type = note.narration ? "direct_narration" : "persistent_generic";
                if (!PublicRegisterEvent(type, text.c_str(), actor->GetFormID(), 0, nullptr, 0)) {
                    Problem("SkyrimNet did not take the event '" + text + "'");
                }
            }
        }
    }

    // ── Public ───────────────────────────────────────────────────────────────

    void Install() {
        static bool installed = false;
        if (installed) return;
        installed = true;
        if (!Dials::GetSettings().skyrimNet) {
            SKSE::log::info("SkyrimNet bridge: off ([SkyrimNet] bEnable)");
            return;
        }
        if (!FindFunctions()) {
            SKSE::log::info("SkyrimNet bridge: SkyrimNet not installed");
            return;
        }
        g_version = PublicGetVersion ? PublicGetVersion() : 0;
        if (g_version < 5) {
            Problem(std::format("SkyrimNet API {} is older than 5 (no decorators)", g_version));
            return;
        }
        g_available = true;
        auto& service = Service::Get();
        service.onNotes = [](std::vector<Dials::Note>&& notes) { Tell(std::move(notes)); };
        service.onDefinitionsChanged = [] {
            g_definitionsDirty.store(true);
            g_knowledgeDirty.store(true);
        };
        RebuildNames();
        RegisterDecorators();
        RegisterEventCallbacks();
        RegisterActions(false);
        g_definitionsDirty.store(false);
        SKSE::log::info("SkyrimNet bridge: SkyrimNet API {}", g_version);
    }

    void OnSessionStart() {
        if (!g_available) return;
        {
            std::lock_guard lock(g_mutex);
            g_cache.clear();  // last session's actors
        }
        g_knowledgeDirty.store(true);
    }

    void Tick() {
        if (!g_available) return;
        const auto now = Clock::now();
        if (g_definitionsDirty.exchange(false)) {
            g_lastDefinitionsChange = now;
            RebuildNames();
            RegisterEventCallbacks();
            RegisterActions(true);
        }
        // Knowledge waits for edits to settle, and for SkyrimNet's memory after a load
        if (g_knowledgeDirty.load() && now - g_lastDefinitionsChange > std::chrono::seconds(2)) SyncKnowledge();

        const auto every = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(Dials::GetSettings().skyrimNetRefresh));
        if (now - g_lastRefresh < every) return;
        g_lastRefresh = now;
        std::vector<RE::FormID> asked;
        {
            std::lock_guard lock(g_mutex);
            for (auto it = g_cache.begin(); it != g_cache.end();) {
                if (now - it->second.askedAt > kForgetAfter) {
                    it = g_cache.erase(it);
                } else {
                    asked.push_back(it->first);
                    ++it;
                }
            }
        }
        if (asked.size() > kMaxActors) asked.resize(kMaxActors);
        for (const auto fid : asked) Refresh(fid);
    }

    bool Available() { return g_available; }

    nlohmann::json Status() {
        nlohmann::json actions = nlohmann::json::array();
        for (const auto& a : g_actionNames) actions.push_back(a);
        nlohmann::json events = nlohmann::json::array();
        for (const auto& e : g_eventTypes) events.push_back(e);
        std::size_t cached = 0;
        {
            std::lock_guard lock(g_mutex);
            cached = g_cache.size();
        }
        return {{"available", g_available},
                {"version", g_version},
                {"decorators", g_available ? nlohmann::json::array({kDialsDecorator, kConditionsDecorator}) : nlohmann::json::array()},
                {"actions", std::move(actions)},
                {"events", std::move(events)},
                {"knowledge", Service::Get().KnowledgeEntries().size()},
                {"actorsCached", cached},
                {"problems", g_problems}};
    }

} // namespace ConditionEngine::SkyrimNet
