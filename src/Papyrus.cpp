/// @file Papyrus.cpp
/// @brief Papyrus natives for the ConditionEngine script (ConditionEngine.psc).

#include "Papyrus.h"

#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Expression.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Watcher.h"

#include <mutex>

namespace ConditionEngine::Papyrus {

    namespace {
        constexpr std::string_view kScriptName = "ConditionEngine";
        constexpr std::size_t kMaxCachedExpressions = 256;

        using VM = RE::BSScript::IVirtualMachine;
        using StaticFn = RE::StaticFunctionTag;

        // Scripts tend to evaluate the same few expressions repeatedly
        std::mutex g_cacheMutex;
        std::unordered_map<std::string, std::shared_ptr<const Condition>> g_compiled;

        std::mutex g_watchMutex;
        std::unordered_set<WatchId> g_scriptWatches;

        std::shared_ptr<const Condition> CompileCached(const std::string& expression, std::string* error = nullptr) {
            {
                std::lock_guard lock(g_cacheMutex);
                auto it = g_compiled.find(expression);
                if (it != g_compiled.end()) return it->second;
            }
            auto compiled = Expression::CompileAny(expression, error);
            if (!compiled) return nullptr;
            std::shared_ptr<const Condition> shared(std::move(compiled));
            std::lock_guard lock(g_cacheMutex);
            if (g_compiled.size() >= kMaxCachedExpressions) g_compiled.clear();
            g_compiled.emplace(expression, shared);
            return shared;
        }

        RE::Actor* ActorOrPlayer(RE::Actor* actor) {
            return actor ? actor : RE::PlayerCharacter::GetSingleton();
        }

        bool Evaluate(StaticFn*, RE::Actor* actor, RE::BSFixedString expression) {
            std::string error;
            auto condition = CompileCached(expression.c_str(), &error);
            if (!condition) {
                SKSE::log::warn("ConditionEngine.Evaluate: {} in \"{}\"", error, expression.c_str());
                return false;
            }
            EvaluationContext context(ActorOrPlayer(actor));
            return condition->Evaluate(context);
        }

        bool EvaluateNamed(StaticFn*, RE::Actor* actor, RE::BSFixedString name) {
            auto condition = Registry::Get().FindNamed(name.c_str());
            if (!condition) {
                SKSE::log::warn("ConditionEngine.EvaluateNamed: unknown condition '{}'", name.c_str());
                return false;
            }
            EvaluationContext context(ActorOrPlayer(actor));
            return condition->Evaluate(context);
        }

        bool RegisterNamed(StaticFn*, RE::BSFixedString name, RE::BSFixedString expression) {
            if (name.empty()) return false;
            std::string error;
            auto condition = Expression::CompileAny(expression.c_str(), &error);
            if (!condition) {
                SKSE::log::warn("ConditionEngine.RegisterNamed('{}'): {}", name.c_str(), error);
                return false;
            }
            Registry::Get().RegisterNamed(name.c_str(), std::shared_ptr<const Condition>(std::move(condition)),
                                          "papyrus", expression.c_str());
            return true;
        }

        bool UnregisterNamed(StaticFn*, RE::BSFixedString name) {
            return Registry::Get().UnregisterNamed(name.c_str());
        }

        // Empty string when the expression compiles and is fully configured
        RE::BSFixedString Validate(StaticFn*, RE::BSFixedString expression) {
            std::string error;
            auto condition = Expression::CompileAny(expression.c_str(), &error);
            if (!condition) return error;
            std::string joined;
            for (const auto& problem : condition->Validate()) {
                if (!joined.empty()) joined += "; ";
                joined += problem;
            }
            return joined;
        }

        RE::BSFixedString Describe(StaticFn*, RE::BSFixedString expression) {
            auto condition = CompileCached(expression.c_str());
            return condition ? condition->GetDescription() : std::string{};
        }

        RE::BSFixedString ToJson(StaticFn*, RE::BSFixedString expression) {
            auto condition = CompileCached(expression.c_str());
            return condition ? condition->ToJsonComplete().dump() : std::string{};
        }

        RE::BSFixedString Normalize(StaticFn*, RE::BSFixedString expression) {
            auto condition = CompileCached(expression.c_str());
            return condition ? Expression::ToExpression(*condition) : std::string{};
        }

        // Sends ModEvent asEventName(strArg = watch id, numArg = 1.0/0.0, sender = actor) on change
        std::int32_t Watch(StaticFn*, RE::Actor* actor, RE::BSFixedString expression, RE::BSFixedString eventName,
                           bool fireImmediately) {
            if (eventName.empty()) return 0;
            std::string error;
            auto condition = CompileCached(expression.c_str(), &error);
            if (!condition) {
                SKSE::log::warn("ConditionEngine.Watch: {} in \"{}\"", error, expression.c_str());
                return 0;
            }

            const std::string event = eventName.c_str();
            auto id = Watcher::Get().Watch(
                condition, actor,
                [event](WatchId watchId, RE::Actor* target, bool result) {
                    SKSE::ModCallbackEvent modEvent{event, std::to_string(watchId), result ? 1.0f : 0.0f, target};
                    if (auto* source = SKSE::GetModCallbackEventSource()) source->SendEvent(&modEvent);
                },
                fireImmediately);

            if (id != 0) {
                std::lock_guard lock(g_watchMutex);
                g_scriptWatches.insert(id);
            }
            return static_cast<std::int32_t>(id);
        }

        void Unwatch(StaticFn*, std::int32_t watchId) {
            Watcher::Get().Unwatch(static_cast<WatchId>(watchId));
            std::lock_guard lock(g_watchMutex);
            g_scriptWatches.erase(static_cast<WatchId>(watchId));
        }

        bool RegisterFunctions(VM* vm) {
            vm->RegisterFunction("Evaluate", kScriptName, Evaluate);
            vm->RegisterFunction("EvaluateNamed", kScriptName, EvaluateNamed);
            vm->RegisterFunction("RegisterNamed", kScriptName, RegisterNamed);
            vm->RegisterFunction("UnregisterNamed", kScriptName, UnregisterNamed);
            vm->RegisterFunction("Validate", kScriptName, Validate, true);
            vm->RegisterFunction("Describe", kScriptName, Describe, true);
            vm->RegisterFunction("ToJson", kScriptName, ToJson, true);
            vm->RegisterFunction("Normalize", kScriptName, Normalize, true);
            vm->RegisterFunction("Watch", kScriptName, Watch);
            vm->RegisterFunction("Unwatch", kScriptName, Unwatch);
            return true;
        }
    }

    void Register() {
        if (auto* papyrus = SKSE::GetPapyrusInterface()) {
            papyrus->Register(RegisterFunctions);
        }
    }

    void ClearScriptState() {
        std::unordered_set<WatchId> watches;
        {
            std::lock_guard lock(g_watchMutex);
            watches.swap(g_scriptWatches);
        }
        for (auto id : watches) Watcher::Get().Unwatch(id);
        Registry::Get().UnregisterSource("papyrus");
    }

} // namespace ConditionEngine::Papyrus
