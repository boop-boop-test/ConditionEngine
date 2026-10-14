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
///   2 - WatchEx (per-watch poll interval + extra triggers), Refresh, Set/GetPollInterval,
///       CE_DEP_INVENTORY / CE_DEP_MAGIC_EFFECT / CE_DEP_ACTOR_STATE
///   3 - GetCatalog / GetJsonSchema (self-describing types), RegisterTypeEx (type + schema)
///   4 - Availability snapshots, required plugins, type status publication and change subscriptions
///   5 - Values: value expressions, named values ($name), pushed values, value watches with a
///       non-blocking ReadValueWatch, RegisterValueTypeEx, CE_DEP_VALUE, Compare conditions
///   6 - Catalog organisation: shared topical categories (RegisterCategory), origin labels
///       (RegisterSource), named-condition metadata (RegisterNamedEx), offer / owner / tags on
///       every item, GetCatalogEx for a picker's view

#include <stdint.h>
#include "ConditionEngine/Export.h"

#define CONDITIONENGINE_API_VERSION 6

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t CE_Handle;       ///< Compiled condition
typedef uint32_t CE_WatchHandle;  ///< Active watch (condition or value)
typedef uint32_t CE_ValueHandle;  ///< v5: compiled value expression

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
    CE_DEP_HOST = 1u << 18,         ///< Host-signalled state (fire via NotifyEvent)
    CE_DEP_INVENTORY = 1u << 19,    ///< v2: item counts
    CE_DEP_MAGIC_EFFECT = 1u << 20, ///< v2: active effects, spells, perks
    CE_DEP_ACTOR_STATE = 1u << 21,  ///< v2: sneaking, sitting, dialogue, bleedout, weapon drawn, follower
    CE_DEP_AVAILABILITY = 1u << 22,
    CE_DEP_VALUE = 1u << 23         ///< v5: a pushed value was set, a named value redefined, an async sample arrived
};

#define CE_POLL_DEFAULT 0u           ///< Use the global poll interval
#define CE_POLL_NEVER   0xFFFFFFFFu  ///< Only re-check on change detection / events

/// v2 watch options. Set structSize = sizeof(CE_WatchOptions) for forward compatibility.
typedef struct CE_WatchOptions {
    uint32_t structSize;
    int32_t fireImmediately;     ///< Non-zero: deliver the initial result too
    uint32_t pollIntervalMs;     ///< CE_POLL_DEFAULT, CE_POLL_NEVER or milliseconds
    uint32_t extraDependencies;  ///< CE_DEP_* bits that also trigger a re-check (e.g. CE_DEP_LOCATION)
} CE_WatchOptions;

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

/// v3: Registers a custom type together with its schema, so editors (DressCode's UI,
/// other mods) can offer it without knowing it. v6 adds "source" (origin id; when missing,
/// the calling module's file name is used and a warning logged), "offer" and "tags".
/// `schemaJson`:
///   {"name": "MyMod_IsCursed", "label": "Is Cursed", "category": "actor",
///    "description": "...", "aliases": [], "compareField": "level",
///    "fields": [{"key": "level", "label": "Curse Level", "type": "int", "default": 1}],
///    "positional": ["level"]}
/// Field types: int, float, string, bool, enum, enumList, form, formList, stringList,
/// intList, comparison, namedCondition, custom (see ConditionEngine/Schema.h).
CONDITIONENGINE_API CE_Result ConditionEngine_RegisterTypeEx(const char* schemaJson, CE_CustomEvalFn fn, void* userData,
                                                             uint32_t dependencies, char* errorBuf,
                                                             uint32_t errorBufSize);

// ── Catalog ──────────────────────────────────────────────────────────────────

/// v3: Every registered type with its fields, labels and categories, as JSON:
///   {"version": 1, "categories": [{"id","label","order"}],
///    "types": [{"name","label","category","icon","description","aliases","fields",
///               "positional","compareField","dependencies","hidden","source"}]}
CONDITIONENGINE_API uint32_t ConditionEngine_GetCatalog(char* buf, uint32_t bufSize);
/// v3: JSON Schema (draft-07) validating condition JSON, generated from the catalog.
CONDITIONENGINE_API uint32_t ConditionEngine_GetJsonSchema(char* buf, uint32_t bufSize);

// ── Catalog organisation (v6) ────────────────────────────────────────────────
// Categories are topical and shared: "survival" means survival whoever registered it.
// They form a tree through "parent". Every catalog item (types, namedValues,
// namedConditions) carries:
//   category      its category id (an id nobody registered is created on the fly, top
//                 level, labelled from the id - items are never dropped or regrouped)
//   source        origin id (built-ins: "ConditionEngine"), also the owner
//   sourceLabel   origin label for display, e.g. "Vampire hunger · SeverActions"
//   offer         "everywhere" | "owner" | "nowhere" (pickers show owner items only to
//                 their owner; nowhere = expression-only; metadata, never enforced)
//   tags          free tags plus automatic ones: condition, value, per-actor, global,
//                 pushed, async, requires-plugin
//   icon, description
// The catalog root lists "categories" and "sources": [{id, label, count}]. The catalog is
// small: pickers fetch it once and filter by category, source, tags and text themselves.
// Pickers should show each item's origin (sourceLabel) next to its label.

/// Registers a category: {"id", "label", "parent", "order", "icon", "description"}.
/// Registering an existing id is reuse (CE_OK): the first registrant keeps the label and
/// later ones only fill in a missing description, icon or parent. A parent cycle is
/// CE_ERR_INVALID_ARG. `owner` null = the calling module's file name.
CONDITIONENGINE_API CE_Result ConditionEngine_RegisterCategory(const char* categoryJson, const char* owner,
                                                               char* errorBuf, uint32_t errorBufSize);
/// Display label for an origin id, e.g. ("NoTalkJustHands", "No Talk Just Hands").
/// Unlabelled ids display as themselves.
CONDITIONENGINE_API void ConditionEngine_RegisterSource(const char* id, const char* label);
/// A picker's view: {"for": "<owner>", "include": ["types", "values", "conditions",
/// "categories"], "offered": true}. "for" adds that owner's owner-only items; offered:false
/// returns everything ("show all"). Null / "" = GetCatalog (everything).
CONDITIONENGINE_API uint32_t ConditionEngine_GetCatalogEx(const char* filterJson, char* buf, uint32_t bufSize);
/// Named condition with picker metadata. `definition` is expression text, a condition tree
/// (JSON with "type"), or {"expr" | "condition", "label", "category", "icon", "description",
/// "offer", "tags", "owner"}. `owner` null = the calling module's file name.
CONDITIONENGINE_API CE_Result ConditionEngine_RegisterNamedEx(const char* id, const char* definition, const char* owner,
                                                              char* errorBuf, uint32_t errorBufSize);

// ── Availability (v4) ─────────────────────────────────────────────────────────
// RegisterTypeEx accepts requiresPlugins: ["SomeMod.esp"]. Catalog entries include
// availability: {state: "available" | "pending" | "unavailable", available: bool, reason: string}.
// Form fields with portable 0x...|Plugin.esp keys also require that plugin at expression level.
// Types always remain registered so saved expressions still compile without their dependencies.
// Evaluate/EvaluateNamed and watches return false for an unavailable whole expression (even under NOT/OR).
// C++ consumers use Condition::EvaluateAvailable at rule boundaries for the same behavior.
// SetTypeAvailability publishes host readiness; 0=pending, 1=available, 2=unavailable.
// Reads and publication are thread-safe. Subscribers run on the publishing thread, outside locks;
// marshal game/UI work to the main thread. Unsubscribe before unloading and synchronize any in-flight callback.
typedef void (*CE_AvailabilityChangedFn)(uint64_t revision, void* userData);
CONDITIONENGINE_API uint32_t ConditionEngine_GetAvailability(CE_Handle handle, char* buf, uint32_t bufSize);
CONDITIONENGINE_API CE_Result ConditionEngine_SetTypeAvailability(const char* type, int32_t state, const char* reason);
CONDITIONENGINE_API uint32_t ConditionEngine_SubscribeAvailability(CE_AvailabilityChangedFn fn, void* userData);
CONDITIONENGINE_API void ConditionEngine_UnsubscribeAvailability(uint32_t subscription);
CONDITIONENGINE_API uint64_t ConditionEngine_GetAvailabilityRevision(void);

// ── Watches ──────────────────────────────────────────────────────────────────

/// Re-evaluates when one of the condition's dependencies fires and calls `fn`
/// when the result changes. The current result is evaluated immediately and
/// passed to `fn` when `fireImmediately` is non-zero.
CONDITIONENGINE_API CE_WatchHandle ConditionEngine_Watch(CE_Handle handle, void* actor, CE_WatchFn fn, void* userData,
                                                         int32_t fireImmediately);
CONDITIONENGINE_API void ConditionEngine_Unwatch(CE_WatchHandle watch);

/// v2: Watch with options (null options = defaults).
CONDITIONENGINE_API CE_WatchHandle ConditionEngine_WatchEx(CE_Handle handle, void* actor, CE_WatchFn fn, void* userData,
                                                           const CE_WatchOptions* options);
/// v2: Re-evaluates a watch now (callback fires on change). Returns 1/0, or -1 when
/// the watch is unknown or the world is not loaded. Main thread only.
CONDITIONENGINE_API int32_t ConditionEngine_Refresh(CE_WatchHandle watch);
/// v2: Default full-poll interval for watches (initially ConditionEngine.ini, 10 s).
CONDITIONENGINE_API void ConditionEngine_SetPollInterval(uint32_t milliseconds);
CONDITIONENGINE_API uint32_t ConditionEngine_GetPollInterval(void);

/// Signals that host-owned state changed. `actor` null = all watched actors. Any thread;
/// notifications before the main thread drains them merge (v5).
CONDITIONENGINE_API void ConditionEngine_NotifyEvent(uint32_t dependencies, void* actor);

// ── Providers ────────────────────────────────────────────────────────────────

CONDITIONENGINE_API void ConditionEngine_SetLoveSceneProvider(CE_ActorFlagFn fn, void* userData);

// ── Values (v5) ──────────────────────────────────────────────────────────────
// A value is the number a numeric condition type compares, read on its own, plus
// arithmetic: "ActorValue(Health, usePercentage=true)", "$hunger * (1 - $light)",
// "clamp($x, 0, 1)", min/max/abs/lerp; a condition used as a value reads 0 / 1.
// Named values ($name) come from preset files ("values": {...}), RegisterNamedValue
// or SetValue, and resolve by name on every read, so redefinitions reach existing
// handles and watches. An unknown name reads CE_VALUE_UNAVAILABLE.
// Conditions compare values directly: "$vampire_hunger > 80".

typedef enum CE_ValueStatus {
    CE_VALUE_OK = 0,
    CE_VALUE_PENDING = 1,      ///< No sample yet (async) or a required plugin / host is not ready
    CE_VALUE_UNAVAILABLE = 2,  ///< Missing plugin/form, actor not loaded, enabledWhen false, no value pushed
    CE_VALUE_ERROR = 3         ///< Misconfigured or failed to evaluate
} CE_ValueStatus;

/// Set structSize = sizeof(CE_ValueSample); fields past it are not written.
typedef struct CE_ValueSample {
    uint32_t structSize;
    int32_t status;                 ///< CE_ValueStatus
    double value;                   ///< Last good value, or the named value's fallback when not OK
    uint32_t ageMs;                 ///< Since the value was read, computed when you read; 0xFFFFFFFF = never
    uint64_t availabilityRevision;  ///< ConditionEngine_GetAvailabilityRevision() when sampled
} CE_ValueSample;

/// Set structSize = sizeof(CE_ValueWatchOptions); null options = defaults.
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

/// External value type callback. Runs on the main thread (watch sampling, EvaluateValue,
/// Evaluate). Return a CE_ValueStatus and write *outValue when CE_VALUE_OK.
typedef int32_t (*CE_CustomValueFn)(void* actor, const char* paramsJson, double* outValue, void* userData);

/// Like CE_CustomValueFn, and when the status is not CE_VALUE_OK it may write a reason
/// ("Fear is not tracked for this actor") into reasonBuf (null-terminated, reasonBufSize bytes).
typedef int32_t (*CE_CustomValueReasonFn)(void* actor, const char* paramsJson, double* outValue, char* reasonBuf,
                                          uint32_t reasonBufSize, void* userData);

/// Compiles value text, a JSON value node (leading '{') or a bare named-value name
/// ("vampire_hunger" = "$vampire_hunger", defined now or later). 0 on failure.
CONDITIONENGINE_API CE_ValueHandle ConditionEngine_CompileValue(const char* source, char* errorBuf, uint32_t errorBufSize);
CONDITIONENGINE_API void ConditionEngine_ReleaseValue(CE_ValueHandle value);
/// Main thread. Reads now (async nodes return their latest sample). CE_ERR_INVALID_HANDLE / CE_OK.
CONDITIONENGINE_API CE_Result ConditionEngine_EvaluateValue(CE_ValueHandle value, void* actor, CE_ValueSample* out);
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
///    "pushed": true | {"default", "timeoutSeconds"}, "graceSeconds", "requiresPlugins": [...],
///    "requiresDlls": ["SomeNative.dll"]}   (DLLs must be loaded in the game process)
/// ConditionEngine_UnregisterOwner(owner) also removes named values.
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

/// Samples the value at its interval and on its dependencies; watches of the same value
/// and actor share one sample. `fn` may be null (the watch then only keeps the sample fresh
/// for ReadValueWatch). Unwatch with ConditionEngine_Unwatch.
CONDITIONENGINE_API CE_WatchHandle ConditionEngine_WatchValue(CE_ValueHandle value, void* actor, CE_ValueWatchFn fn,
                                                              void* userData, const CE_ValueWatchOptions* options);
/// ANY thread, non-blocking, never touches game state: the latest sample. The per-frame read.
/// Returns 1, or 0 for an unknown watch.
CONDITIONENGINE_API int32_t ConditionEngine_ReadValueWatch(CE_WatchHandle watch, CE_ValueSample* out);
/// Main thread: samples now, then returns it like ReadValueWatch. 0 when unknown / world not loaded.
CONDITIONENGINE_API int32_t ConditionEngine_RefreshValueWatch(CE_WatchHandle watch, CE_ValueSample* out);
/// Any thread: why the latest sample is not OK ("actor not loaded", "no value named x"),
/// empty when it is OK. Returns the size needed; 0 for an unknown watch.
CONDITIONENGINE_API uint32_t ConditionEngine_ReadValueWatchReason(CE_WatchHandle watch, char* buf, uint32_t bufSize);
/// EvaluateValue that also writes the reason (empty when OK) into `reasonBuf`.
CONDITIONENGINE_API CE_Result ConditionEngine_EvaluateValueEx(CE_ValueHandle value, void* actor, CE_ValueSample* out,
                                                              char* reasonBuf, uint32_t reasonBufSize);

/// Registers a numeric type: RegisterTypeEx's schema plus "value": {"unit", "range": [min, max],
/// "integer"}. It reads as a value ("MyMod_Fear") and works as a condition with a trailing
/// comparison ("MyMod_Fear > 0.5"; compareField defaults to "value"). Signal changes with
/// ConditionEngine_NotifyEvent(dependencies or CE_DEP_VALUE, actor).
CONDITIONENGINE_API CE_Result ConditionEngine_RegisterValueTypeEx(const char* schemaJson, CE_CustomValueFn fn,
                                                                  void* userData, uint32_t dependencies,
                                                                  char* errorBuf, uint32_t errorBufSize);
/// RegisterValueTypeEx whose callback reports a reason with a non-OK status.
CONDITIONENGINE_API CE_Result ConditionEngine_RegisterValueTypeWithReason(const char* schemaJson, CE_CustomValueReasonFn fn,
                                                                          void* userData, uint32_t dependencies,
                                                                          char* errorBuf, uint32_t errorBufSize);

#ifdef __cplusplus
}
#endif
