# Survival needs

ConditionEngine has three shared survival values built in: **hunger**, **fatigue** (need for sleep) and **cold**.
Each one reads the first survival mod in a fixed order that tracks the actor, and gives one answer on one scale. A
mod, preset or OAR config can ask "how hungry, tired or cold is this actor" without knowing which survival mod is
installed.

The definitions are compiled into `ConditionEngine.dll` (`presets/ConditionEngine_Survival.json` in the
repository), so every copy of the DLL has them, including copies bundled with other mods. A preset file in
`Data/SKSE/Plugins/ConditionEngine/` that defines a value of the same name replaces the built-in one.

## What you can read

| Name | What it is |
|---|---|
| `$hunger`, `$fatigue`, `$cold` | 0-100 per actor. 0 means fed / rested / warm; 25, 50 and 75 are where mild, moderate and severe start; 100 is the mod's maximum |
| `$hunger_stage`, `$fatigue_stage`, `$cold_stage` | 0 none, 1 mild, 2 moderate, 3 severe |
| `@HungerMild`, `@HungerModerate`, `@HungerSevere` | at least that stage (`$hunger >= 25` / `50` / `75`) |
| `@FatigueMild`, `@FatigueModerate`, `@FatigueSevere` | the same for fatigue |
| `@ColdMild`, `@ColdModerate`, `@ColdSevere` | the same for cold |
| `$sunhelm_hunger`, `$frostfall_cold`, ... | one source on its own (table below) |

```papyrus
Float hunger = ConditionEngine.GetValue(akActor, "hunger")             ; 0-100, 0 when nothing tracks the actor
String source = ConditionEngine.GetValueSource(akActor, "hunger")      ; "sunhelm_hunger", or "" when no source
Bool starving = ConditionEngine.Evaluate(akActor, "@HungerSevere")
```

```cpp
CE_ValueHandle cold = ConditionEngine_CompileValue("cold", err, sizeof err);
char reason[128];
ConditionEngine_EvaluateValue(cold, actor, &sample, reason, sizeof reason);   // reason: "from frostfall_cold"
```

When no source tracks the actor, the value is `CE_VALUE_UNAVAILABLE` with its fallback 0, the reason lists every
source and why it did not answer, and the conditions are false. In OAR, `ConditionEngine` with the preset
`HungerSevere` or `ConditionEngineExpression` with `$cold >= 50` work the same way.

## One scale

Every survival mod counts differently: Survival Mode uses 0-1000, SunHelm 0-360 for hunger, Last Seed and Frostfall
0-120, iNeed counts fullness down from 110. Each source maps the mod's own stage boundaries onto 25 / 50 / 75 and
its maximum onto 100, linearly in between (`piecewise`). So mild is always the mod's first bad stage (peckish,
slightly tired, chilly), whatever its numbers are. Where the mod has two stages past severe (weak and starving,
freezing and numb), they share the top quarter.

## Which source answers

Per actor, the highest-priority source whose mod is installed, running and tracking that actor. A source whose plugin
isn't loaded is skipped without being read.

| Priority | Hunger | Fatigue | Cold | Actors |
|---|---|---|---|---|
| 90 | | | Frostfall | player |
| 80 | SunHelm | SunHelm | SunHelm | player |
| 70 | Last Seed | Last Seed | | player |
| 65 | iNeed | iNeed | | player |
| 60 | Survival Mode | Survival Mode | Survival Mode | player |
| 50 | SeverActions | SeverActions | SeverActions | the followers SeverActions tracks |

Why this order:

- **Dedicated survival mods before Survival Mode.** Survival Mode ships with the Anniversary Edition, so its plugin is
  loaded in most games. A player who also installed SunHelm, Last Seed or iNeed chose that mod, and those mods are
  meant to replace Survival Mode (SunHelm ships its own `Survival_MainScript` to take over Survival Mode's start).
- **Frostfall first for cold.** Frostfall is a full exposure simulation and has no hunger or fatigue. It's often
  paired with Last Seed or iNeed, which have no cold, so the player can get hunger from one mod and cold from the other.
- **SunHelm, Last Seed, iNeed** don't support being installed together. The order only decides a broken setup, and
  it favours the finer readings: SunHelm and Last Seed track a continuous need, while iNeed only counts time once
  you're empty.
- **SeverActions** tracks followers only, and the player sources only answer for the player, so its priority never
  competes. It sits lowest so a follower-tracking source added later can rank above it.

Ties break by name. While a higher source is pending (an asynchronous read has no result yet), the combined value
stays pending instead of switching between mods.

## Sources

Every form is referenced by form string (`0xID|Plugin`), checked against the plugin files listed. Stage numbers
come from the mods' scripts and quest script properties.

### Survival Mode (Creation Club, `ccqdrsse001-survivalmode.esl`): player

On while `Survival_ModeEnabled` (`0x826`) is 1. Survival Mode keeps the player's needs in globals that apply to no
one else, so it answers for the player only.

| Need | Global | Range | Mild / moderate / severe (stages) | 100 |
|---|---|---|---|---|
| Hunger | `Survival_HungerNeedValue` `0x81A` | 0-1000 | 160 peckish / 340 hungry / 520 weak (770 starving) | 1000 |
| Fatigue | `Survival_ExhaustionNeedValue` `0x816` | 0-960 | 160 slightly tired / 340 tired / 560 weary (800 debilitated) | 960 |
| Cold | `Survival_ColdNeedValue` `0x81B` | 0-1000 | 120 chilly / 300 very cold / 500 freezing (800 numb) | 1000 |

Stage values are set in `Survival_NeedHunger` / `Survival_NeedExhaustion` / `Survival_NeedCold`
(`SetNeedStageValues`). A stage applies from its value up to the next one.

### SunHelm Survival 3.1.4 (`SunHelmSurvival.esp`): player

On while `_SHEnabled` (`0x2EB63`) is 1 and that need's system quest runs. SunHelm stops a system's quest when that
need is turned off (`StopSystem`).

| Need | Global | Quest | Mild / moderate / severe | 100 |
|---|---|---|---|---|
| Hunger | `_SHCurrentHungerLevel` `0xEAAE` | `_SHHungerQuest` `0x8EE8` | 80 peckish / 140 hungry / 240 ravenous (360 starving) | 360 (the cap) |
| Fatigue | `_SHCurrentFatigueLevel` `0x21E3F` | `_SHFatigueQuest` `0x1E844` | 160 slightly tired / 240 tired / 360 weary (480 exhausted) | 480 (the cap) |
| Cold | `_SHCurrentColdLevel` `0x6A13C5` | `_SHColdQuest` `0x6A13C2` | 150 chilly / 250 cold / 450 freezing (700 frigid) | 900 (`_SHColdLevelCap`) |

Thresholds are the `_SH*Stage1-5` properties on those quests' scripts.

### Last Seed 5.3 (`LastSeed.esp`): player

On while the need's setting is 2 (on) and its system quest runs. The quest only runs after Last Seed is started
from its menu or a first sleep.

| Need | Global | Setting / quest | Mild / moderate / severe | 100 |
|---|---|---|---|---|
| Hunger | `_Seed_AttributeHunger` `0x29CD` | `_Seed_Setting_SystemEnabled_Hunger` `0x7073` / `_Seed_HungerSystemQuest` `0x29CC` | 40 hungry / 60 very hungry / 80 ravenous (100 starving) | 120 |
| Fatigue | `_Seed_AttributeFatigue` `0x29CF` | `_Seed_Setting_SystemEnabled_Fatigue` `0x7075` / `_Seed_FatigueSystemQuest` `0x29D2` | 40 tired / 60 very tired / 80 exhausted (100 severely exhausted) | 120 |

Levels every 20 (`ATTR_LEVEL_1-5` in `_Seed_AttributeSystem`). Last Seed has no cold of its own; it uses Frostfall.
It also tracks hunger for up to three Campfire-tracked followers, in per-slot globals
(`_Seed_AttributeHunger_Follower1-3`). Those aren't mapped, because the slot-to-actor link lives in Campfire's
Papyrus state.

### iNeed 1.95 (`iNeed.esp`): player

On while `_SNQuest` (`0xD62`) runs and the need's rate on its `_SNQuestScript` is above 0 (a rate of 0 turns that
need off). iNeed counts fullness down: `HungerState` / `FatigueState` run 110 (full) to 0. At 0 it counts the hours
spent there in a penalty global.

| Need | Reading | Mapping |
|---|---|---|
| Hunger | `_SNQuestScript.HungerState`, `_SNHungerPenalty` `0x12DB` | fullness 110 → 0, 50 (hungry) → 25, 0 → 50; penalty 1 / 2 / 3 (starving, longer) → 50 / 75 / 100 |
| Fatigue | `_SNQuestScript.FatigueState`, `_SNFatiguePenalty` `0x12DC` | the same: rest 50 (tired) → 25, empty → 50, penalty 1 / 2 / 3 → 50 / 75 / 100 |

iNeed's followers only eat and drink on a timer. It keeps no per-follower need, so iNeed answers for the player only.

### Frostfall 3.4.1 (`Frostfall.esp`, with Campfire): player

On while `FrostfallRunning` (`0x6DCFB`) and `_Frost_Setting_ExposureOn` (`0x68113`) are 2. Exposure
`_Frost_AttributeExposure` (`0x183D`) runs 0-120, levels every 20 (`EXPOSURE_LEVEL_1-5` in `_Frost_ExposureSystem`):
warm, comfortable, 40 chilly / 60 very cold / 80 freezing (100 freezing to death) map to 25 / 50 / 75, 120 to 100.
Campfire on its own has no exposure; it only matters as Frostfall's requirement.

### SeverActions (`SeverActions.esp`, `SeverActionsNative.dll`): its followers

On for actors SeverActions' survival system tracks (`Survival_IsTracked`), while survival and that need are on and the
actor isn't excluded. It already reads 0-100 with levels at 25 / 50 / 75 (peckish / hungry / ravenous, tired /
drained / exhausted, chilly / cold / freezing), read through `SeverActionsNative.Native_Survival_GetHunger` /
`GetFatigue` / `GetCold`. It re-reads on `SeverActions_Tick_Survival` and `SeverActionsNative_FoodConsumed`.

## Adding a source

Another mod adds its own source in its own preset. ConditionEngine doesn't need a change:

```json
{ "values": { "mymod_hunger": {
    "expr": "piecewise(GlobalVariable(\"0x801|MyMod.esp\"), 0, 0, 30, 25, 60, 50, 90, 75, 100, 100)",
    "enabledWhen": "IsPlayer && GlobalVariable(\"0x802|MyMod.esp\") == 1",
    "requiresPlugins": ["MyMod.esp"], "provides": "hunger", "priority": 75 } } }
```

Map your stages onto 25 / 50 / 75 and pick a priority from the table. Gate player-only globals on `IsPlayer`, or
every NPC reads the player's needs.

## Limits

- **Player only, except SeverActions.** None of the other mods keeps per-actor needs that ConditionEngine can read.
  Last Seed's follower hunger is the exception: it's tracked in per-slot globals but not mapped (above).
- **Polled.** Globals and script properties have no change event. Watches re-read them at the poll interval (10 s by
  default, set per watch), except SeverActions, which is refreshed on its own events.
- **Form IDs are the mods' own.** A plugin that was compacted and ESL-flagged (ESLify) has new IDs, and its source
  reads unavailable. The temperature condition `Temperature(...)` is unrelated: it reads one global set in
  `ConditionEngine.ini`.
