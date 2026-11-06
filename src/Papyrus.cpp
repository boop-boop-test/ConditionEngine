/// @file Papyrus.cpp
/// @brief Papyrus natives for the ConditionEngine script (ConditionEngine.psc).

#include "Papyrus.h"

#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Explain.h"
#include "ConditionEngine/Expression.h"
#include "ConditionEngine/People.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Value.h"
#include "ConditionEngine/Values/PushedValues.h"
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

        std::unordered_map<std::string, std::shared_ptr<const Value>> g_compiledValues;

        std::shared_ptr<const Value> CompileValueCached(const std::string& expression, std::string* error = nullptr) {
            {
                std::lock_guard lock(g_cacheMutex);
                auto it = g_compiledValues.find(expression);
                if (it != g_compiledValues.end()) return it->second;
            }
            auto compiled = Expression::CompileValueAny(expression, error);
            if (!compiled) return nullptr;
            std::shared_ptr<const Value> shared(std::move(compiled));
            std::lock_guard lock(g_cacheMutex);
            if (g_compiledValues.size() >= kMaxCachedExpressions) g_compiledValues.clear();
            g_compiledValues.emplace(expression, shared);
            return shared;
        }

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

        using Latent = RE::BSScript::LatentStatus;
        using InternalVM = RE::BSScript::Internal::VirtualMachine;

        // Latent native returning R to the script. Equivalent to
        // IVirtualMachine::RegisterLatentFunction<R>, whose CommonLib implementation
        // (NativeLatentFunction) does not compile: it calls GetRawType<R>() instead of
        // GetRawType<R>{}().
        template <class R, class... Args>
        class LatentFunction final
            : public RE::BSScript::NativeFunction<true, Latent(InternalVM*, RE::VMStackID, StaticFn*, Args...),
                                                  std::underlying_type_t<Latent>, StaticFn*, Args...> {
            using Base = RE::BSScript::NativeFunction<true, Latent(InternalVM*, RE::VMStackID, StaticFn*, Args...),
                                                      std::underlying_type_t<Latent>, StaticFn*, Args...>;

        public:
            LatentFunction(std::string_view name, std::string_view className,
                           Latent (*callback)(InternalVM*, RE::VMStackID, StaticFn*, Args...))
                : Base(name, className, callback) {
                this->_retType = RE::BSScript::GetRawType<R>{}();
                this->_isLatent = true;
            }
        };

        template <class R, class... Args>
        void RegisterLatent(VM* vm, std::string_view name, Latent (*callback)(InternalVM*, RE::VMStackID, StaticFn*, Args...)) {
            vm->BindNativeMethod(new LatentFunction<R, Args...>(name, kScriptName, callback));
        }

        // Evaluation reads game state (and Game(...) runs the engine's condition
        // evaluator), so it must happen on the main thread. Latent natives let the
        // script wait one frame for the result instead of evaluating on a VM thread.
        Latent EvaluateOnMainThread(InternalVM* vm, RE::VMStackID stackId, std::shared_ptr<const Condition> condition,
                                    RE::Actor* actor) {
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) return Latent::kFailed;
            const RE::ActorHandle handle = actor ? actor->GetHandle() : RE::ActorHandle{};
            const bool usePlayer = actor == nullptr;
            tasks->AddTask([vm, stackId, condition = std::move(condition), handle, usePlayer]() {
                bool result = false;
                RE::Actor* target = usePlayer ? RE::PlayerCharacter::GetSingleton() : handle.get().get();
                if (condition && target) {
                    EvaluationContext context(target);
                    result = condition->EvaluateAvailable(context);
                }
                vm->ReturnLatentResult<bool>(stackId, result);
            });
            return Latent::kStarted;
        }

        Latent Evaluate(InternalVM* vm, RE::VMStackID stackId, StaticFn*, RE::Actor* actor,
                        RE::BSFixedString expression) {
            std::string error;
            auto condition = CompileCached(expression.c_str(), &error);
            if (!condition) {
                SKSE::log::warn("ConditionEngine.Evaluate: {} in \"{}\"", error, expression.c_str());
            }
            // Invalid expressions still complete (as false) so the script never hangs
            return EvaluateOnMainThread(vm, stackId, std::move(condition), actor);
        }

        Latent EvaluateNamed(InternalVM* vm, RE::VMStackID stackId, StaticFn*, RE::Actor* actor,
                             RE::BSFixedString name) {
            auto condition = Registry::Get().FindNamed(name.c_str());
            if (!condition) {
                SKSE::log::warn("ConditionEngine.EvaluateNamed: unknown condition '{}'", name.c_str());
            }
            return EvaluateOnMainThread(vm, stackId, std::move(condition), actor);
        }

        // ── Values ──────────────────────────────────────────────────────────

        // Reads a value on the main thread; the script waits one frame. Not-OK values
        // return their fallback (0 when there is none).
        Latent GetValue(InternalVM* vm, RE::VMStackID stackId, StaticFn*, RE::Actor* actor, RE::BSFixedString nameOrExpr) {
            std::string error;
            auto value = CompileValueCached(nameOrExpr.c_str(), &error);
            if (!value) SKSE::log::warn("ConditionEngine.GetValue: {} in \"{}\"", error, nameOrExpr.c_str());
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) return Latent::kFailed;
            const RE::ActorHandle handle = actor ? actor->GetHandle() : RE::ActorHandle{};
            const bool usePlayer = actor == nullptr;
            tasks->AddTask([vm, stackId, value = std::move(value), handle, usePlayer]() {
                float result = 0.0f;
                RE::Actor* target = usePlayer ? RE::PlayerCharacter::GetSingleton() : handle.get().get();
                if (value && target) {
                    EvaluationContext context(target);
                    result = static_cast<float>(ReadValueAvailable(*value, context).value);
                }
                vm->ReturnLatentResult<float>(stackId, result);
            });
            return Latent::kStarted;
        }

        // Which source answered a combined value for the actor ("sunhelm_hunger"); "" when none is
        // available or the value is not combined
        Latent GetValueSource(InternalVM* vm, RE::VMStackID stackId, StaticFn*, RE::Actor* actor, RE::BSFixedString nameOrExpr) {
            std::string error;
            auto value = CompileValueCached(nameOrExpr.c_str(), &error);
            if (!value) SKSE::log::warn("ConditionEngine.GetValueSource: {} in \"{}\"", error, nameOrExpr.c_str());
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) return Latent::kFailed;
            const RE::ActorHandle handle = actor ? actor->GetHandle() : RE::ActorHandle{};
            const bool usePlayer = actor == nullptr;
            tasks->AddTask([vm, stackId, value = std::move(value), handle, usePlayer]() {
                std::string source;
                RE::Actor* target = usePlayer ? RE::PlayerCharacter::GetSingleton() : handle.get().get();
                if (value && target) {
                    EvaluationContext context(target);
                    const auto read = ReadValueAvailable(*value, context);
                    if (read.Ok() && read.from && !read.fromOwnExpression) source = *read.from;
                }
                vm->ReturnLatentResult<RE::BSFixedString>(stackId, RE::BSFixedString(source.c_str()));
            });
            return Latent::kStarted;
        }

        // Plain-text explanation of a condition (or, if it is not one, a value) for the actor
        Latent Explain(InternalVM* vm, RE::VMStackID stackId, StaticFn*, RE::Actor* actor, RE::BSFixedString expression) {
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) return Latent::kFailed;
            const std::string text = expression.c_str();
            const RE::ActorHandle handle = actor ? actor->GetHandle() : RE::ActorHandle{};
            const bool usePlayer = actor == nullptr;
            tasks->AddTask([vm, stackId, text, handle, usePlayer]() {
                std::string out;
                RE::Actor* target = usePlayer ? RE::PlayerCharacter::GetSingleton() : handle.get().get();
                std::string error;
                if (!target) {
                    out = "actor not loaded";
                } else if (auto condition = CompileCached(text, &error)) {
                    EvaluationContext context(target);
                    out = Explain::ToText(Explain::ExplainCondition(*condition, context));
                } else if (auto value = CompileValueCached(text, &error)) {
                    EvaluationContext context(target);
                    out = Explain::ToText(Explain::ExplainValue(*value, context));
                } else {
                    out = "not a condition or value: " + error;
                }
                vm->ReturnLatentResult<RE::BSFixedString>(stackId, RE::BSFixedString(out));
            });
            return Latent::kStarted;
        }

        bool SetValue(StaticFn*, RE::Actor* actor, RE::BSFixedString name, float value, float timeoutSeconds) {
            return PushedValues::Get().Set(actor, name.c_str(), value, timeoutSeconds);
        }

        bool SetGlobalValue(StaticFn*, RE::BSFixedString name, float value, float timeoutSeconds) {
            return PushedValues::Get().SetGlobal(name.c_str(), value, timeoutSeconds);
        }

        bool ClearValue(StaticFn*, RE::Actor* actor, RE::BSFixedString name) {
            return PushedValues::Get().Clear(actor, name.c_str());
        }

        bool ClearGlobalValue(StaticFn*, RE::BSFixedString name) {
            return PushedValues::Get().ClearGlobal(name.c_str());
        }

        // asDefinition: value text ("$a * 2") or a JSON definition object
        bool RegisterNamedValue(StaticFn*, RE::BSFixedString name, RE::BSFixedString definition) {
            const std::string text = definition.c_str();
            nlohmann::json parsed = text;
            const auto first = text.find_first_not_of(" \t\r\n");
            if (first != std::string::npos && text[first] == '{') {
                try {
                    parsed = nlohmann::json::parse(text);
                } catch (const std::exception& e) {
                    SKSE::log::warn("ConditionEngine.RegisterNamedValue('{}'): invalid JSON: {}", name.c_str(), e.what());
                    return false;
                }
            }
            std::string error;
            auto def = NamedValueDef::Parse(name.c_str(), parsed, "papyrus", &error);
            if (!def) {
                SKSE::log::warn("ConditionEngine.RegisterNamedValue('{}'): {}", name.c_str(), error);
                return false;
            }
            Registry::Get().RegisterNamedValue(std::move(def));
            Watcher::Get().PostEvent(EventDependency::Value, Watcher::kAllActors);
            return true;
        }

        // Sends ModEvent asEventName(strArg = watch id, numArg = value, sender = actor) when the
        // value moves by afMinStep (any change when 0)
        std::int32_t WatchValue(StaticFn*, RE::Actor* actor, RE::BSFixedString nameOrExpr, RE::BSFixedString eventName,
                                float minStep, float pollIntervalSeconds, bool fireImmediately) {
            if (eventName.empty()) return 0;
            std::string error;
            auto value = CompileValueCached(nameOrExpr.c_str(), &error);
            if (!value) {
                SKSE::log::warn("ConditionEngine.WatchValue: {} in \"{}\"", error, nameOrExpr.c_str());
                return 0;
            }

            ValueWatchOptions options;
            options.fireImmediately = fireImmediately;
            options.minStep = std::max(0.0f, minStep);
            if (pollIntervalSeconds < 0.0f) {
                options.pollInterval = WatchOptions::kNever;
            } else if (pollIntervalSeconds > 0.0f) {
                options.pollInterval = std::chrono::milliseconds(static_cast<long long>(pollIntervalSeconds * 1000.0f));
            }

            const std::string event = eventName.c_str();
            auto id = Watcher::Get().WatchValue(
                value, actor,
                [event](WatchId watchId, RE::Actor* target, const ValueSample& sample, int) {
                    SKSE::ModCallbackEvent modEvent{event, std::to_string(watchId), static_cast<float>(sample.value), target};
                    if (auto* source = SKSE::GetModCallbackEventSource()) source->SendEvent(&modEvent);
                },
                options);

            if (id != 0) {
                std::lock_guard lock(g_watchMutex);
                g_scriptWatches.insert(id);
            }
            return static_cast<std::int32_t>(id);
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
        // pollIntervalSeconds: 0 = global default, < 0 = never (change detection/events only)
        std::int32_t Watch(StaticFn*, RE::Actor* actor, RE::BSFixedString expression, RE::BSFixedString eventName,
                           bool fireImmediately, float pollIntervalSeconds) {
            if (eventName.empty()) return 0;
            std::string error;
            auto condition = CompileCached(expression.c_str(), &error);
            if (!condition) {
                SKSE::log::warn("ConditionEngine.Watch: {} in \"{}\"", error, expression.c_str());
                return 0;
            }

            WatchOptions options;
            options.fireImmediately = fireImmediately;
            if (pollIntervalSeconds < 0.0f) {
                options.pollInterval = WatchOptions::kNever;
            } else if (pollIntervalSeconds > 0.0f) {
                options.pollInterval = std::chrono::milliseconds(static_cast<long long>(pollIntervalSeconds * 1000.0f));
            }

            const std::string event = eventName.c_str();
            auto id = Watcher::Get().Watch(
                condition, actor,
                [event](WatchId watchId, RE::Actor* target, bool result) {
                    SKSE::ModCallbackEvent modEvent{event, std::to_string(watchId), result ? 1.0f : 0.0f, target};
                    if (auto* source = SKSE::GetModCallbackEventSource()) source->SendEvent(&modEvent);
                },
                options);

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

        // Re-checks a watch on the next frame (its mod event fires if the result changed)
        void RefreshWatch(StaticFn*, std::int32_t watchId) {
            const auto id = static_cast<WatchId>(watchId);
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([id]() { Watcher::Get().Refresh(id); });
            }
        }

        void SetPollInterval(StaticFn*, float seconds) {
            if (seconds > 0.0f) {
                Watcher::Get().SetPollInterval(std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0f)));
            }
        }

        float GetPollInterval(StaticFn*) {
            return static_cast<float>(Watcher::Get().GetPollInterval().count()) / 1000.0f;
        }

        RE::BSFixedString GetCatalog(StaticFn*) {
            return Registry::Get().GetCatalog().dump();
        }

        // ── Important people (docs/people.md) ──
        std::vector<std::string> GroupList(const RE::BSFixedString& groups) {
            if (groups.empty()) return {};
            return ReadStringList(nlohmann::json{{"groups", groups.c_str()}}, "groups");
        }

        // The first matching group, read on the main thread
        template <class R, class Make>
        Latent PeopleOnMainThread(InternalVM* vm, RE::VMStackID stackId, RE::Actor* actor, RE::BSFixedString groups,
                                  Make make) {
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) return Latent::kFailed;
            const RE::ActorHandle handle = actor ? actor->GetHandle() : RE::ActorHandle{};
            tasks->AddTask([vm, stackId, handle, list = GroupList(groups), make]() {
                auto target = handle.get();
                const auto group = target ? People::MatchGroup(target.get(), list) : std::string{};
                vm->ReturnLatentResult<R>(stackId, make(group));
            });
            return Latent::kStarted;
        }

        Latent IsImportantPerson(InternalVM* vm, RE::VMStackID stackId, StaticFn*, RE::Actor* actor,
                                 RE::BSFixedString groups) {
            return PeopleOnMainThread<bool>(vm, stackId, actor, groups, [](const std::string& g) { return !g.empty(); });
        }

        Latent GetImportantGroup(InternalVM* vm, RE::VMStackID stackId, StaticFn*, RE::Actor* actor,
                                 RE::BSFixedString groups) {
            return PeopleOnMainThread<RE::BSFixedString>(vm, stackId, actor, groups,
                                                         [](const std::string& g) { return RE::BSFixedString(g); });
        }

        std::string GroupOrCustom(const RE::BSFixedString& group) {
            return group.empty() ? std::string(People::kUserGroup) : std::string(group.c_str());
        }

        bool AddImportantPerson(StaticFn*, RE::TESForm* form, RE::BSFixedString group) {
            const auto member = People::MemberString(form);
            return !member.empty() && People::AddMember(GroupOrCustom(group), member, "papyrus");
        }

        bool RemoveImportantPerson(StaticFn*, RE::TESForm* form, RE::BSFixedString group) {
            const auto member = People::MemberString(form);
            return !member.empty() && People::RemoveMember(GroupOrCustom(group), member);
        }

        bool RememberImportantPerson(StaticFn*, RE::TESForm* form, RE::BSFixedString group) {
            const auto member = People::MemberString(form);
            std::string error;
            if (!member.empty() && People::AddUserMember(GroupOrCustom(group), member, &error)) return true;
            if (!error.empty()) SKSE::log::info("ConditionEngine.RememberImportantPerson: {}", error);
            return false;
        }

        bool ForgetImportantPerson(StaticFn*, RE::TESForm* form, RE::BSFixedString group) {
            const auto member = People::MemberString(form);
            std::string error;
            if (!member.empty() && People::RemoveUserMember(GroupOrCustom(group), member, &error)) return true;
            if (!error.empty()) SKSE::log::info("ConditionEngine.ForgetImportantPerson: {}", error);
            return false;
        }

        RE::BSFixedString GetPeopleGroups(StaticFn*) {
            nlohmann::json list = nlohmann::json::array();
            for (const auto& g : People::ListGroups()) list.push_back(g.ToJson());
            return list.dump();
        }

        bool RegisterFunctions(VM* vm) {
            RegisterLatent<bool>(vm, "Evaluate", Evaluate);
            RegisterLatent<bool>(vm, "EvaluateNamed", EvaluateNamed);
            vm->RegisterFunction("RegisterNamed", kScriptName, RegisterNamed);
            vm->RegisterFunction("UnregisterNamed", kScriptName, UnregisterNamed);
            vm->RegisterFunction("Validate", kScriptName, Validate, true);
            vm->RegisterFunction("Describe", kScriptName, Describe, true);
            vm->RegisterFunction("ToJson", kScriptName, ToJson, true);
            vm->RegisterFunction("Normalize", kScriptName, Normalize, true);
            vm->RegisterFunction("Watch", kScriptName, Watch);
            vm->RegisterFunction("Unwatch", kScriptName, Unwatch);
            vm->RegisterFunction("RefreshWatch", kScriptName, RefreshWatch);
            vm->RegisterFunction("GetCatalog", kScriptName, GetCatalog, true);
            vm->RegisterFunction("SetPollInterval", kScriptName, SetPollInterval);
            RegisterLatent<float>(vm, "GetValue", GetValue);
            RegisterLatent<RE::BSFixedString>(vm, "GetValueSource", GetValueSource);
            RegisterLatent<RE::BSFixedString>(vm, "Explain", Explain);
            vm->RegisterFunction("SetValue", kScriptName, SetValue);
            vm->RegisterFunction("SetGlobalValue", kScriptName, SetGlobalValue);
            vm->RegisterFunction("ClearValue", kScriptName, ClearValue);
            vm->RegisterFunction("ClearGlobalValue", kScriptName, ClearGlobalValue);
            vm->RegisterFunction("RegisterNamedValue", kScriptName, RegisterNamedValue);
            vm->RegisterFunction("WatchValue", kScriptName, WatchValue);
            vm->RegisterFunction("GetPollInterval", kScriptName, GetPollInterval);
            RegisterLatent<bool>(vm, "IsImportantPerson", IsImportantPerson);
            RegisterLatent<RE::BSFixedString>(vm, "GetImportantGroup", GetImportantGroup);
            vm->RegisterFunction("AddImportantPerson", kScriptName, AddImportantPerson);
            vm->RegisterFunction("RemoveImportantPerson", kScriptName, RemoveImportantPerson);
            vm->RegisterFunction("RememberImportantPerson", kScriptName, RememberImportantPerson);
            vm->RegisterFunction("ForgetImportantPerson", kScriptName, ForgetImportantPerson);
            vm->RegisterFunction("GetPeopleGroups", kScriptName, GetPeopleGroups, true);
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
