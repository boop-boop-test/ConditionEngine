# ConditionEngine

Reusable condition and expression evaluator for Skyrim SE/AE/VR SKSE plugins.
It was extracted from DressCode and has no DressCode dependencies.

- **53 built-in condition types**, in 12 categories:
  - **Vanilla Creation Kit conditions**: `Game(GetLevel) >= 20`, `Game(IsSneaking)`, `Game(WornHasKeyword, ArmorHeavy)`. Any of Skyrim's condition functions works, with parameters converted from their declared types.
  - **Actor**: `Sneaking`, `Sitting`, `InDialogue`, `Bleedout`, `WeaponDrawn`, `IsFollower`, `IsPlayer`, `IsActor`, `Level`, `Sex`, `HasKeyword`, `HasMagicEffect`, `HasPerk`, `HasSpell`, `Race`, `Faction`, `ActorValue`, and the swimming/sleeping/mounted/combat states
  - **Gear**: `WornHasKeyword`, `HasItem`, `ItemCount` (an item, or every item with a keyword), `EquippedWeaponType`, `HasSlotsEquipped`
  - **World**: `Worldspace`, `Hold`, `InLocation`, `InCell`, location type and keywords, interior/exterior, weather, time, date, `Holiday`, temperature
  - **Mods and scripts**: `IsPluginLoaded`, globals, quests, Papyrus script properties
  - **Logic**: seeded random, and/or/not, and references to named conditions
- **Expression syntax** that compiles to the same JSON trees DressCode stores:
  `Time(20, 6) && !InteriorExterior(true) || @BadWeather`
- **Named conditions**, which you reference as `@Name`. They can come from data-only preset files, from Papyrus, from other plugins, or from host resolvers such as DressCode rules.
- **Watches**: a callback or mod event fires when a condition's result changes for an actor. Checking runs in two tiers:
  - **Every second, cheaply:** compare the cell, location, game minute, weather and actor flags, and re-check only the watches that depend on what changed. Game events do the same: equip, inventory, magic effects, combat, cell load, sleep and wait, menus, and game load.
  - **A full poll every 10 seconds:** for state with no change signal. The interval is set in `ConditionEngine.ini` (`fPollIntervalSeconds`) or with `SetPollInterval`, and each watch can override it.
- **Self-describing types**: every type publishes a schema (its fields, value types, labels, defaults, allowed values and category). The catalog (`ConditionEngine_GetCatalog`, Papyrus `GetCatalog`) and a generated JSON Schema (`ConditionEngine_GetJsonSchema`) let editors and other mods offer any condition without knowing how it works.
- **Extensible**: other plugins can register their own condition types, with schemas, from C++ (`TypeBuilder`) or through the C API (`ConditionEngine_RegisterTypeEx`). Those types then appear in DressCode's editors automatically.

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

For per-watch settings (API v2), use `CE_WatchOptions` with `ConditionEngine_WatchEx`. It takes a poll interval (`CE_POLL_DEFAULT`, `CE_POLL_NEVER` or a number of milliseconds) and extra triggers, such as `CE_DEP_LOCATION` to re-check on every cell transition. `ConditionEngine_Refresh(watch)` forces a check.

Custom types are registered with `ConditionEngine_RegisterType("MyMod_IsCursed", fn, ud, CE_DEP_POLLED, "level", nullptr)`. After that, `MyMod_IsCursed(3)` works in any expression, preset or DressCode rule.

## Papyrus

`ConditionEngine.psc` provides `Evaluate`, `EvaluateNamed`, `RegisterNamed`, `Validate`, `Describe`, `ToJson`, `Normalize`, `Watch` (with an optional poll interval), `Unwatch`, `RefreshWatch` and `SetPollInterval`/`GetPollInterval`. Watches send a mod event when their result changes. Script-created watches and names are cleared on every load, so register them again from `OnPlayerLoadGame`.

## Settings (`Data/SKSE/Plugins/ConditionEngine.ini`)

- `[Watcher] fPollIntervalSeconds = 10` and `fDetectIntervalSeconds = 1`
- `[Temperature] sGlobal`, `fScale` and `fOffset`: read `Temperature(...)` from a global set by a survival mod

## Notes

- **Evaluation must run on the main thread:** `Game(...)` conditions call the engine's own condition evaluator. Watches already run there, and Papyrus `Evaluate`/`EvaluateNamed` are latent: they evaluate on the next frame on the main thread, then return. C API callers must call `Evaluate` from the main thread themselves.
- **Editor IDs** only resolve for form types the game keeps them for (keywords, races, globals, cells, worldspaces, quests). Use form strings (`"0x000BE127|Skyrim.esm"`) for perks, spells, effects, actors and locations, unless powerofthree's Tweaks is installed. Places can also match by display name.

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


## Integration availability (API v4)

Availability is separate from boolean matching and configuration validation. Types remain registered
when their dependencies are missing, so saved expressions still compile and round-trip.

Declare `requiresPlugins: ["SomeMod.esp"]` in a RegisterTypeEx schema, or use
`TypeBuilder::RequiresPlugins` in C++. Full and light plugins are snapshotted after data loads and on
new-game/save-load events. Form/formList fields using portable `0x...|Plugin.esp` keys also require
their owner plugin. `IsPluginLoaded("SomeMod.esp")` itself remains available; it is a presence test.
Bare editor IDs do not identify a plugin dependency; use portable keys or explicit requirements.

`ConditionEngine_GetAvailability(handle, buffer, size)` reports `{state, available, reason}` with
states `available`, `pending`, and `unavailable`. The catalog includes the same status per type plus
`requiresPlugins` and `availabilityRevision`. Catalog/snapshot reads never access game state.

Hosts publish runtime readiness through `ConditionEngine_SetTypeAvailability(type, state, reason)`
(0 pending, 1 available, 2 unavailable). Available clears the reason; identical publications do not
notify. `ConditionEngine_SubscribeAvailability` returns a subscription token; unsubscribe with
`ConditionEngine_UnsubscribeAvailability`. Notifications include a revision, run outside locks on
the publishing thread, and can query snapshots. Marshal game/UI work to the main thread, and
synchronize in-flight callbacks before unloading. The C++ equivalent is `Availability`.
Named-definition and type registration changes also invalidate availability. Hosts that mutate a
named resolver's definitions directly call `Availability::Invalidate` (static consumers).

Evaluate/EvaluateNamed, Papyrus evaluation, and watches suspend a whole expression if any dependency
is unavailable or pending. This includes NOT and OR: absence must not become a positive match through
inversion. C++ consumers use `Condition::EvaluateAvailable` at rule boundaries for the same policy.
GetAvailability explains the suspension; legacy boolean evaluation still returns 0. Watches include
CE_DEP_AVAILABILITY and are rechecked on the main thread when readiness changes. Conditions are not
rewritten and user enablement is never changed. Named references propagate status with cycle guards.
This initial status is global dependency readiness; actor-specific sample failures still belong to
the integration's value reader.
