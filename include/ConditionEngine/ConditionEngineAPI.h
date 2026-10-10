#pragma once

/// @file ConditionEngineAPI.h
/// @brief Stable C API exported by ConditionEngine.dll.
///
/// Plain C so any SKSE plugin can use it regardless of compiler, CRT or
/// nlohmann/json version. Load it at runtime:
///
///     HMODULE dll = GetModuleHandleW(L"ConditionEngine.dll");   // after kPostLoad
///     auto getVersion = (uint32_t(*)())GetProcAddress(dll, "ConditionEngine_GetAPIVersion");
///     if (getVersion && getVersion() >= 1) { ... GetProcAddress the rest ... }
///
/// or link ConditionEngine.lib and include this header directly.
///
/// Conventions
///   - Handles are uint32, 0 = invalid.
///   - `actor` parameters are RE::Actor* passed as void*; null means the player.
///   - String outputs use caller buffers: functions return the size needed
///     including the terminating null, and write as much as fits.
///   - Every function is safe to call from any thread, but Evaluate and watch
///     callbacks read game state and should run on the main thread.
///
/// Version history (append-only):
///   1 - Compile/Evaluate/Describe, named conditions, custom types, watches, events

#include <stdint.h>
#include "ConditionEngine/Export.h"

#define CONDITIONENGINE_API_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t CE_Handle;       ///< Compiled condition
typedef uint32_t CE_WatchHandle;  ///< Active watch

typedef enum CE_Result {
    CE_OK = 0,
    CE_ERR_PARSE = 1,           ///< Source text/JSON could not be compiled
    CE_ERR_INVALID_HANDLE = 2,
    CE_ERR_INVALID_ARG = 3,
    CE_ERR_ALREADY_EXISTS = 4,  ///< Type name already registered
    CE_ERR_NOT_FOUND = 5
} CE_Result;

/// Event dependency bits (match ConditionEngine::EventDependency)
enum {
    CE_DEP_TIME = 1u << 0,
    CE_DEP_DATE = 1u << 1,
    CE_DEP_WEATHER = 1u << 2,
    CE_DEP_LOCATION = 1u << 3,
    CE_DEP_COMBAT = 1u << 4,
    CE_DEP_EQUIPMENT = 1u << 5,
    CE_DEP_FACTION = 1u << 6,
    CE_DEP_QUEST = 1u << 7,
    CE_DEP_ACTOR_VALUE = 1u << 8,
    CE_DEP_GLOBAL_VARIABLE = 1u << 9,
    CE_DEP_SCRIPT = 1u << 10,
    CE_DEP_TEMPERATURE = 1u << 11,
    CE_DEP_SWIMMING = 1u << 12,
    CE_DEP_SLEEPING = 1u << 13,
    CE_DEP_MOUNTING = 1u << 14,
    CE_DEP_IN_WATER = 1u << 15,
    CE_DEP_LOVE_SCENE = 1u << 16,
    CE_DEP_POLLED = 1u << 17,
    CE_DEP_HOST = 1u << 18  ///< Host-signalled state (fire via NotifyEvent)
};

/// Evaluates a custom condition type. `paramsJson` is the condition's JSON object.
/// Return non-zero for true.
typedef int32_t (*CE_CustomEvalFn)(void* actor, const char* paramsJson, void* userData);

/// Evaluates a callback-backed named condition. Return non-zero for true.
typedef int32_t (*CE_NamedEvalFn)(void* actor, void* userData);

/// Host resolver for named conditions it owns (e.g. DressCode rules). Lookup returns
/// non-zero when `key` is known and writes its dependency bits; eval returns the result.
typedef int32_t (*CE_NamedLookupFn)(const char* key, uint32_t* outDependencies, void* userData);
typedef int32_t (*CE_NamedKeyEvalFn)(const char* key, void* actor, void* userData);

/// Called on the main thread when a watched condition's result changes.
typedef void (*CE_WatchFn)(CE_WatchHandle watch, void* actor, int32_t result, void* userData);

/// Supplies a love-scene flag (OStim/SexLab integrations). Return non-zero for true.
typedef int32_t (*CE_ActorFlagFn)(void* actor, void* userData);

// ── Version ──────────────────────────────────────────────────────────────────

CONDITIONENGINE_API uint32_t ConditionEngine_GetAPIVersion(void);

// ── Compile / evaluate ───────────────────────────────────────────────────────

/// Compiles an expression (`IsInterior && Weather(Rainy)`) or a JSON condition
/// tree (source starting with '{'). Returns 0 on failure and writes the error.
CONDITIONENGINE_API CE_Handle ConditionEngine_Compile(const char* source, char* errorBuf, uint32_t errorBufSize);
CONDITIONENGINE_API void ConditionEngine_Release(CE_Handle handle);

/// Returns 1 (true), 0 (false) or -1 (invalid handle).
CONDITIONENGINE_API int32_t ConditionEngine_Evaluate(CE_Handle handle, void* actor);
CONDITIONENGINE_API int32_t ConditionEngine_EvaluateFormID(CE_Handle handle, uint32_t actorFormId);

/// Event dependency bits (CE_DEP_*) that can change the result.
CONDITIONENGINE_API uint32_t ConditionEngine_GetDependencies(CE_Handle handle);

CONDITIONENGINE_API uint32_t ConditionEngine_ToJson(CE_Handle handle, char* buf, uint32_t bufSize);
CONDITIONENGINE_API uint32_t ConditionEngine_ToExpression(CE_Handle handle, char* buf, uint32_t bufSize);
CONDITIONENGINE_API uint32_t ConditionEngine_Describe(CE_Handle handle, char* buf, uint32_t bufSize);
/// Newline-separated configuration errors; returns 1 (empty string) when valid.
CONDITIONENGINE_API uint32_t ConditionEngine_Validate(CE_Handle handle, char* buf, uint32_t bufSize);

// ── Named conditions ─────────────────────────────────────────────────────────
// Referenced from expressions as @Name or Rule("Name"). `owner` is your plugin name.

CONDITIONENGINE_API CE_Result ConditionEngine_RegisterNamed(const char* id, const char* source, const char* owner,
                                                            char* errorBuf, uint32_t errorBufSize);
CONDITIONENGINE_API CE_Result ConditionEngine_RegisterNamedCallback(const char* id, CE_NamedEvalFn fn, void* userData,
                                                                    uint32_t dependencies, const char* owner);
CONDITIONENGINE_API CE_Result ConditionEngine_UnregisterNamed(const char* id);
/// Removes every named condition registered with this owner.
CONDITIONENGINE_API uint32_t ConditionEngine_UnregisterOwner(const char* owner);
/// Looks up a named condition (engine-owned or resolver-provided). Returns 1 when
/// found and writes its dependency bits.
CONDITIONENGINE_API int32_t ConditionEngine_LookupNamed(const char* key, uint32_t* outDependencies);
/// Evaluates a named condition: 1 (true), 0 (false) or -1 (not found).
CONDITIONENGINE_API int32_t ConditionEngine_EvaluateNamed(const char* key, void* actor);
/// Adds a resolver consulted for keys the engine does not own. Resolvers are
/// queried at evaluation time, so the host never needs to re-publish changes.
CONDITIONENGINE_API CE_Result ConditionEngine_AddNamedResolver(CE_NamedLookupFn lookup, CE_NamedKeyEvalFn eval,
                                                               void* userData);

// ── Custom condition types ───────────────────────────────────────────────────

/// Registers a condition type usable from JSON ({"type": name, ...}) and
/// expressions (name(arg, key=value)). `positionalCsv` lists the JSON fields
/// positional expression arguments fill ("faction,rank"); `compareField` is the
/// field set by a trailing comparison (`MyType(x) >= 3`), or null.
CONDITIONENGINE_API CE_Result ConditionEngine_RegisterType(const char* name, CE_CustomEvalFn fn, void* userData,
                                                           uint32_t dependencies, const char* positionalCsv,
                                                           const char* compareField);

// ── Watches ──────────────────────────────────────────────────────────────────

/// Re-evaluates when one of the condition's dependencies fires and calls `fn`
/// when the result changes. The current result is evaluated immediately and
/// passed to `fn` when `fireImmediately` is non-zero.
CONDITIONENGINE_API CE_WatchHandle ConditionEngine_Watch(CE_Handle handle, void* actor, CE_WatchFn fn, void* userData,
                                                         int32_t fireImmediately);
CONDITIONENGINE_API void ConditionEngine_Unwatch(CE_WatchHandle watch);

/// Signals that host-owned state changed. `actor` null = all watched actors.
CONDITIONENGINE_API void ConditionEngine_NotifyEvent(uint32_t dependencies, void* actor);

// ── Providers ────────────────────────────────────────────────────────────────

CONDITIONENGINE_API void ConditionEngine_SetLoveSceneProvider(CE_ActorFlagFn fn, void* userData);

#ifdef __cplusplus
}
#endif
