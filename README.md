# ConditionEngine

Reusable condition and expression evaluator for Skyrim SE/AE/VR SKSE plugins.
It was extracted from DressCode and has no DressCode dependencies.

- **About 30 built-in condition types**:
  - Time, date, location, weather and temperature
  - Faction, actor values, race and actor state (combat, swimming, sleeping and so on)
  - Equipment slots, globals, quests, Papyrus script properties
  - Seeded random, and/or/not logic, and references to named conditions
- **Expression syntax** that compiles to the same JSON trees DressCode stores:
  `Time(20, 6) && !InteriorExterior(true) || @BadWeather`
- **Named conditions**, which you reference as `@Name`. They can come from data-only preset files, from Papyrus, from other plugins, or from host resolvers such as DressCode rules.
- **Watches**: a callback or mod event fires when a condition's result changes for an actor. It is driven by game events plus a 1-second poll.
- **Extensible**: other plugins can register their own condition types from C++ or through the C API.

## Build modes

| Context | Output | Notes |
|---|---|---|
| Built on its own (`cmake --preset build-release-msvc`) | `ConditionEngine.dll`, an SKSE plugin | Exports the C API and registers the Papyrus natives |
| `add_subdirectory(ConditionEngine)` from a host | `condition-engine-lib`, a static library | The host uses the C++ API directly (`ConditionEngine/ConditionEngine.h`) |

When building on its own, CommonLib is taken from `extern/CommonLibVR-ng`. If that is missing, it falls back to the parent checkout's `../extern/CommonLibVR-ng`.

The unit tests (`condition-engine-tests`) cover the parser, the printer and the registry. They run outside the game.

## Using it from another SKSE plugin (C API)

```cpp
#include <ConditionEngine/ConditionEngineAPI.h>   // link ConditionEngine.lib, or GetProcAddress

char err[256];
CE_Handle h = ConditionEngine_Compile("@IsNight && Weather(Rainy)", err, sizeof err);
bool raining = ConditionEngine_Evaluate(h, actor) == 1;

ConditionEngine_Watch(h, actor, [](CE_WatchHandle, void* a, int32_t result, void*) {
    // main thread, only on change
}, nullptr, /*fireImmediately*/ 1);
```

Custom types are registered with `ConditionEngine_RegisterType("MyMod_IsCursed", fn, ud, CE_DEP_POLLED, "level", nullptr)`. After that, `MyMod_IsCursed(3)` works in any expression, preset or DressCode rule.

## Papyrus

`ConditionEngine.psc` provides `Evaluate`, `EvaluateNamed`, `RegisterNamed`, `Validate`, `Describe`, `ToJson`, `Normalize`, `Watch` and `Unwatch`. Watches send a mod event when their result changes. Script-created watches and names are cleared on every load, so register them again from `OnPlayerLoadGame`.

## Data-only presets

Put `*.json` files in `Data/SKSE/Plugins/ConditionEngine/`:

```json
{ "conditions": { "IsNight": "Time(20, 6)", "BadWeather": "Weather(Rainy) || Weather(Snow)" } }
```

## Expression syntax

```
expr     := or
or       := and  ( "||" | "or"  ) and ...
and      := not  ( "&&" | "and" ) not ...
not      := ("!" | "not") not | primary
primary  := "(" expr ")" | true | false | @Name | Type[(args)] [cmp value]
args     := value | field=value, ...        cmp := == != < <= > >=
value    := number | "string" | 'string' | word | [list] | ${param}
```

Positional arguments fill each type's declared fields in order, and a trailing comparison sets its compare field. For example, `ActorValue(Health, usePercentage=true) < 30` is the same as `{"type":"ActorValue","actorValue":"Health","usePercentage":true,"value":30,"operator":"<"}`.
