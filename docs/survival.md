# Survival needs

ConditionEngine has shared survival values built in: **hunger**, **fatigue** (need for sleep) and **cold**, plus
**warmth** and **coverage** (what the worn clothes keep out) and **dirt** and **blood**. Each one reads the first
mod in a fixed order that tracks the actor, and gives one answer on one scale. A mod, preset or OAR config can ask
"how hungry, tired, cold, warmly dressed or dirty is this actor" without knowing which survival mod is installed.
Warmth and coverage work for any actor, with or without a survival mod: they read the worn armor (see
[Warmth and coverage](#warmth-and-coverage)).

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
| `$warmth`, `$coverage` | 0-100 per actor: 25 poorly dressed, 50 ordinary clothes, 75 warm (furs, a cloak and hood), 100 very warm. Coverage is how well rain and snow are kept off |
| `@WarmthPoor`, `@WarmthGood`, `@CoveragePoor`, `@CoverageGood` | `$warmth < 25`, `$warmth >= 75`, the same for coverage |
| `@OutsideUnderdressed`, `@OutsideGettingSoaked` | outdoors in snow with warmth below 50; outdoors in rain with coverage below 50 |
| `$dirt`, `$dirt_stage`, `$blood` | 0-100 per actor (25 mild, 50 dirty, 75 filthy); stage 0-3 |
| `@DirtMild`, `@DirtModerate`, `@DirtSevere`, `@BloodMild`, `@BloodSevere` | at least that level |
| `$sunhelm_hunger`, `$frostfall_cold`, ... | one source on its own (table below) |

Other mods' pickers (No Talk Just Hands and the like) offer one entry per need: the seven values `$hunger`,
`$fatigue`, `$cold`, `$warmth`, `$coverage`, `$dirt`, `$blood`, and `@HungerModerate`, `@FatigueModerate`,
`@ColdModerate`, `@DirtModerate`, `@BloodMild`, `@WarmthPoor`, `@OutsideUnderdressed`, `@OutsideGettingSoaked`. The
sources, the stage values and the other tiers have `"offer": "nowhere"`. They still work in expressions, prompts,
Papyrus and OAR, and ConditionEngine's own editor lists them all: a combined value shows its sources by priority and
the conditions that read it at which level, and a source shows the value it feeds.

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

Player mods and NPC mods run side by side. With iNeed and SeverActions both installed, or SunHelm and SeverActions,
each actor gets its own answer:

| Actor | Answered by |
|---|---|
| The player | The first player survival mod that is on: Frostfall for cold, then SunHelm, Last Seed, iNeed, Survival Mode |
| A follower SeverActions tracks | SeverActions |
| Any other NPC | Nothing: the value reads unavailable (0) |

The player sources check `IsPlayer`, and SeverActions only answers for actors it tracks. So the priorities below only
order sources for the same kind of actor.

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

## Warmth and coverage

`$warmth` and `$coverage` work for every actor, NPCs included, because they read the armor the actor is wearing. For
the player with Frostfall running they read Frostfall's own totals instead (priority 90). Those also count torches,
food, spells and perks.

The armor score is `ArmorWarmth(Warmth)` / `ArmorWarmth(Coverage)`, Frostfall's points summed over the worn items.
The rules are Frostfall 3.4.1's (`_Frost_ArmorProtectionDatastoreHandler`), the scale armor mods tag their gear for:

- **Gear type from the slots**: body (32), head (30 or 31), hands (33), feet (37), cloak (46, or the
  `WAF_ClothingCloak` keyword), shield (39). Rings, amulets and a lone circlet count for nothing.
- **Frostfall's keywords**, when the item also has `FrostfallEnableKeywordProtection`: `FrostfallWarmthPoor` ...
  `FrostfallWarmthMax` and `FrostfallCoveragePoor` ... `Max` pick a tier for the item's gear type. The cloak and
  accessory keywords (`FrostfallIsCloakFur`, `FrostfallIsWarmAccessory`, ...) set fixed values, and on body or head
  items `FrostfallExtraHead*` and `FrostfallExtraCloak*` add a hood or a cloak. These keywords exist while Frostfall
  is loaded (it injects them into Update.esm's ID range, so armor mods can tag gear without needing Frostfall).
- **Otherwise the defaults** for the gear type, about the Fair tier. An item that also covers the head, hands, feet
  or cloak slot adds that part's default. A head item named "hood" or "robe" counts as a hood: less warm than a
  helmet, but more covering.
- **Vanilla gear**: Frostfall keeps its own table of vanilla armor, which ConditionEngine doesn't copy. In its place
  the Survival Mode keywords move an item one tier: `Survival_ArmorWarm` up to Good, `Survival_ArmorCold` down to
  Poor. Every SE / AE Update.esm has them, Survival Mode installed or not. 89 vanilla items are warm (jarl clothes,
  college robes and hoods, Daedric armor ...) and 21 are cold (prisoner rags, miner clothes, sandals ...).

| Gear | Poor | Fair | Good | Excellent | Max |
|---|---|---|---|---|---|
| Body (warmth / coverage) | 60 / 35 | 110 / 54 | 140 / 72 | 175 / 91 | 190 / 109 |
| Head | 15 / 14 | 30 / 29 | 40 / 43 | 50 / 58 | 55 / 72 |
| Hands | 9 / 6 | 12 / 14 | 15 / 21 | 21 / 29 | 24 / 35 |
| Feet | 7 / 6 | 12 / 14 | 15 / 21 | 21 / 29 | 24 / 35 |
| Cloak, misc | 6 / 6 | 12 / 12 | 20 / 20 | 30 / 30 | 40 / 40 |

On the 0-100 scale, warmth 90 (all Poor) maps to 25, 165 (an ordinary set) to 50, 230 (Good with a cloak) to 75 and
300 to 100. Coverage maps 60 / 110 / 175 / 240 the same way. `ce warmth` in the console lists each worn item and what
it adds.

The game's own Survival Mode warmth rating (the condition function `GetActorWarmth`, SE / AE only) is another
per-actor reading. Its scale isn't documented, so it isn't mapped yet.

## Dirt and blood

`$dirt` reads the first bathing mod that tracks the actor; `$blood` reads Dirt and Blood.

| Priority | Source | Actors | Reading |
|---|---|---|---|
| 70 | Bathing in Skyrim - Renewed 2.7 (`Bathing in Skyrim.esp`) | the player | `mzinDirtinessPercentage` (`0xDA8`), 0-1 |
| 70 | Bathing in Skyrim - Renewed | its tracked bathers (`mzinTrackedBatherFaction` `0x86`) | StorageUtil float `BiS_Dirtiness` on the actor, 0-1 |
| 68 | Bathing in Skyrim, original (`Bathing in Skyrim - Main.esp`) | the player | stage effects `0xE55C` / `0xE55D` / `0xE55E` |
| 60 | Dirt and Blood 2.38 (`Dirt and Blood - Dynamic Visuals.esp`) | the player; bandits and laborers | the player's dirt effects `0x80D` / `0x80E` / `0x80F` / `0x83B` (stages 1-4); NPC dirt effects `0xDBE`-`0xDC2` |
| 50 | Keep It Clean (`Keep It Clean.esp`) | the player | stage effects `0xFBDBA` / `0xFBDB6` / `0x1564EE` |

- **Bathing in Skyrim - Renewed** counts dirtiness from 0 to 1 and changes tier at 0.2, 0.6 and 0.98. Those map to
  25 / 50 / 75, and 1 maps to 100. It tracks the player in a global, and every other bather (followers it is set to
  track) in a StorageUtil value, so followers have their own `$dirt`.
- **Dirt and Blood** keeps the player's stage as one of four dirt spells (and four blood spells), mapped to
  25 / 50 / 75 / 100. NPCs get its distributed spell (`DirtandBlood__DISTR.ini`). That spell gives bandits and
  laborers fixed dirt (stage 2-4 effects) and gives any NPC hurt in a fight blood stages 1-5, mapped to
  25 / 50 / 75 / 90 / 100. Other NPCs read 0, which is clean.
- **Keep It Clean** and the **original Bathing in Skyrim** keep the player's stage as a magic effect. Their form IDs
  come from the scripts of *Widget Addon - Keep It Clean - Bathing In Skyrim - Dirt and Blood* 1.7.2, a widget that
  reads all three. They have not been checked against those mods' own plugins.

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

- **Warmth is about clothes, not the weather.** `$warmth` says how warmly an actor is dressed, and `$cold` says how
  cold the player is. Combine warmth with the weather values ([Weather](weather.md)) for "dressed for this weather":
  `@OutsideUnderdressed`.
- **Hunger, fatigue and cold are for the player only, except SeverActions.** None of the other mods keeps per-actor
  needs that ConditionEngine can read. Last Seed's follower hunger is the exception: it's tracked in per-slot
  globals but not mapped (above). Warmth and coverage are per actor. Dirt is per actor for Bathing in Skyrim's
  tracked bathers and Dirt and Blood's NPCs.
- **Polled.** Globals and script properties have no change event. Watches re-read them at the poll interval (10 s by
  default, set per watch), except SeverActions, which is refreshed on its own events.
- **Form IDs are the mods' own.** A plugin that was compacted and ESL-flagged (ESLify) has new IDs, and its source
  reads unavailable. The temperature condition `Temperature(...)` is unrelated: it reads one global set in
  `ConditionEngine.ini`.
