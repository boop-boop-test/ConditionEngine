#pragma once

/// @file ConditionEngineAPI.h
/// @brief C API exported by ConditionEngine.dll.
///
/// Plain C so any SKSE plugin can use it regardless of compiler, CRT or
/// nlohmann/json version. Load it at runtime:
///
///     HMODULE dll = GetModuleHandleW(L"ConditionEngine.dll");   // after kPostLoad
///     auto getVersion = (uint32_t(*)())GetProcAddress(dll, "ConditionEngine_GetAPIVersion");
///     if (getVersion && getVersion() == CONDITIONENGINE_API_VERSION) { ... GetProcAddress the rest ... }
///
/// or link ConditionEngine.lib and include this header directly.
///
/// Version 1, prerelease: the API may still change until the first release.
///
/// Conventions
///   - Handles are uint32, 0 = invalid.
///   - `actor` parameters are RE::Actor* passed as void*; null means the player.
///   - String outputs use caller buffers: functions return the size needed
///     including the terminating null, and write as much as fits. A null buffer
///     (size 0) just returns the size.
///   - Structs start with `structSize`: set it to sizeof(the struct).
///   - Every function is safe to call from any thread, but evaluation, value reads
///     and watch callbacks read game state and run on the main thread.

#include <stdint.h>
#include "ConditionEngine/Export.h"

#define CONDITIONENGINE_API_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t CE_Handle;       ///< Compiled condition
typedef uint32_t CE_ValueHandle;  ///< Compiled value expression
typedef uint32_t CE_WatchHandle;  ///< Active watch (condition, value or list)

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
    CE_DEP_HOST = 1u << 18,          ///< Host-signalled state (fire via NotifyEvent)
    CE_DEP_INVENTORY = 1u << 19,     ///< Item counts
    CE_DEP_MAGIC_EFFECT = 1u << 20,  ///< Active effects, spells, perks
    CE_DEP_ACTOR_STATE = 1u << 21,   ///< Sneaking, sitting, dialogue, bleedout, weapon drawn, follower
    CE_DEP_AVAILABILITY = 1u << 22,  ///< A required plugin or host integration changed readiness
    CE_DEP_VALUE = 1u << 23          ///< A pushed value was set, a named value redefined, an async sample arrived
};

#define CE_POLL_DEFAULT 0u           ///< Use the global poll interval
#define CE_POLL_NEVER   0xFFFFFFFFu  ///< Only re-check on change detection / events

/// Evaluates a custom condition type. `paramsJson` is the condition's JSON object.
/// Return non-zero for true.
typedef int32_t (*CE_CustomEvalFn)(void* actor, const char* paramsJson, void* userData);

/// Evaluates a callback-backed named condition. Return non-zero for true.
typedef int32_t (*CE_NamedEvalFn)(void* actor, void* userData);

/// Host resolver for named conditions it owns (e.g. DressCode rules). Lookup returns
/// non-zero when `key` is known and writes its dependency bits; eval returns the result.
typedef int32_t (*CE_NamedLookupFn)(const char* key, uint32_t* outDependencies, void* userData);
typedef int32_t (*CE_NamedKeyEvalFn)(const char* key, void* actor, void* userData);

/// Supplies a love-scene flag (OStim/SexLab integrations). Return non-zero for true.
typedef int32_t (*CE_ActorFlagFn)(void* actor, void* userData);

CONDITIONENGINE_API uint32_t ConditionEngine_GetAPIVersion(void);

// ── Conditions ───────────────────────────────────────────────────────────────

/// Compiles an expression (`IsInterior && Weather(Rainy)`) or a JSON condition
/// tree (source starting with '{'). Returns 0 on failure and writes the error.
CONDITIONENGINE_API CE_Handle ConditionEngine_Compile(const char* source, char* errorBuf, uint32_t errorBufSize);
CONDITIONENGINE_API void ConditionEngine_Release(CE_Handle handle);

/// Main thread. Returns 1 (true), 0 (false or unavailable) or -1 (invalid handle).
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
// Referenced from expressions as @Name or Rule("Name").

/// `definition` is expression text, a condition tree (JSON with "type"), or
/// {"expr" | "condition", "label", "category", "icon", "description", "offer", "tags",
/// "owner"}. `owner` null = the calling module's file name.
CONDITIONENGINE_API CE_Result ConditionEngine_RegisterNamed(const char* id, const char* definition, const char* owner,
                                                            char* errorBuf, uint32_t errorBufSize);
CONDITIONENGINE_API CE_Result ConditionEngine_RegisterNamedCallback(const char* id, CE_NamedEvalFn fn, void* userData,
                                                                    uint32_t dependencies, const char* owner);
CONDITIONENGINE_API CE_Result ConditionEngine_UnregisterNamed(const char* id);
/// Removes every named condition and named value registered with this owner.
CONDITIONENGINE_API uint32_t ConditionEngine_UnregisterOwner(const char* owner);
/// Looks up a named condition (engine-owned or resolver-provided). Returns 1 when
/// found and writes its dependency bits.
CONDITIONENGINE_API int32_t ConditionEngine_LookupNamed(const char* key, uint32_t* outDependencies);
/// Main thread. Evaluates a named condition: 1 (true), 0 (false) or -1 (not found).
CONDITIONENGINE_API int32_t ConditionEngine_EvaluateNamed(const char* key, void* actor);
/// Adds a resolver consulted for keys the engine does not own. Resolvers are
/// queried at evaluation time, so the host never needs to re-publish changes.
CONDITIONENGINE_API CE_Result ConditionEngine_AddNamedResolver(CE_NamedLookupFn lookup, CE_NamedKeyEvalFn eval,
                                                               void* userData);

// ── Custom condition types ───────────────────────────────────────────────────

/// Registers a condition type with its schema, so editors (DressCode's UI, other mods)
/// can offer it without knowing it. `schemaJson`:
///   {"name": "MyMod_IsCursed", "label": "Is Cursed", "category": "actor",
///    "description": "...", "source": "MyMod", "aliases": [], "compareField": "level",
///    "fields": [{"key": "level", "label": "Curse Level", "type": "int", "default": 1}],
///    "positional": ["level"], "requiresPlugins": ["MyMod.esp"], "offer": "everywhere",
///    "tags": [], "cost": 3}
/// "cost" is the evaluation cost class (0 constant .. 5 Papyrus call, default 3): And / Or
/// evaluate cheaper parts first.
/// Field types: int, float, string, bool, enum, enumList, form, formList, stringList,
/// intList, comparison, namedCondition, custom, value (see ConditionEngine/Schema.h).
/// A missing "source" uses the calling module's file name (and logs a warning).
/// Field keys "type" and "forcePolled" are reserved (they are the condition's own JSON keys),
/// and "operator" / "operation" only for a comparison field: such schemas are refused with
/// CE_ERR_INVALID_ARG, as are duplicate keys.
CONDITIONENGINE_API CE_Result ConditionEngine_RegisterType(const char* schemaJson, CE_CustomEvalFn fn, void* userData,
                                                           uint32_t dependencies, char* errorBuf,
                                                           uint32_t errorBufSize);

// ── Catalog ──────────────────────────────────────────────────────────────────
// Every type, named value and named condition with its fields, labels and metadata.
// Categories are topical and shared ("survival" means survival whoever registered it)
// and form a tree through "parent". Every item carries:
//   category      its category id (an id nobody registered is created on the fly, top
//                 level, labelled from the id - items are never dropped or regrouped)
//   source        origin id (built-ins: "ConditionEngine"), also the owner
//   sourceLabel   origin label for display, e.g. "Vampire hunger · SeverActions"
//   offer         "everywhere" | "owner" | "nowhere" (pickers show owner items only to
//                 their owner; nowhere = expression-only; metadata, never enforced)
//   tags          free tags plus automatic ones: condition, value, per-actor, global,
//                 pushed, async, requires-plugin
//   icon, description
// The root lists "categories" and "sources": [{id, label, count}]. The catalog is small:
// pickers fetch it once and filter by category, source, tags and text themselves, and
// show each item's origin (sourceLabel) next to its label.

/// `filterJson` (may be null): {"for": "<owner>", "include": ["types", "values",
/// "conditions", "categories"], "offered": true}. "for" adds that owner's owner-only items;
/// offered:false returns everything ("show all"). Null = everything.
CONDITIONENGINE_API uint32_t ConditionEngine_GetCatalog(const char* filterJson, char* buf, uint32_t bufSize);
/// JSON Schema (draft-07) validating condition JSON, generated from the catalog.
CONDITIONENGINE_API uint32_t ConditionEngine_GetJsonSchema(char* buf, uint32_t bufSize);

/// Registers a category: {"id", "label", "parent", "order", "icon", "description"}.
/// Registering an existing id is reuse (CE_OK): the first registrant keeps the label and
/// later ones only fill in a missing description, icon or parent. A parent cycle is
/// CE_ERR_INVALID_ARG. `owner` null = the calling module's file name.
CONDITIONENGINE_API CE_Result ConditionEngine_RegisterCategory(const char* categoryJson, const char* owner,
                                                               char* errorBuf, uint32_t errorBufSize);
/// Display label for an origin id, e.g. ("NoTalkJustHands", "No Talk Just Hands").
/// Unlabelled ids display as themselves.
CONDITIONENGINE_API void ConditionEngine_RegisterSource(const char* id, const char* label);

// ── Availability ─────────────────────────────────────────────────────────────
// Types declare requiresPlugins; form fields with portable 0x...|Plugin.esp keys require
// that plugin too. Types always remain registered so saved expressions still compile
// without their dependencies. Evaluation and watches treat an unavailable expression as
// false as a whole (even under NOT / OR).
// GetAvailability: {"state": "available" | "pending" | "unavailable", "available", "reason"}.
// SetTypeAvailability publishes host readiness: 0 = pending, 1 = available, 2 = unavailable.
// Subscribers run on the publishing thread, outside locks; marshal game / UI work to the
// main thread, and unsubscribe before unloading.
typedef void (*CE_AvailabilityChangedFn)(uint64_t revision, void* userData);
CONDITIONENGINE_API uint32_t ConditionEngine_GetAvailability(CE_Handle handle, char* buf, uint32_t bufSize);
CONDITIONENGINE_API CE_Result ConditionEngine_SetTypeAvailability(const char* type, int32_t state, const char* reason);
CONDITIONENGINE_API uint32_t ConditionEngine_SubscribeAvailability(CE_AvailabilityChangedFn fn, void* userData);
CONDITIONENGINE_API void ConditionEngine_UnsubscribeAvailability(uint32_t subscription);
CONDITIONENGINE_API uint64_t ConditionEngine_GetAvailabilityRevision(void);

// ── Condition watches ────────────────────────────────────────────────────────

typedef struct CE_WatchOptions {
    uint32_t structSize;
    int32_t fireImmediately;     ///< Non-zero: deliver the initial result too
    uint32_t pollIntervalMs;     ///< CE_POLL_DEFAULT, CE_POLL_NEVER or milliseconds
    uint32_t extraDependencies;  ///< CE_DEP_* bits that also trigger a re-check (e.g. CE_DEP_LOCATION)
} CE_WatchOptions;

/// Called on the main thread when a watched condition's result changes.
typedef void (*CE_WatchFn)(CE_WatchHandle watch, void* actor, int32_t result, void* userData);

/// Re-evaluates when one of the condition's dependencies fires, on its poll interval,
/// and calls `fn` when the result changes. Null options = defaults.
CONDITIONENGINE_API CE_WatchHandle ConditionEngine_Watch(CE_Handle handle, void* actor, CE_WatchFn fn, void* userData,
                                                         const CE_WatchOptions* options);
/// Removes any watch (condition, value or list).
CONDITIONENGINE_API void ConditionEngine_Unwatch(CE_WatchHandle watch);
/// Main thread. Re-evaluates a watch now (callback fires on change). Returns 1/0, or -1
/// when the watch is unknown or the world is not loaded.
CONDITIONENGINE_API int32_t ConditionEngine_Refresh(CE_WatchHandle watch);
// ── List watches ─────────────────────────────────────────────────────────────
// An ordered list of conditions for one actor, in priority order: the winner is the first
// true one (its index), -1 when none is. After a change only what can move the result is
// evaluated: touched rules above the winner, then the winner, and the rules below it only
// when it turned false. A pending rule above the winner holds it (no flipping while
// something loads); an unavailable rule counts as not matching. A change to one named
// value wakes only the rules that read it, for that actor.

/// Called on the main thread when the winner changes.
typedef void (*CE_ListWatchFn)(CE_WatchHandle watch, void* actor, int32_t winner, void* userData);

/// The condition handles are copied (release yours whenever). `fn` may be null: read the
/// winner with ReadListWatch. options->pollIntervalMs is the full re-evaluation interval
/// (for polled rules such as Game(...)). Unwatch with ConditionEngine_Unwatch.
CONDITIONENGINE_API CE_WatchHandle ConditionEngine_WatchList(const CE_Handle* conditions, uint32_t count, void* actor,
                                                             CE_ListWatchFn fn, void* userData,
                                                             const CE_WatchOptions* options);
/// ANY thread, non-blocking: the winner index, -1 none, -2 unknown watch.
CONDITIONENGINE_API int32_t ConditionEngine_ReadListWatch(CE_WatchHandle watch);
/// Main thread: re-evaluates every rule now. The winner, -1 none, -2 unknown / world not loaded.
CONDITIONENGINE_API int32_t ConditionEngine_RefreshListWatch(CE_WatchHandle watch);
/// Any thread, for debug panels: {"winner", "rules": [{"state": "true" | "false" | "pending" |
/// "unavailable" | "unread", "reason"}]}. Empty string for an unknown watch.
CONDITIONENGINE_API uint32_t ConditionEngine_ReadListWatchState(CE_WatchHandle watch, char* buf, uint32_t bufSize);

/// Default full-poll interval for watches (initially ConditionEngine.ini, 10 s).
CONDITIONENGINE_API void ConditionEngine_SetPollInterval(uint32_t milliseconds);
CONDITIONENGINE_API uint32_t ConditionEngine_GetPollInterval(void);

/// Signals that host-owned state changed. `actor` null = all watched actors. Any thread;
/// notifications before the main thread drains them merge.
CONDITIONENGINE_API void ConditionEngine_NotifyEvent(uint32_t dependencies, void* actor);

CONDITIONENGINE_API void ConditionEngine_SetLoveSceneProvider(CE_ActorFlagFn fn, void* userData);

// ── Values ───────────────────────────────────────────────────────────────────
// A value is the number a numeric condition type compares, read on its own, plus
// arithmetic: "ActorValue(Health, usePercentage=true)", "$hunger * (1 - $light)",
// "clamp($x, 0, 1)", min/max/abs/lerp; a condition used as a value reads 0 / 1.
// Named values ($name) come from preset files ("values": {...}), RegisterNamedValue or
// SetValue; redefinitions reach existing handles and watches. An unknown name reads
// CE_VALUE_UNAVAILABLE. Conditions compare values directly: "$vampire_hunger > 80".

typedef enum CE_ValueStatus {
    CE_VALUE_OK = 0,
    CE_VALUE_PENDING = 1,      ///< No sample yet (async) or a required plugin / host is not ready
    CE_VALUE_UNAVAILABLE = 2,  ///< Missing plugin/form, actor not loaded, enabledWhen false, no value pushed
    CE_VALUE_ERROR = 3         ///< Misconfigured or failed to evaluate
} CE_ValueStatus;

typedef struct CE_ValueSample {
    uint32_t structSize;
    int32_t status;                 ///< CE_ValueStatus
    double value;                   ///< Last good value, or the named value's fallback when not OK
    uint32_t ageMs;                 ///< Since the value was read, computed when you read; 0xFFFFFFFF = never
    uint64_t availabilityRevision;  ///< ConditionEngine_GetAvailabilityRevision() when sampled
} CE_ValueSample;

/// Compiles value text, a JSON value node (leading '{') or a bare named-value name
/// ("vampire_hunger" = "$vampire_hunger", defined now or later). 0 on failure.
CONDITIONENGINE_API CE_ValueHandle ConditionEngine_CompileValue(const char* source, char* errorBuf, uint32_t errorBufSize);
CONDITIONENGINE_API void ConditionEngine_ReleaseValue(CE_ValueHandle value);
/// Main thread. Reads now (async nodes return their latest sample) and writes why it is not
/// OK into `reasonBuf` (may be null). When OK the buffer is empty, or for a combined value
/// names the provider that answered ("from severactions_hunger"). CE_ERR_INVALID_HANDLE / CE_OK.
CONDITIONENGINE_API CE_Result ConditionEngine_EvaluateValue(CE_ValueHandle value, void* actor, CE_ValueSample* out,
                                                            char* reasonBuf, uint32_t reasonBufSize);
CONDITIONENGINE_API uint32_t ConditionEngine_GetValueDependencies(CE_ValueHandle value);
/// Same JSON as ConditionEngine_GetAvailability
CONDITIONENGINE_API uint32_t ConditionEngine_GetValueAvailability(CE_ValueHandle value, char* buf, uint32_t bufSize);
CONDITIONENGINE_API uint32_t ConditionEngine_ValueToExpression(CE_ValueHandle value, char* buf, uint32_t bufSize);
CONDITIONENGINE_API uint32_t ConditionEngine_ValueToJson(CE_ValueHandle value, char* buf, uint32_t bufSize);
/// Newline-separated configuration errors; returns 1 (empty string) when valid.
CONDITIONENGINE_API uint32_t ConditionEngine_ValidateValue(CE_ValueHandle value, char* buf, uint32_t bufSize);

/// Registers (or replaces) $name. `definition` is value text, or a JSON object:
///   {"expr": "...", "label", "unit", "range": [min, max], "integer", "fallback",
///    "enabledWhen": "<condition>", "scope": "auto" | "actor" | "global",
///    "pushed": true | {"default", "timeoutSeconds"}, "graceSeconds",
///    "requiresPlugins": [...], "requiresDlls": ["SomeNative.dll"], "refreshOn": ["ModEvent"],
///    "provides": ["hunger"], "priority": 50, "combined": true,
///    "category", "icon", "description", "offer", "tags", "owner"}
/// Providers: a value with "provides" is a source for those combined values. Per actor a
/// combined value reads its highest-priority available source (ties by name), then its own
/// "expr" if it has one; a pending higher source makes it pending; nothing available reads
/// its fallback, or unavailable listing the sources tried. "combined": true declares one
/// with no expression of its own; a source naming an undeclared one declares it.
CONDITIONENGINE_API CE_Result ConditionEngine_RegisterNamedValue(const char* name, const char* definition, const char* owner,
                                                                 char* errorBuf, uint32_t errorBufSize);
CONDITIONENGINE_API CE_Result ConditionEngine_UnregisterNamedValue(const char* name);
/// JSON array: [{name, label, unit, range, integer, scope, pushed, source, expression, fallback, ...}]
CONDITIONENGINE_API uint32_t ConditionEngine_ListNamedValues(char* buf, uint32_t bufSize);

/// Pushed values, from any thread. `actor` null = the player; timeoutSeconds <= 0 uses the
/// named value's default timeout (none if it has none). Pushing an undeclared name declares
/// it. Not saved: every game load starts clean. CE_ERR_INVALID_ARG for invalid names.
CONDITIONENGINE_API CE_Result ConditionEngine_SetValue(void* actor, const char* name, double value, float timeoutSeconds);
CONDITIONENGINE_API CE_Result ConditionEngine_SetGlobalValue(const char* name, double value, float timeoutSeconds);
CONDITIONENGINE_API CE_Result ConditionEngine_ClearValue(void* actor, const char* name);
CONDITIONENGINE_API CE_Result ConditionEngine_ClearGlobalValue(const char* name);

/// External value type callback. Runs on the main thread. Return a CE_ValueStatus and write
/// *outValue when CE_VALUE_OK; otherwise it may write a reason ("Fear is not tracked for
/// this actor") into reasonBuf (reasonBufSize bytes, null-terminated).
typedef int32_t (*CE_CustomValueFn)(void* actor, const char* paramsJson, double* outValue, char* reasonBuf,
                                    uint32_t reasonBufSize, void* userData);

/// Registers a numeric type: RegisterType's schema plus "value": {"unit", "range": [min, max],
/// "integer"}. It reads as a value ("MyMod_Fear") and works as a condition with a trailing
/// comparison ("MyMod_Fear > 0.5"; compareField defaults to "value"). Signal changes with
/// ConditionEngine_NotifyEvent(dependencies or CE_DEP_VALUE, actor).
CONDITIONENGINE_API CE_Result ConditionEngine_RegisterValueType(const char* schemaJson, CE_CustomValueFn fn,
                                                                void* userData, uint32_t dependencies,
                                                                char* errorBuf, uint32_t errorBufSize);

// ── Value watches ────────────────────────────────────────────────────────────

typedef struct CE_ValueWatchOptions {
    uint32_t structSize;
    int32_t fireImmediately;        ///< Non-zero: deliver the first sample too
    uint32_t pollIntervalMs;        ///< Sampling rate: CE_POLL_DEFAULT, CE_POLL_NEVER (events only) or ms (>= 16)
    uint32_t extraDependencies;     ///< CE_DEP_* bits that also trigger a sample
    double minStep;                 ///< Fire when |value - last fired| >= minStep (0: any change, unless thresholds)
    const double* thresholds;       ///< Fire when the value crosses one (copied at call time)
    uint32_t thresholdCount;
    double hysteresis;              ///< A crossed threshold re-arms after moving back this far past it
} CE_ValueWatchOptions;

/// Main thread. crossedIndex = threshold index crossed, or -1 for a step / status change.
typedef void (*CE_ValueWatchFn)(CE_WatchHandle watch, void* actor, const CE_ValueSample* sample, int32_t crossedIndex,
                                void* userData);

/// Samples the value at its interval and on its dependencies; watches of the same value
/// and actor share one sample. `fn` may be null (the watch then only keeps the sample fresh
/// for ReadValueWatch). Unwatch with ConditionEngine_Unwatch.
CONDITIONENGINE_API CE_WatchHandle ConditionEngine_WatchValue(CE_ValueHandle value, void* actor, CE_ValueWatchFn fn,
                                                              void* userData, const CE_ValueWatchOptions* options);
/// ANY thread, non-blocking, never touches game state: the latest sample, and in `reasonBuf`
/// (may be null) why it is not OK, or when OK which provider answered ("from x", empty for
/// plain values). The per-frame read. Returns 1, or 0 for an unknown watch.
CONDITIONENGINE_API int32_t ConditionEngine_ReadValueWatch(CE_WatchHandle watch, CE_ValueSample* out, char* reasonBuf,
                                                           uint32_t reasonBufSize);
/// Main thread: samples now, then returns it like ReadValueWatch. 0 when unknown / world not loaded.
CONDITIONENGINE_API int32_t ConditionEngine_RefreshValueWatch(CE_WatchHandle watch, CE_ValueSample* out);

// ── Cost counters ────────────────────────────────────────────────────────────

/// JSON, any thread:
///   {"enabled": bool, "sinceMs": int,
///    "watcher": {"ticks", "tickTotalUs", "tickAverageUs", "tickMaxUs", "lastTickUs",
///                "conditionEvaluations", "valueSamples", "postedEvents", "drains",
///                "conditionWatches", "valueWatches"},
///    "availability": {"checks", "totalUs", "revision", "rebinds"},
///    "papyrusCalls": {"dispatched", "completed", "failed", "timedOut"},
///    "types": [{"name", "evaluations", "reads", "totalUs", "selfUs"}]}
/// watcher / availability / papyrusCalls are always counted. "types" (per condition type
/// and value node: Literal, Math, NamedValue, Truth) is filled only while enabled, as it
/// times every node: totalUs includes nested nodes, selfUs does not. Only types with
/// activity are listed. Counts run since the last ResetStats ("sinceMs"). "revision" is the
/// current availability revision; "rebinds" counts $name / @rule re-binding after it moved:
/// a revision that keeps moving makes every read re-bind.
CONDITIONENGINE_API uint32_t ConditionEngine_GetStats(char* buf, uint32_t bufSize);
CONDITIONENGINE_API void ConditionEngine_ResetStats(void);
/// Per-type timing on (non-zero) or off (default off).
CONDITIONENGINE_API void ConditionEngine_SetStatsEnabled(int32_t enabled);

#ifdef __cplusplus
}
#endif
