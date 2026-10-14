#include "ConditionEngine/Values/RefreshOn.h"
#include "ConditionEngine/Conditions/PapyrusCallCondition.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Watcher.h"

namespace ConditionEngine::RefreshOn {

    namespace {
        using json = nlohmann::json;

        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        // Every PapyrusCall a definition reads, following $name references once each
        void CollectCalls(const json& node, std::vector<PapyrusCallCondition::Call>& out,
                          std::unordered_set<std::string>& seen, int depth) {
            if (depth > 64) return;
            if (node.is_array()) {
                for (const auto& item : node) CollectCalls(item, out, seen, depth + 1);
                return;
            }
            if (!node.is_object()) return;
            const auto type = Lower(node.value("type", std::string()));
            if (type == "papyruscall") {
                PapyrusCallCondition::Call call;
                call.script = node.value("script", "");
                call.function = node.value("function", "");
                call.form = node.value("form", "");
                call.passActor = node.value("passActor", true);
                out.push_back(std::move(call));
                return;
            }
            if (type == "namedvalue") {
                const auto name = node.value("name", std::string());
                if (!seen.insert(Lower(name)).second) return;
                if (auto def = Registry::Get().FindNamedValue(name)) {
                    if (def->value) CollectCalls(def->value->ToJson(), out, seen, depth + 1);
                    if (def->enabledWhen) CollectCalls(def->enabledWhen->ToJsonComplete(), out, seen, depth + 1);
                }
                return;
            }
            for (const auto& [key, child] : node.items()) CollectCalls(child, out, seen, depth + 1);
        }

        class Sink final : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
        public:
            RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* e,
                                                  RE::BSTEventSource<SKSE::ModCallbackEvent>*) override {
                if (e && e->eventName.c_str()) OnModEvent(e->eventName.c_str(), e->sender);
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void Install() {
        static bool installed = false;
        if (installed) return;
        if (auto* source = SKSE::GetModCallbackEventSource()) {
            static Sink sink;
            source->AddEventSink(&sink);
            installed = true;
        }
    }

    std::size_t OnModEvent(std::string_view eventName, RE::TESForm* sender) {
        const auto defs = Registry::Get().ValuesRefreshedBy(eventName);
        if (defs.empty()) return 0;

        auto* actor = sender ? sender->As<RE::Actor>() : nullptr;
        const RE::FormID actorId = actor ? actor->GetFormID() : 0;

        // Re-dispatch the async reads first, so the re-sample below starts them
        std::vector<PapyrusCallCondition::Call> calls;
        std::unordered_set<std::string> seen;
        for (const auto& def : defs) {
            seen.insert(Lower(def->name));
            if (def->value) CollectCalls(def->value->ToJson(), calls, seen, 0);
            if (def->enabledWhen) CollectCalls(def->enabledWhen->ToJsonComplete(), calls, seen, 0);
        }
        for (const auto& call : calls) PapyrusCalls::Get().Expire(call, call.passActor ? actorId : 0);

        for (const auto& def : defs) {
            Watcher::Get().PostValueChange("$" + Lower(def->name), actor ? actorId : Watcher::kAllActors);
        }
        return defs.size();
    }

} // namespace ConditionEngine::RefreshOn
