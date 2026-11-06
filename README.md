# ConditionEngine

Reusable condition and expression evaluator for Skyrim SE/AE/VR SKSE plugins. MIT licence (`LICENSE`).

How-to guides (adding, reading and watching conditions, registering your own types, values, OAR) are in [`docs/`](docs/README.md).

- **53 built-in condition types**, in 12 categories:
  - **Vanilla Creation Kit conditions**: `Game(GetLevel) >= 20`, `Game(IsSneaking)`, `Game(WornHasKeyword, ArmorHeavy)`. Any of Skyrim's condition functions works, with parameters converted from their declared types.
  - **Actor**: `Sneaking`, `Sitting`, `InDialogue`, `Bleedout`, `WeaponDrawn`, `IsFollower`, `IsPlayer`, `IsActor`, `Level`, `Sex`, `HasKeyword`, `HasMagicEffect`, `HasPerk`, `HasSpell`, `Race`, `Faction`, `ActorValue`, and the swimming/sleeping/mounted/combat states
  - **Gear**: `WornHasKeyword`, `HasItem`, `ItemCount` (an item, or every item with a keyword), `EquippedWeaponType`, `HasSlotsEquipped`
  - **World**: `Worldspace`, `Hold`, `InLocation`, `InCell`, location type and keywords, interior/exterior, weather, time, date, `Holiday`, temperature
  - **Mods and scripts**: `IsPluginLoaded`, globals, quests, Papyrus script properties
  - **Logic**: seeded random, and/or/not, and references to named conditions
- **Expression syntax** that compiles to JSON condition trees and prints back to text:
  `Time(20, 6) && !InteriorExterior(true) || @BadWeather`
- **Named conditions**, which you reference as `@Name`. They can come from data-only preset files, from Papyrus, from other plugins, or from host resolvers that own their own rules.
- **Watches**: a callback or mod event fires when a condition's result changes for an actor. Checking runs in two tiers:
  - **Every second, cheaply:** compare the cell, location, game minute, weather and actor flags, and re-check only the watches that depend on what changed. Game events do the same: equip, inventory, magic effects, combat, cell load, sleep and wait, menus, and game load.
  - **A full poll every 10 seconds:** for state with no change signal. The interval is set in `ConditionEngine.ini` (`fPollIntervalSeconds`) or with `SetPollInterval`, and each watch can override it.
- **Self-describing types**: every type publishes a schema (its fields, value types, labels, defaults, allowed values and category). The catalog (`ConditionEngine_GetCatalog`, Papyrus `GetCatalog`) and a generated JSON Schema (`ConditionEngine_GetJsonSchema`) let editors and other mods offer any condition without knowing how it works.
- **Values**: the number a numeric type compares, read on its own (`ActorValue(Health, usePercentage=true)`, `Level`, `Game(GetLevel)`, `PapyrusCall(...)`), with arithmetic (`$hunger * (1 - $light)`, `clamp`, `min`, `max`, `abs`, `lerp`, `piecewise`, `steps`). Named values (`$name`) come from presets, plugins or scripts, can be pushed from Papyrus (`SetValue`), and can be watched with a non-blocking per-frame read. Conditions compare them: `$vampire_hunger > 80`.
- **Shared survival needs**, built in: `$hunger`, `$fatigue` and `$cold` (0-100, with mild / moderate / severe at 25 / 50 / 75), `$hunger_stage` etc. (0-3) and `@HungerMild` ... `@ColdSevere`. Each reads the first installed survival mod that tracks the actor: Frostfall, SunHelm, Last Seed, iNeed or Survival Mode for the player, SeverActions for its followers. Papyrus `GetValueSource` says which one answered. The source order and what each mod maps to are in [Survival needs](docs/survival.md).
- **Warmth, coverage, dirt and blood**, built in: `$warmth` and `$coverage` (how warmly dressed, how well rain and snow are kept off) for any actor from the worn armor on Frostfall's scale (`ArmorWarmth(Warmth)`), with Survival Mode's warm and cold armor keywords for vanilla gear and Frostfall's totals for the player; `$dirt` and `$blood` from Bathing in Skyrim (player and tracked bathers), Dirt and Blood (player, bandits, laborers, wounded NPCs) or Keep It Clean; `@WarmthPoor`, `@OutsideUnderdressed`, `@DirtSevere`, ... See [Survival needs](docs/survival.md#warmth-and-coverage).
- **Weather from any weather mod**, built in: `$weather_rain`, `$weather_snow`, `$weather_storm`, `$weather_fog`, `$weather_wind`, `$weather_ash`, `$weather_sand` and more (0-100, light / moderate / heavy at 25 / 50 / 75), `@WeatherRain`, `@WeatherBlizzard`, `@OutsideInRain` ... and `$season`. Each weather is read from its own record (classification, particles, wind, lightning, fog, editor ID), so vanilla and modded weathers alike work. It was checked against Obsidian, Cathedral, Azurite, Vivid, True Storms, Climates of Tamriel, Rustic and Aequinoctium. `WeatherKind(Rain) >= 75` reads one kind, and `Weather(...)` also takes `Stormy`, `Blizzard`, `Foggy`, ... or an editor ID glob. See [Weather](docs/weather.md).
- **Dials**: values that rules drive, with state. An event switches one on and another off, a condition sets it to 10 and a rate lowers it 1 a second (or by `$hunger`), a timer rolls a daily chance, a key cycles named modes; "for 30 minutes" effects undo themselves. Global, player only, or one per actor; saved with the game. Every dial is a value (`$manic`) and names its own conditions (`@Manic`). Mods ship them in a preset file's `"dials"` section, scripts and plugins define them at run time, players make them in the editor. See [Dials](docs/dials.md).
- **SkyrimNet**: prompts read every dial with `{{ get_dials(npc.UUID).manic }}` and every named condition and value with `condition_engine(npc.UUID)`. Dial rules react to SkyrimNet events (`"skyrimnet": "dialogue"`), write memories (`"tell"`), give NPCs world knowledge while a condition holds, and offer AI actions (`"aiAction"`). See [SkyrimNet](docs/skyrimnet.md).
- **An in-game editor** (Magelight UI, `ce ui`): dials, named conditions and named values in three tabs. Conditions are picked from lists (a condition, a value and a level, joined with AND / OR), with "Edit as text" for the rest. The built-in items and every mod's are read only, with "Make a copy"; live values, "Why?", and copy-ready lines for SkyrimNet prompts, expressions and Papyrus.
- **Extensible**: other plugins can register their own condition types, with schemas, from C++ (`TypeBuilder`) or through the C API (`ConditionEngine_RegisterType`). Those types then appear in any editor that reads the catalog.

## Requirements (players)

- **Skyrim SE, AE or VR.** Built with CommonLibVR NG (alandtse/CommonLibVR, `ng` branch, MIT): one DLL for the
  runtimes. Tested in game on AE 1.6.1170; SE 1.5.97 and VR are untested. **1.7.99+ support is experimental**: it
  loads and its basics were checked once on 1.7.104, but it is not tested in play.
- **SKSE** for your game build (2.3.1+ on 1.7.104) and **Address Library for SKSE Plugins** (v13+ on 1.7.x; the VR
  Address Library on VR).
- Optional: **PapyrusUtil** for the `StorageUtil` values; `Data/SKSE/Plugins/ConditionEngine.ini` and data-only presets
  in `Data/SKSE/Plugins/ConditionEngine/`.
- Usually installed by a mod that bundles it (No Talk Just Hands). No Talk Just Hands uses a copy that is
  already loaded or installed standalone (`Data/SKSE/Plugins/ConditionEngine.dll`) before its own bundled one.

## Building (developers)

- Windows x64, **Visual Studio 2022** with the "Desktop development with C++" workload (C++23, MSVC; clang-cl presets
  exist but are not used), **CMake 3.21+** and **Ninja** (both ship with VS 2022's C++ CMake tools).
- **vcpkg** with `VCPKG_ROOT` set: the presets use its toolchain, the `x64-windows-skse` triplet from `cmake/`, and
  `vcpkg-configuration.json` adds the colorglass registry (skse and script-source ports).
- **CommonLibVR-ng**: `extern/CommonLibVR-ng`, or a parent checkout's `../extern/CommonLibVR-ng` (the
  fallback when this repo sits inside another project), or any checkout given with
  `-DCONDITIONENGINE_COMMONLIB_DIR=<path>`. CommonLibVR `ng` v4.7.1 (MIT) builds SE 1.5.97, AE 1.6.x and VR.
  Skyrim SE 1.7.99+ needs a CommonLib with 1.7 support (the format 5 Address Library, SKSE 2.3's plugin flag and the
  1.7 structure changes). Released ConditionEngine DLLs are built with such a CommonLib (MIT); it is not part of this
  repository.
- Build: `cmake --preset build-release-msvc`, then `cmake --build build/release-msvc`; the post-build step copies the
  DLL to `contrib/Distribution/PluginRelease/` (No Talk Just Hands bundles it from there, `CONDITIONENGINE_DIST`).
- Papyrus (`ConditionEngine.psc`): `Release.ppj` imports the SKSE / Skyrim script sources from the parent project's
  build (`../build/release-msvc/vcpkg_installed/x64-windows-skse/contrib/papyrus`); compile with pyro.
- Tests: `condition-engine-tests` (built when this is the top-level project); no game needed.
- Licence: MIT (`LICENSE`); bundling mods ship it beside the DLL.

## Build modes

| Context | Output | Notes |
|---|---|---|
| Built on its own (`cmake --preset build-release-msvc`) | `ConditionEngine.dll`, an SKSE plugin | Exports the C API, registers the Papyrus natives and, with OAR installed, the OAR conditions |
| `add_subdirectory(ConditionEngine)` from a host | `condition-engine-lib`, a static library | The host uses the C++ API directly (`ConditionEngine/ConditionEngine.h`) |

When building on its own, CommonLib is taken from `extern/CommonLibVR-ng`. If that is missing, it falls back to the parent checkout's `../extern/CommonLibVR-ng`.

The unit tests (`condition-engine-tests`) cover the parser, the printer and the registry. They run outside the game.

## Using it from another SKSE plugin (C API)

```cpp
#include <ConditionEngine/ConditionEngineAPI.h>   // link ConditionEngine.lib, or GetProcAddress

char err[256];
CE_Handle h = ConditionEngine_Compile("@IsNight && Weather(Rainy)", err, sizeof err);
bool raining = ConditionEngine_Evaluate(h, actor) == 1;

CE_WatchOptions options{sizeof options};
options.fireImmediately = 1;
ConditionEngine_Watch(h, actor, [](CE_WatchHandle, void* a, int32_t result, void*) {
    // main thread, only on change
}, nullptr, &options);
```

**Evaluation order**: every type has a cost class (0 constant, 1 snapshot read, 2 engine read, 3 `Game(...)`, 4 script property, 5 `PapyrusCall` / `StorageUtil`; C API types declare `"cost"`, default 3). And / Or groups evaluate their children cheapest first, skip duplicates, and fold constants (`false && x` is false without reading `x`); the written order is kept for saving and display. Literal math is computed once.

**List watches** (`ConditionEngine_WatchList`): an ordered list of conditions for one actor; the winner is the first true one (`ReadListWatch` from any thread, a callback when it changes). Only what can move the result is evaluated after a change: touched rules above the winner, then the winner, and the rules below it only when it turns false. A pending rule above the winner holds it. Value changes are keyed, so pushing `$hunger` for Serana wakes only Serana's watches that read hunger.

The API is version 1 (`ConditionEngine_GetAPIVersion`) and prerelease: it may still change until the first release. `CE_WatchOptions` takes a poll interval (`CE_POLL_DEFAULT`, `CE_POLL_NEVER` or a number of milliseconds) and extra triggers, such as `CE_DEP_LOCATION` to re-check on every cell transition. `ConditionEngine_Refresh(watch)` forces a check.

Custom types are registered with a schema: `ConditionEngine_RegisterType(R"({"name": "MyMod_IsCursed", "source": "MyMod", "fields": [{"key": "level", "type": "int"}]})", fn, ud, CE_DEP_POLLED, err, sizeof err)`. After that, `MyMod_IsCursed(3)` works in any expression or preset.

## Values

A value is a number read from the game: any numeric type without its comparison, a named value, a condition read as 1 / 0, or arithmetic over those.

```cpp
CE_ValueHandle v = ConditionEngine_CompileValue("clamp($vampire_hunger, 0, 100) / 100", err, sizeof err);

CE_ValueWatchOptions opts{sizeof opts};
opts.pollIntervalMs = 250;                       // >= one frame; CE_POLL_NEVER = events only
CE_WatchHandle w = ConditionEngine_WatchValue(v, actor, nullptr, nullptr, &opts);   // no callback: just keep it fresh

CE_ValueSample s{sizeof s};
ConditionEngine_ReadValueWatch(w, &s);           // any thread, never blocks: s.value, s.status, s.ageMs
```

- **Status**: `CE_VALUE_OK`, `PENDING` (no result yet, e.g. an async call), `UNAVAILABLE` (missing plugin/form, actor not loaded, `enabledWhen` false, nothing pushed, unknown `$name`), `ERROR`. A comparison with a side that is not OK is false.
- **Watches** sample at their interval (down to one frame) and on their dependencies; watches of the same value and actor share one sample. With a callback they fire on a step (`minStep`), on threshold crossings (`thresholds`, `hysteresis`) or on any change.
- **Named values** resolve by name on every read, so redefinitions reach existing handles and watches. `ConditionEngine_RegisterNamedValue`, presets (`"values"`), or Papyrus `RegisterNamedValue`.
- **Pushed values**: `ConditionEngine_SetValue(actor, name, value, timeoutSeconds)` / `SetGlobalValue`, or Papyrus `SetValue`. Pushing an undeclared name declares it. Pushes are never saved; every load starts clean.
- **PapyrusCall** reads a number returned by a Papyrus function with the actor as its argument, asynchronously: `PapyrusCall(SeverActionsNative, Native_Survival_GetHunger, interval=2)`, or a method on a form's script with `form="0xD62|SeverActions.esp"`. Extra arguments after the actor: `args=["hunger", 0.0]` (strings, ints, floats, bools). Reads return the latest result and dispatch at most one call per function, arguments and actor.
- **StorageUtil** reads a PapyrusUtil StorageUtil number stored on the actor: `StorageUtil(hunger) > 50`, `StorageUtil(mood, kind=int)`, or global storage with `global=true`. It is a PapyrusCall underneath (asynchronous, cached, cost 5) and reads Unavailable without PapyrusUtil.
- **External value types**: `ConditionEngine_RegisterValueType(schema + "value": {unit, range}, fn, ...)`. The type reads as a value and compares as a condition (`MyMod_Fear > 0.5`). Signal changes with `ConditionEngine_NotifyEvent(CE_DEP_VALUE, actor)`.
- **Combined values (providers)**: a value with `"provides": "hunger", "priority": 50` is a source for `$hunger`. Per actor, `$hunger` reads its highest-priority available source (ties by name), then its own `expr` if it has one. A pending higher source keeps it pending (no flipping between mods while one loads); nothing available reads its fallback, or unavailable listing the sources tried. The reason buffer says which source answered (`from severactions_hunger`). Each mod adds its own source in its own preset.
- **Reasons**: `ConditionEngine_ReadValueWatch` / `EvaluateValue` also return why a sample is not OK; external types report theirs through their callback's reason buffer. Named values can require native DLLs (`"requiresDlls": ["SomeNative.dll"]`).
- **Created actors**: watches on created references (`0xFF...`) stop at the next game load, since that FormID may name a different actor in another save. Value watches then read Unavailable; watch again after loading.
- **Three-valued enabledWhen**: while an `enabledWhen` condition cannot be decided (an async part has no result yet) the value reads `PENDING` with what it waits for, not "not enabled". `false && pending` is still decided.

## Explaining a result

"Why is this false?" `ConditionEngine_Explain(handle, actor, asText, buf, size)` (and `ConditionEngine_ExplainValue`) evaluates the way the engine does, in the same cheapest-first order with the same short-circuits, and returns each part's outcome:

```
[F] $hunger > 80 && !Sneaking
  [F] $hunger > 80
    [ok] $hunger = 42  (from severactions_hunger)
      [ok] source, priority 50: $severactions_hunger = 42
    [ok] 80.0 = 80
  [-] !Sneaking  [not read: a cheaper part decided the group]
```

Markers: `[T]` true, `[F]` false, `[P]` pending, `[U]` unavailable, `[-]` skipped, `[ok]` / `[E]` for values. With `asText` 0 it is JSON (`expression`, `state`, `reason`, `value`, `from`, `note`, `role`, `children`) for a UI. Named conditions (`@name`) and named values (`$name`) expand into their definitions, with `enabledWhen` and every provider. Main thread only. Papyrus: `ConditionEngine.Explain(akActor, "expression")` (latent).

## Console

In game, `ce` in the console (it takes over the unused vanilla `TestCode` command; change or turn that off in the ini):

- `ce eval <expression>`: explain a condition or value for the selected actor (click one), else the player
- `ce evalfor <actor> <expression>`: the same for `player`, `selected`, a form ID (`14`, `0x14|Skyrim.esm`), an editor ID or a loaded actor's name
- `ce list [filter]`: named values with their current numbers and named conditions with their states
- `ce stats [on|off|reset]`: watches, tick times, Papyrus calls and the costliest types (`on` times evaluations)
- `ce weather`: the current weather, what each kind reads and why (and the outgoing weather during a change)
- `ce warmth`: each worn item and the warmth and coverage it adds
- `ce dials [help]`: every dial and its value; `ce dials set|add|reset|toggle|cycle <dial> [n]`, `ce dials signal <event> [n]`, `ce dials log`, `ce dials info <dial>`, `ce dials reload`
- `ce dials skyrimnet [dial]`: what `get_dials()` answers SkyrimNet for the selected actor
- `ce ui [name]`: open the editor (Magelight UI); Shift+F10 also opens it, changeable in the editor or through InputManager

Quote an expression that has commas: `ce eval "Time(20, 6) && $hunger > 50"`.

## Open Animation Replacer

With Open Animation Replacer (OAR) installed, `ConditionEngine.dll` adds two OAR conditions, usable in any submod's `config.json` and in OAR's in-game editor:

- `ConditionEngine`: the text `Preset` names a named condition (a preset, or one registered by a script or another plugin). A leading `@` is optional.
- `ConditionEngineExpression`: the text `Expression` is any expression, compiled once per distinct text.

```json
{
    "condition": "ConditionEngine",
    "requiredPlugin": "ConditionEngine",
    "requiredVersion": "1.2.0.0",
    "Preset": "NTJH.Talk.Guarded"
}
```

```json
{
    "condition": "ConditionEngineExpression",
    "requiredPlugin": "ConditionEngine",
    "requiredVersion": "1.2.0.0",
    "Expression": "@IsNight && !Sneaking"
}
```

- The subject is the actor OAR evaluates for; non-actors read false. `negated` and `disabled` work as for any OAR condition.
- A missing preset, an invalid expression, or a condition whose plugins or integrations are unavailable reads false; OAR's editor shows why next to the condition.
- OAR evaluates conditions inside the animation update, mostly on worker threads, and ConditionEngine evaluates on the main thread. Off the main thread a condition therefore reads the latest result of a watch ConditionEngine keeps for that text and actor (re-checked on its events and polls, like any watch). The first evaluation for an actor reads false and starts the watch; pairs not evaluated for a minute are dropped.
- Without OAR nothing registers; the log says so in one line.

## Catalog organisation

- **Categories are topical and shared**: `survival` means survival whoever registered it. `ConditionEngine_RegisterCategory({"id", "label", "parent", "order", "icon", "description"}, owner, ...)`, or `"categories": [...]` in a preset. Registering an existing id is reuse: the first registrant keeps the label, later ones only fill in a missing description, icon or parent. `parent` makes a tree; cycles are refused.
- **Nothing is dropped or regrouped**: an item that names a category nobody registered gets that category created at the top level, labelled from its id (`relationships` becomes "Relationships").
- **Origin**: every item carries `source` (built-ins: `ConditionEngine`) and `sourceLabel`; `ConditionEngine_RegisterSource("NoTalkJustHands", "No Talk Just Hands")` or `"sources": {...}` in a preset sets the label. Types registered without a `"source"` take the calling module's file name, with a warning. **Pickers show the origin next to each item**, e.g. "Vampire hunger · SeverActions".
- **Offer** (metadata, never enforced): `everywhere` (default), `owner` (offered only to pickers asking as the owner), `nowhere` (expression-only; `hidden` means the same). Everything compiles and evaluates for everyone.
- **Tags**: free `tags` plus automatic ones (`condition`, `value`, `per-actor`, `global`, `pushed`, `async`, `requires-plugin`).
- **Named values and conditions** take `category`, `icon`, `description`, `offer`, `tags` and `owner` in their definitions (presets, `RegisterNamedValue`, `ConditionEngine_RegisterNamed`).
- The catalog root lists `categories` and `sources: [{id, label, count}]`. It is small, so pickers fetch it once and filter by category, source, tags and text themselves. `ConditionEngine_GetCatalog({"for": "<owner>", "include": [...], "offered": true}, ...)` resolves owner-only items for a picker; `offered: false` is "show all".

## Papyrus

`ConditionEngine.psc` provides `Evaluate`, `EvaluateNamed`, `RegisterNamed`, `Validate`, `Describe`, `ToJson`, `Normalize`, `Watch` (with an optional poll interval), `Unwatch`, `RefreshWatch` and `SetPollInterval`/`GetPollInterval`. Watches send a mod event when their result changes. Values: `GetValue` (latent), `GetValueSource` (latent: which source answered a combined value), `SetValue`, `SetGlobalValue`, `ClearValue`, `ClearGlobalValue`, `RegisterNamedValue` and `WatchValue` (mod event with the value). `Explain` (latent) returns the explanation text. Script-created watches, names and values are cleared on every load, so register them again from `OnPlayerLoadGame`.

`Dials.psc` handles dials: `GetValue`, `SetValue`, `AddValue`, `Reset`, `Toggle`, `Cycle`, `GetLabel`, `Signal`, `Define`, `Remove`, `Exists`, `List`, `Describe`, `OpenEditor`; changes send the mod event `Dials_Changed`. See [Dials](docs/dials.md).

## Settings (`Data/SKSE/Plugins/ConditionEngine.ini`)

- `[Watcher] fPollIntervalSeconds = 10` and `fDetectIntervalSeconds = 1`
- `[Temperature] sGlobal`, `fScale` and `fOffset`: read `Temperature(...)` from a global set by a survival mod
- `[Console] sReplaceCommand = TestCode`: the vanilla console command `ce` replaces; empty adds no command
- `[Dials] iTickMs`, `fMaxCatchUpHours`, `fTrackSeconds`, `bVerbose`; `[Editor] iHotkey` (Shift+F10), `bPauseGame`, `iVRButton`, `iVRModifier`; `[SkyrimNet] bEnable`, `fRefreshSeconds` ([Dials](docs/dials.md#settings-conditionengineini))

## Notes

- **Evaluation must run on the main thread:** `Game(...)` conditions call the engine's own condition evaluator. Watches already run there, and Papyrus `Evaluate`/`EvaluateNamed` are latent: they evaluate on the next frame on the main thread, then return. C API callers must call `Evaluate` from the main thread themselves.
- **Editor IDs** only resolve for form types the game keeps them for (keywords, races, globals, cells, worldspaces, quests). Use form strings (`"0x000BE127|Skyrim.esm"`) for perks, spells, effects, actors and locations, unless powerofthree's Tweaks is installed. Places can also match by display name.

## Data-only presets

Put `*.json` files in `Data/SKSE/Plugins/ConditionEngine/`:

```json
{
  "conditions": { "IsNight": "Time(20, 6)", "BadWeather": "Weather(Rainy) || Weather(Snow)", "VeryHungry": "$vampire_hunger > 80" },
  "values": {
    "vampire_hunger": {
      "label": "Vampire hunger (SeverActions)", "unit": "%", "range": [0, 100],
      "expr": "clamp(PapyrusCall(SeverActionsNative, Native_Survival_GetHunger, interval=2), 0, 100)",
      "enabledWhen": "PapyrusCall(SeverActionsNative, Native_Survival_IsExcluded, interval=10) == 0",
      "requiresPlugins": ["SeverActions.esp"], "graceSeconds": 30, "fallback": 0
    },
    "hungry_mood": { "pushed": { "default": 0, "timeoutSeconds": 600 }, "range": [0, 1] }
  }
}
```

The survival values ([Survival needs](docs/survival.md)) and weather values ([Weather](docs/weather.md)) are built into the DLL and load before these files, so a file that defines the same name replaces them. A `"weathers"` section corrects how a weather reads: `{"weathers": {"TS_*_Heavy": {"fog": 75}}}`.

Value definitions are value text, or an object with `expr`, `label`, `unit`, `range`, `integer`, `fallback` (read while not OK), `enabledWhen`, `scope` (`auto`/`actor`/`global`; auto is inferred from what the value reads), `pushed`, `graceSeconds` (hold the last good value after a failed read) and `requiresPlugins`.

## Expression syntax

```
expr     := or
or       := and  ( "||" | "or"  ) and ...
and      := not  ( "&&" | "and" ) not ...
not      := ("!" | "not") not | compare
compare  := sum [cmp sum]                    cmp := == != < <= > >=
sum      := term (("+" | "-") term)*
term     := unary (("*" | "/") unary)*
unary    := "-" unary | atom
atom     := "(" expr ")" | true | false | @Name | $name | number | func(expr, ...) | Type[(args)]
func     := min | max | clamp | abs | lerp | piecewise | steps
args     := value | field=value, ...
value    := number | "string" | 'string' | word | [list] | ${param}
```

Positional arguments fill each type's declared fields in order, and a trailing comparison with a literal sets its compare field. For example, `ActorValue(Health, usePercentage=true) < 30` is the same as `{"type":"ActorValue","actorValue":"Health","usePercentage":true,"value":30,"operator":"<"}`. Any other comparison (arithmetic, `$name`, a numeric type on the right) becomes a `Compare` condition over two values. Where a condition is expected a bare value means `!= 0`; where a value is expected a condition reads 1 / 0. Division by zero reads 0.


## Integration availability

Availability is separate from boolean matching and configuration validation. Types remain registered
when their dependencies are missing, so saved expressions still compile and round-trip.

Declare `requiresPlugins: ["SomeMod.esp"]` in a RegisterType schema, or use
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
