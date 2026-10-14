# Values (API v5) — design draft

Status: **built (steps 1-7), unit-tested, not yet run in game**. Requested by NTJH (issue 166). Open questions decided (see Decisions). README.md "Values (API v5)" documents the shipped API; this file is the design record.

## Goal

Every numeric condition type already reads a number and compares it. v5 exposes that number on its own, as a
**value**, so consumers (NTJH drivers, DressCode, SkyrimNet actions) can read, combine, name, push and watch it.
v5 only adds things: v4 JSON, expressions, exports and callbacks keep working unchanged.

## Shape

- **One type registry.** A numeric type keeps its `TypeInfo` and gains an optional `valueFactory`, plus value
  metadata (`unit`, `range`, `integer`). The same entry serves `ActorValue(Health) < 30` (condition) and
  `ActorValue(Health)` (value). There's no second registry and no second catalog.
- **A new `Value` node interface**, separate from `Condition`:
  ```cpp
  struct ValueResult { double value = 0; ValueStatus status = ValueStatus::Ok; std::uint32_t ageMs = 0; };
  class Value {
      virtual ValueResult Read(const EvaluationContext&) const = 0;
      virtual EventDependency GetEventDependencies() const = 0;
      virtual nlohmann::json ToJson() const = 0;      // {"type": "ActorValue", ...} same fields minus the comparison
      virtual std::unique_ptr<Value> Clone() const = 0;
  };
  ```
  Built-in nodes: `Literal`, `Arith` (+ - * /), `Func` (min, max, clamp, abs, lerp), `Named` (`$name`),
  `FromCondition` (any condition read as 0/1), and one node per numeric type.
- **A new `Compare` condition** for comparisons a single type's `compareField` can't express:
  `{"type": "Compare", "left": <value>, "operator": ">", "right": <value>}`. The parser emits it only when a side is
  arithmetic, `$name` or a value-only type. `ActorValue(Health) < 30` still produces today's JSON.
- Existing condition classes stay as they are. Each value node shares a small read helper with its condition class,
  so the two can't drift, and the condition JSON never changes.
- **Named values** get their own table (`$name`), separate from named conditions (`@Name`), because the two return
  different types. They're cycle-checked like rules.

## Expression syntax additions

```
value    := term (("+" | "-") term)*
term     := unary (("*" | "/") unary)*
unary    := "-" unary | atom
atom     := number | "$" name | "(" value ")" | func "(" value ("," value)* ")" | Type [ "(" args ")" ]
cmp      := value cmpop value          // becomes Compare unless it's the plain Type(...) op literal form
```

- `$name` (no brace) is free; `${param}` stays parameter substitution.
- Lexer change: today `-` or `+` directly before a digit lexes as part of a number, so `$a -1` would break. A sign
  should only join a number at the start of an expression or after an operator, `(` or `,`.
- A condition used where a value is expected reads 0/1, e.g. `$hunger * (1 - IsInterior)`.

## Value types

| Type | Value | Work |
|---|---|---|
| ActorValue (current / `usePercentage`) | float | trivial: `Evaluate` already computes it |
| Level | int | trivial |
| ItemCount (form or keyword) | int | trivial |
| Global | float | trivial |
| QuestStage | int | trivial |
| Faction | rank, -1 = not a member | trivial |
| Temperature | provider float | trivial |
| Time / DayOfMonth / Month / DayOfWeek | hour, day, month, weekday | trivial |
| Random | the roll | trivial |
| every boolean type | 0/1 via `FromCondition` | free |
| Game(Function, ...) | raw function result | **moderate**: today it builds a `TESConditionItem` and calls `IsTrue`. A value needs the raw double from the function's `SCRIPT_FUNCTION::conditionFunction`. Same parameter compilation |
| ScriptValue | numeric property, bool as 0/1 | **moderate**: today it only reads scripts bound to the *actor*. SeverActions' gates (`Enabled`, `HungerEnabled`) live on quest `0xD62\|SeverActions.esp`, so it needs an optional `form` field (quest or any form) |
| PapyrusCall (new) | number returned by a Papyrus function | **new, async**, see below |
| Pushed (new) | set via SetValue | **new**, see below |
| external (RegisterValueTypeEx) | host callback | **new callback type** |

### PapyrusCall (async)

```
PapyrusCall(script="SeverActionsNative", function="Native_Survival_GetHunger", interval=2)             // static call
PapyrusCall(form="0xD62|SeverActions.esp", script="SeverActions_Survival", function="...", interval=2)  // bound method
```

- Note: SeverActions' hunger is a *static* native (`DispatchStaticCall("SeverActionsNative", ...)` in NTJH's
  VampireTeeth.cpp), not a quest method. Both forms are supported; the actor is the single argument.
- `Read` never blocks. It returns the last sample with its age, and if the sample is older than `interval` it queues
  one dispatch per (node, actor), throttled and generation-checked as in VampireTeeth.cpp.
- Status is `Pending` before the first sample. A failed call keeps the last good value for `graceSeconds`, then the
  status becomes `Unavailable` with a reason.
- When a sample arrives, the engine fires the new `CE_DEP_VALUE` bit for that actor, so watches and `$x > 80`
  conditions react without waiting for a poll.
- `requiresPlugins` is derived from `form` and from an explicit `requiresPlugins` field. The `requiresDll` check
  needs a new availability source (`GetModuleHandle`), which is small.

### Pushed

- `SetValue(actor, name, value, timeoutSeconds)` and `SetGlobalValue(name, value, timeoutSeconds)` from Papyrus and
  the C API. When the timeout expires, the value reverts to its declared `default`, or reads `Unavailable` if none
  is declared.
- A name doesn't have to be declared: SetValue on an unknown name creates a pushed value, so a SkyrimNet action YAML
  works without a data file. Declaring it in a preset adds a label, range and default.
- Each set fires `CE_DEP_VALUE` for that actor (for a global value, for all actors).
- Not saved: pushed values aren't written to the SKSE co-save. Every game load starts clean, and actions push
  again after load.

## Named values in presets

Same files as named conditions (`Data/SKSE/Plugins/ConditionEngine/*.json`), new `"values"` key:

```json
{
  "values": {
    "light": "Game(GetLightLevel) / 100",
    "vampire_hunger": {
      "label": "Vampire hunger (SeverActions)", "unit": "%", "range": [0, 100],
      "expr": "PapyrusCall(script=SeverActionsNative, function=Native_Survival_GetHunger, interval=2)",
      "enabledWhen": "ScriptValue(form=\"0xD62|SeverActions.esp\", script=SeverActions_Survival, Enabled) && ScriptValue(form=\"0xD62|SeverActions.esp\", script=SeverActions_Survival, HungerEnabled) && IsVampire",
      "requiresPlugins": ["SeverActions.esp"],
      "graceSeconds": 30,
      "fallback": 0
    },
    "hungry_mood": { "pushed": { "default": 0, "timeoutSeconds": 600 }, "range": [0, 1] },
    "thirst_scaled": "$vampire_hunger * (1 - $light)"
  },
  "conditions": {
    "VeryHungry": "$vampire_hunger > 80"
  }
}
```

- `enabledWhen`: if false for an actor, the value is `Unavailable` ("not enabled for this actor") for that actor.
  This covers NTJH's `appliesTo`, its `excluded` check (as a second PapyrusCall read as a condition) and its
  property gates.
- `fallback`: the number returned while the value is pending or unavailable. Availability is reported regardless,
  so nothing falls back silently.
- Smoothing, curves, tracks and drivers stay in NTJH. The engine reports raw values plus their age.

## C API (v5)

```c
#define CONDITIONENGINE_API_VERSION 5

typedef uint32_t CE_ValueHandle;   ///< Compiled value expression (0 = invalid)

typedef enum CE_ValueStatus {
    CE_VALUE_OK = 0,
    CE_VALUE_PENDING = 1,       ///< No sample yet (async) or a required plugin/host isn't ready
    CE_VALUE_UNAVAILABLE = 2,   ///< Missing plugin, enabledWhen false, grace expired, timed-out push without default
    CE_VALUE_ERROR = 3          ///< Invalid handle / evaluation failed
} CE_ValueStatus;

typedef struct CE_ValueSample {
    uint32_t structSize;
    double value;               ///< Last good value, or the declared fallback when not OK
    int32_t status;             ///< CE_ValueStatus
    uint32_t ageMs;             ///< Since the value was read; 0xFFFFFFFF = never
    uint64_t availabilityRevision;
} CE_ValueSample;

enum { CE_DEP_VALUE = 1u << 23 };  ///< A pushed value was set or an async sample arrived

// Compile / evaluate ----------------------------------------------------------
/// Value expression ("$hunger * (1 - $light)", "ActorValue(Health, usePercentage=true)", "vampire_hunger"
/// as a bare name) or JSON. Returns 0 and writes the error on failure.
CE_ValueHandle ConditionEngine_CompileValue(const char* source, char* errorBuf, uint32_t errorBufSize);
void           ConditionEngine_ReleaseValue(CE_ValueHandle value);
/// Main thread. Synchronous for sync nodes; async nodes return their cached sample and schedule a refresh.
CE_Result      ConditionEngine_EvaluateValue(CE_ValueHandle value, void* actor, CE_ValueSample* out);
uint32_t       ConditionEngine_GetValueDependencies(CE_ValueHandle value);
uint32_t       ConditionEngine_GetValueAvailability(CE_ValueHandle value, char* buf, uint32_t bufSize); // v4 JSON shape
uint32_t       ConditionEngine_ValueToExpression(CE_ValueHandle value, char* buf, uint32_t bufSize);
uint32_t       ConditionEngine_ValueToJson(CE_ValueHandle value, char* buf, uint32_t bufSize);

// Named values ----------------------------------------------------------------
CE_Result ConditionEngine_RegisterNamedValue(const char* name, const char* definitionJsonOrExpr, const char* owner,
                                             char* errorBuf, uint32_t errorBufSize);
CE_Result ConditionEngine_UnregisterNamedValue(const char* name);
/// [{name, label, unit, range, source, expression, pushed}]
uint32_t  ConditionEngine_ListNamedValues(char* buf, uint32_t bufSize);
// ConditionEngine_UnregisterOwner (v1) also removes named values with that owner.

// Pushed values (any thread; the change notification is marshalled to the main thread) ----------
CE_Result ConditionEngine_SetValue(void* actor, const char* name, double value, float timeoutSeconds); // actor null = player
CE_Result ConditionEngine_SetGlobalValue(const char* name, double value, float timeoutSeconds);
CE_Result ConditionEngine_ClearValue(void* actor, const char* name);
CE_Result ConditionEngine_ClearGlobalValue(const char* name);

// Value watches -----------------------------------------------------------------
typedef struct CE_ValueWatchOptions {
    uint32_t structSize;
    int32_t  fireImmediately;
    uint32_t pollIntervalMs;       ///< Sampling rate: CE_POLL_DEFAULT, CE_POLL_NEVER (events only) or ms
    uint32_t extraDependencies;
    double   minStep;              ///< Fire when |value - lastFired| >= minStep (0 = any change)
    const double* thresholds;      ///< Fire when a threshold is crossed (copied at call time)
    uint32_t thresholdCount;
    double   hysteresis;           ///< Must move this far back past a threshold to re-arm it
} CE_ValueWatchOptions;

/// Main thread. crossedIndex = threshold index crossed, or -1 for a step/status change.
typedef void (*CE_ValueWatchFn)(CE_WatchHandle watch, void* actor, const CE_ValueSample* sample,
                                int32_t crossedIndex, void* userData);

/// fn may be null: the watch then only keeps the sample fresh for ReadValueWatch.
CE_WatchHandle ConditionEngine_WatchValue(CE_ValueHandle value, void* actor, CE_ValueWatchFn fn, void* userData,
                                          const CE_ValueWatchOptions* options);
/// ANY thread, non-blocking: the latest sample (value, status, age). This is the per-frame read.
/// Returns 1, or 0 for an unknown watch. Unwatch with ConditionEngine_Unwatch (v1).
int32_t ConditionEngine_ReadValueWatch(CE_WatchHandle watch, CE_ValueSample* out);

// External value types ------------------------------------------------------------
/// Returns a CE_ValueStatus; writes *outValue when OK.
typedef int32_t (*CE_CustomValueFn)(void* actor, const char* paramsJson, double* outValue, void* userData);
/// schemaJson as RegisterTypeEx plus "value": {"unit": "%", "range": [0, 1], "integer": false}.
/// The type is also usable as a condition: compareField defaults to "value" (MyMod_Fear() > 0.5).
CE_Result ConditionEngine_RegisterValueTypeEx(const char* schemaJson, CE_CustomValueFn fn, void* userData,
                                              uint32_t dependencies, char* errorBuf, uint32_t errorBufSize);
```

Catalog (v3 `GetCatalog`) additions: each value-capable type gets `"value": {"unit", "range", "integer"}`, and the
root gets `"namedValues": [...]`. JSON Schema covers `Compare` and value nodes.

Threading model: sampling runs in the Watcher tick on the main thread, using the same two tiers as condition
watches. Per-watch interval, event-driven re-reads via dependency bits, and `CE_DEP_VALUE` for pushes and async
arrivals. Each watch stores its sample in a small lock-guarded slot (or a seqlock), so `ReadValueWatch` from a render
thread never touches game state.

### Clarifications (NTJH review)

- **`ageMs` is computed when read** (now minus sample time). It is never stored.
- **External value callbacks run on the main thread**, called from Watcher evaluation or `EvaluateValue`. Hosts with
  fast-changing state call `NotifyEvent(CE_DEP_VALUE, actor)` from any thread. `NotifyEvent` already accepts any
  dependency bits and marshals to the main thread. v5 adds coalescing: notifications for the same actor and bits
  within one frame cause one re-evaluation.
- **`$name` resolves by name on every read**, like `@Name` today. Redefining a name (preset reload or
  RegisterNamedValue) is picked up by existing watches. An unknown name reads `Unavailable` ("no value named x"),
  not `Error`.
- **Actor not resolvable or not loaded** (no form, or no 3D): the watch is kept and reads `Unavailable` ("actor not
  loaded"). Sampling resumes when the actor resolves again. Today's condition watches skip such actors silently, and
  v5 will report it for values.
- **Load: ~100 value watches at 100–250 ms.**
  - Today's Watcher ticks once a second, posted from a timer thread, so per-watch intervals under 1 s don't happen.
    Value watches need a sampler that runs at the shortest active interval (a main-thread task, each frame at most).
  - Samples are shared per (value definition, actor). Several watches on the same value and actor share one read and
    one slot, and each watch keeps its own step and threshold state.
  - One `EvaluationContext` is built per actor per pass. Its constructor snapshots location, weather and time, which
    is wasteful at 4–10 Hz, so value sampling uses a lazy context that fetches those fields only when asked.
  - Sync nodes are field reads, so ~100 reads at 4–10 Hz is negligible. `Game(...)` and `ScriptValue` cost more but
    are still small. Async PapyrusCall reads cost nothing in the tick because they run on the VM.

## Papyrus

```papyrus
Float    Function GetValue(Actor akActor, String asNameOrExpr) global native      ; latent, main thread
Function SetValue(Actor akActor, String asName, Float afValue, Float afTimeoutSeconds = 0.0) global native
Function SetGlobalValue(String asName, Float afValue, Float afTimeoutSeconds = 0.0) global native
Function ClearValue(Actor akActor, String asName) global native
Int      Function WatchValue(Actor akActor, String asNameOrExpr, String asModEvent, Float afMinStep = 0.0, Float afPollSeconds = 0.0) global native
```

`WatchValue` sends `asModEvent(strArg=name, numArg=value, sender=actor)`, matching how condition `Watch` works today.

## Decisions (Eric, 2026-10-13, relayed by the NTJH session)

1. **Pushed value persistence: not saved.** No co-save. Every load starts clean; actions push again after load.
2. **Scope: inferred from the nodes** (any node that reads the actor makes the value per-actor), with an optional
   `"scope"` override.
3. **StorageUtil: a native value type**, `StorageUtil(key)`, requiring PapyrusUtil.
4. **SkyrimNet decorators stay out of ConditionEngine.** The bridge owner (NTJH) registers them as external value
   types.

Build is on hold until Eric gives the go.

## Build order

1. Value node interface, the trivial types, Arith/Func/Literal/FromCondition, the parser (including the lexer sign
   fix), and `Compare`. Tests in `condition-engine-tests` (no game needed).
2. Named values (presets and C API), cycle checks, catalog `value` metadata.
3. Pushed values and `CE_DEP_VALUE`.
4. Value watches and `ReadValueWatch`.
5. Game raw value and ScriptValue `form`.
6. PapyrusCall (async), grace and fallback, the DLL availability check.
7. RegisterValueTypeEx and Papyrus natives. Then DressCode's UI can offer value comparisons.
