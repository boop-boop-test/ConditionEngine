# Values

A value is a number read from the game: any numeric condition type without its comparison (`Level`,
`ActorValue(Health, usePercentage=true)`, `Game(GetLevel)`), a named value (`$hunger`), a condition read as 1/0, or
arithmetic over those (`clamp($hunger * (1 - $light), 0, 100)`, `min`, `max`, `abs`, `lerp`, `piecewise`, `steps`).
Conditions compare them directly: `$vampire_hunger > 80`.

- `piecewise(x, x0, y0, x1, y1, ...)` is linear between the points and flat outside them; the x's run one way, rising
  or falling. It maps a mod's own scale onto yours: `piecewise(GlobalVariable(MyNeed), 0, 0, 160, 25, 340, 50, 1000, 100)`.
- `steps(x, t1, t2, ...)` counts the thresholds x has reached: `steps($hunger, 25, 50, 75)` reads 0-3.

Shared survival values (`$hunger`, `$fatigue`, `$cold`) are built in: see [Survival needs](survival.md).

Every read returns a status as well as the number:

| Status | Meaning |
|---|---|
| `CE_VALUE_OK` | A real reading |
| `CE_VALUE_PENDING` | No result yet (an async Papyrus call, a host still loading) |
| `CE_VALUE_UNAVAILABLE` | Missing plugin or form, actor not loaded, `enabledWhen` false, nothing pushed, unknown `$name` |
| `CE_VALUE_ERROR` | Misconfigured, or failed to evaluate |

A comparison with a side that isn't OK is false. When not OK, the number is the named value's `fallback`.

## Reading a value

```cpp
char err[256];
CE_ValueHandle health = ConditionEngine_CompileValue("ActorValue(Health, usePercentage=true)", err, sizeof err);

CE_ValueSample sample{sizeof sample};
char reason[128];
ConditionEngine_EvaluateValue(health, actor, &sample, reason, sizeof reason);   // main thread
if (sample.status == CE_VALUE_OK) {
    // sample.value
}
ConditionEngine_ReleaseValue(health);
```

`CompileValue` takes value text, a JSON value node, or a bare name (`"vampire_hunger"` means `$vampire_hunger`, even
if it's defined later). From Papyrus: `Float hp = ConditionEngine.GetValue(akActor, "ActorValue(Health)")`
(latent; an unavailable value returns its fallback, or 0).

### Every frame, from any thread

For a number you need every frame (an animation driver, a UI meter), watch it and read the latest sample. The read
never blocks and never touches game state:

```cpp
CE_ValueWatchOptions opts{sizeof opts};
opts.pollIntervalMs = 250;   // sampling rate, at least 16 ms; CE_POLL_NEVER = events only
CE_WatchHandle w = ConditionEngine_WatchValue(hunger, actor, nullptr, nullptr, &opts);   // no callback

// Render thread, every frame:
CE_ValueSample s{sizeof s};
ConditionEngine_ReadValueWatch(w, &s, nullptr, 0);   // s.value, s.status, s.ageMs
```

With a callback, a value watch fires on the main thread when the value moves by `minStep`, crosses one of
`thresholds` (re-armed after moving back by `hysteresis`), or on any change. `crossedIndex` tells you which threshold,
or -1 for a step or status change. Watches of the same value and actor share one sample.

## Adding a named value

In a preset file (`Data/SKSE/Plugins/ConditionEngine/*.json`):

```json
{
  "values": {
    "mymod_light": "Game(GetLightLevel) / 100",
    "mymod_fatigue": {
      "label": "Fatigue", "unit": "%", "range": [0, 100],
      "expr": "StorageUtil(mymod_fatigue)",
      "requiresPlugins": ["MyMod.esp"], "fallback": 0
    }
  }
}
```

From the C API: `ConditionEngine_RegisterNamedValue("mymod_fatigue", definition, "MyMod", err, sizeof err)`, where
`definition` is value text or the same JSON object. From Papyrus: `ConditionEngine.RegisterNamedValue(name,
definition)`, again from `OnPlayerLoadGame`.

Definition keys: `expr`, `label`, `unit`, `range`, `integer`, `fallback`, `enabledWhen` (a condition; false makes the
value unavailable for that actor), `scope` (`auto`, `actor`, `global`), `pushed`, `graceSeconds` (keep the last good
reading after a failed one), `requiresPlugins`, `requiresDlls`, `refreshOn` (mod event names), plus the catalog keys
(`category`, `icon`, `description`, `offer`, `tags`, `owner`).

### Several mods, one value

Several mods can each supply the same value. A value with `"provides": "hunger"` and a `priority` is a source for
`$hunger`:

```json
{ "values": { "mymod_hunger": { "expr": "StorageUtil(mymod_hunger)", "provides": "hunger", "priority": 50,
                                "requiresPlugins": ["MyMod.esp"] } } }
```

For each actor, `$hunger` reads its highest-priority available source (ties by name), then its own `expr` if it has
one. A pending higher source keeps it pending, so it doesn't flip between mods while one loads. The reason string
names the source that answered (`from mymod_hunger`).

## Pushing a value

A script or plugin can set a value directly. Pushing a name nobody declared declares it, so this works with no preset:

```papyrus
ConditionEngine.SetValue(akActor, "mymod_mood", 0.8, 600.0)   ; reverts after 600 s
Bool bad = ConditionEngine.Evaluate(akActor, "$mymod_mood > 0.5")
```

```cpp
ConditionEngine_SetValue(actor, "mymod_mood", 0.8, 600.0f);      // any thread; actor null = the player
ConditionEngine_SetGlobalValue("mymod_threat", 3.0, 0.0f);       // not per actor
```

A timeout of 0 uses the value's declared default timeout (none if it has none). Afterwards the value reverts to its
expression or `default`, or reads unavailable. `ClearValue` / `ClearGlobalValue` remove a push. Pushed values are never
saved: every game load starts clean, so push again after loading.

## Registering a value type

When your plugin computes a number (with parameters), register a value type. It reads as a value and compares as a
condition (`MyMod_Fear > 0.5`):

```cpp
int32_t ReadFear(void* actor, const char* paramsJson, double* outValue, char* reasonBuf, uint32_t reasonBufSize,
                 void* userData) {
    auto* a = static_cast<RE::Actor*>(actor);
    if (!MyMod::Fear::Tracks(a)) {
        strncpy_s(reasonBuf, reasonBufSize, "fear is not tracked for this actor", _TRUNCATE);
        return CE_VALUE_UNAVAILABLE;
    }
    *outValue = MyMod::Fear::Get(a);
    return CE_VALUE_OK;
}

ConditionEngine_RegisterValueType(R"({
    "name": "MyMod_Fear", "label": "Fear", "category": "actor", "source": "MyMod",
    "value": {"unit": "", "range": [0, 1]}
})", ReadFear, nullptr, CE_DEP_VALUE, err, sizeof err);

// When fear changes (any thread):
ConditionEngine_NotifyEvent(CE_DEP_VALUE, actor);
```

The schema is the same as for [condition types](registering-condition-types.md#the-schema) plus `"value"`. The
callback runs on the main thread. The compare field defaults to `value`, so a trailing comparison just works.
