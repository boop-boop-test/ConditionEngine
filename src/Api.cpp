/// @file Api.cpp
/// @brief extern "C" API exported by ConditionEngine.dll (see ConditionEngineAPI.h).

#include "ConditionEngine/ConditionEngineAPI.h"
#include "ConditionEngine/AllConditions.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Expression.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Watcher.h"

#include <mutex>
#include <sstream>

namespace {
    using namespace ConditionEngine;
    using json = nlohmann::json;

    std::mutex g_handleMutex;
    std::unordered_map<CE_Handle, std::shared_ptr<const Condition>> g_handles;
    CE_Handle g_nextHandle = 1;

    CE_Handle Store(std::shared_ptr<const Condition> condition) {
        std::lock_guard lock(g_handleMutex);
        CE_Handle handle = g_nextHandle++;
        if (g_nextHandle == 0) g_nextHandle = 1;
        g_handles.emplace(handle, std::move(condition));
        return handle;
    }

    std::shared_ptr<const Condition> Lookup(CE_Handle handle) {
        std::lock_guard lock(g_handleMutex);
        auto it = g_handles.find(handle);
        return it != g_handles.end() ? it->second : nullptr;
    }

    RE::Actor* ActorOrPlayer(void* actor) {
        return actor ? static_cast<RE::Actor*>(actor) : RE::PlayerCharacter::GetSingleton();
    }

    // Writes `text` into a caller buffer; returns the size needed including the null.
    uint32_t WriteString(const std::string& text, char* buf, uint32_t bufSize) {
        const auto needed = static_cast<uint32_t>(text.size() + 1);
        if (buf && bufSize > 0) {
            const auto count = std::min<std::size_t>(text.size(), bufSize - 1);
            std::memcpy(buf, text.data(), count);
            buf[count] = '\0';
        }
        return needed;
    }

    std::vector<std::string> SplitCsv(const char* csv) {
        std::vector<std::string> result;
        if (!csv) return result;
        std::istringstream ss(csv);
        std::string token;
        while (std::getline(ss, token, ',')) {
            auto start = token.find_first_not_of(" \t");
            auto end = token.find_last_not_of(" \t");
            if (start != std::string::npos) result.push_back(token.substr(start, end - start + 1));
        }
        return result;
    }

    bool Evaluate(const Condition& condition, RE::Actor* actor, void* userData = nullptr) {
        EvaluationContext context(actor);
        context.userData = userData;
        return condition.Evaluate(context);
    }
}

extern "C" {

CONDITIONENGINE_API uint32_t ConditionEngine_GetAPIVersion(void) {
    return CONDITIONENGINE_API_VERSION;
}

// ── Compile / evaluate ───────────────────────────────────────────────────────

CONDITIONENGINE_API CE_Handle ConditionEngine_Compile(const char* source, char* errorBuf, uint32_t errorBufSize) {
    if (!source) {
        WriteString("source is null", errorBuf, errorBufSize);
        return 0;
    }
    std::string error;
    auto condition = Expression::CompileAny(source, &error);
    if (!condition) {
        WriteString(error, errorBuf, errorBufSize);
        return 0;
    }
    WriteString({}, errorBuf, errorBufSize);
    return Store(std::shared_ptr<const Condition>(std::move(condition)));
}

CONDITIONENGINE_API void ConditionEngine_Release(CE_Handle handle) {
    std::lock_guard lock(g_handleMutex);
    g_handles.erase(handle);
}

CONDITIONENGINE_API int32_t ConditionEngine_Evaluate(CE_Handle handle, void* actor) {
    auto condition = Lookup(handle);
    if (!condition) return -1;
    return Evaluate(*condition, ActorOrPlayer(actor)) ? 1 : 0;
}

CONDITIONENGINE_API int32_t ConditionEngine_EvaluateFormID(CE_Handle handle, uint32_t actorFormId) {
    auto* actor = actorFormId ? RE::TESForm::LookupByID<RE::Actor>(actorFormId) : nullptr;
    if (actorFormId && !actor) return -1;
    return ConditionEngine_Evaluate(handle, actor);
}

CONDITIONENGINE_API uint32_t ConditionEngine_GetDependencies(CE_Handle handle) {
    auto condition = Lookup(handle);
    return condition ? static_cast<uint32_t>(condition->GetEffectiveEventDependencies()) : 0;
}

CONDITIONENGINE_API uint32_t ConditionEngine_ToJson(CE_Handle handle, char* buf, uint32_t bufSize) {
    auto condition = Lookup(handle);
    return WriteString(condition ? condition->ToJsonComplete().dump() : std::string{}, buf, bufSize);
}

CONDITIONENGINE_API uint32_t ConditionEngine_ToExpression(CE_Handle handle, char* buf, uint32_t bufSize) {
    auto condition = Lookup(handle);
    return WriteString(condition ? Expression::ToExpression(*condition) : std::string{}, buf, bufSize);
}

CONDITIONENGINE_API uint32_t ConditionEngine_Describe(CE_Handle handle, char* buf, uint32_t bufSize) {
    auto condition = Lookup(handle);
    return WriteString(condition ? condition->GetDescription() : std::string{}, buf, bufSize);
}

CONDITIONENGINE_API uint32_t ConditionEngine_Validate(CE_Handle handle, char* buf, uint32_t bufSize) {
    auto condition = Lookup(handle);
    if (!condition) return WriteString("invalid handle", buf, bufSize);
    std::string joined;
    for (const auto& error : condition->Validate()) {
        if (!joined.empty()) joined += '\n';
        joined += error;
    }
    return WriteString(joined, buf, bufSize);
}

// ── Named conditions ─────────────────────────────────────────────────────────

CONDITIONENGINE_API CE_Result ConditionEngine_RegisterNamed(const char* id, const char* source, const char* owner,
                                                            char* errorBuf, uint32_t errorBufSize) {
    if (!id || !*id || !source) return CE_ERR_INVALID_ARG;
    std::string error;
    auto condition = Expression::CompileAny(source, &error);
    if (!condition) {
        WriteString(error, errorBuf, errorBufSize);
        return CE_ERR_PARSE;
    }
    Registry::Get().RegisterNamed(id, std::shared_ptr<const Condition>(std::move(condition)), owner ? owner : "",
                                  source);
    return CE_OK;
}

CONDITIONENGINE_API CE_Result ConditionEngine_RegisterNamedCallback(const char* id, CE_NamedEvalFn fn, void* userData,
                                                                    uint32_t dependencies, const char* owner) {
    if (!id || !*id || !fn) return CE_ERR_INVALID_ARG;
    const std::string name = id;
    auto condition = std::make_shared<CallbackCondition>(
        name,
        [fn, userData](const EvaluationContext& context) { return fn(context.GetActor(), userData) != 0; },
        static_cast<EventDependency>(dependencies), json{{"type", "Rule"}, {"ruleName", name}});
    Registry::Get().RegisterNamed(name, std::move(condition), owner ? owner : "");
    return CE_OK;
}

CONDITIONENGINE_API CE_Result ConditionEngine_UnregisterNamed(const char* id) {
    if (!id) return CE_ERR_INVALID_ARG;
    return Registry::Get().UnregisterNamed(id) ? CE_OK : CE_ERR_NOT_FOUND;
}

CONDITIONENGINE_API uint32_t ConditionEngine_UnregisterOwner(const char* owner) {
    return owner ? static_cast<uint32_t>(Registry::Get().UnregisterSource(owner)) : 0;
}

CONDITIONENGINE_API int32_t ConditionEngine_LookupNamed(const char* key, uint32_t* outDependencies) {
    if (!key) return 0;
    auto condition = Registry::Get().FindNamed(key);
    if (!condition) return 0;
    if (outDependencies) *outDependencies = static_cast<uint32_t>(condition->GetEffectiveEventDependencies());
    return 1;
}

CONDITIONENGINE_API int32_t ConditionEngine_EvaluateNamed(const char* key, void* actor) {
    if (!key) return -1;
    auto condition = Registry::Get().FindNamed(key);
    if (!condition) return -1;
    return Evaluate(*condition, ActorOrPlayer(actor)) ? 1 : 0;
}

CONDITIONENGINE_API CE_Result ConditionEngine_AddNamedResolver(CE_NamedLookupFn lookup, CE_NamedKeyEvalFn eval,
                                                               void* userData) {
    if (!lookup || !eval) return CE_ERR_INVALID_ARG;
    Registry::Get().AddNamedResolver([lookup, eval, userData](const std::string& key) -> std::shared_ptr<const Condition> {
        uint32_t deps = 0;
        if (lookup(key.c_str(), &deps, userData) == 0) return nullptr;
        return std::make_shared<CallbackCondition>(
            key,
            [eval, userData, key](const EvaluationContext& context) {
                return eval(key.c_str(), context.GetActor(), userData) != 0;
            },
            static_cast<EventDependency>(deps), json{{"type", "Rule"}, {"ruleName", key}});
    });
    return CE_OK;
}

// ── Custom condition types ───────────────────────────────────────────────────

CONDITIONENGINE_API CE_Result ConditionEngine_RegisterType(const char* name, CE_CustomEvalFn fn, void* userData,
                                                           uint32_t dependencies, const char* positionalCsv,
                                                           const char* compareField) {
    if (!name || !*name || !fn) return CE_ERR_INVALID_ARG;

    TypeInfo info;
    info.name = name;
    info.positional = SplitCsv(positionalCsv);
    info.compareField = compareField ? compareField : "";
    const auto deps = static_cast<EventDependency>(dependencies);
    const std::string typeName = name;
    info.factory = [fn, userData, deps, typeName](const json& j) -> std::unique_ptr<Condition> {
        auto params = std::make_shared<const std::string>(j.dump());
        return std::make_unique<CallbackCondition>(
            typeName,
            [fn, userData, params](const EvaluationContext& context) {
                return fn(context.GetActor(), params->c_str(), userData) != 0;
            },
            deps, j);
    };
    return Registry::Get().RegisterType(std::move(info)) ? CE_OK : CE_ERR_ALREADY_EXISTS;
}

// ── Watches ──────────────────────────────────────────────────────────────────

CONDITIONENGINE_API CE_WatchHandle ConditionEngine_Watch(CE_Handle handle, void* actor, CE_WatchFn fn, void* userData,
                                                         int32_t fireImmediately) {
    auto condition = Lookup(handle);
    if (!condition || !fn) return 0;
    return Watcher::Get().Watch(
        condition, static_cast<RE::Actor*>(actor),
        [fn, userData](WatchId id, RE::Actor* target, bool result) { fn(id, target, result ? 1 : 0, userData); },
        fireImmediately != 0);
}

CONDITIONENGINE_API void ConditionEngine_Unwatch(CE_WatchHandle watch) {
    Watcher::Get().Unwatch(watch);
}

CONDITIONENGINE_API void ConditionEngine_NotifyEvent(uint32_t dependencies, void* actor) {
    const auto events = static_cast<EventDependency>(dependencies);
    auto* target = static_cast<RE::Actor*>(actor);
    // May be called from any thread; evaluation must happen on the main thread
    const RE::FormID key = target ? target->GetFormID() : 0;
    if (auto* tasks = SKSE::GetTaskInterface()) {
        tasks->AddTask([events, key]() {
            RE::Actor* resolved = key ? RE::TESForm::LookupByID<RE::Actor>(key) : nullptr;
            if (key && !resolved) return;
            Watcher::Get().NotifyEvent(events, resolved);
        });
    }
}

// ── Providers ────────────────────────────────────────────────────────────────

CONDITIONENGINE_API void ConditionEngine_SetLoveSceneProvider(CE_ActorFlagFn fn, void* userData) {
    if (!fn) {
        Registry::Get().SetLoveSceneProvider(nullptr);
        return;
    }
    Registry::Get().SetLoveSceneProvider([fn, userData](RE::Actor* actor) { return fn(actor, userData) != 0; });
}

} // extern "C"
