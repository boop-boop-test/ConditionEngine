# Dials

A dial is a value that rules drive: an event switches it on and another off, a condition sets it, a rate wears it
down every second (or by your current hunger), a key cycles it through named modes. Every dial is also a named value
(`$name`) and can name conditions (`@Name`), so anything that reads ConditionEngine can react to it, and SkyrimNet
prompts read every dial with `get_dials(npc.UUID)` ([skyrimnet.md](skyrimnet.md)).

Preset files, `Dials.Define`, `ConditionEngine_DefineDial` and the editor (`ce ui`) all describe a dial with the
same JSON object. A preset file holds them in its `"dials"` section, next to `"conditions"` and `"values"` if it
has any:

```json
{
  "dials": {
    "MyMod_Alarm": {
      "label": "Alarm",
      "range": [0, 10],
      "rules": [
        {"when": "Combat", "set": 10},
        {"while": "!Combat", "rate": -1}
      ],
      "conditions": {"MyMod_Alarmed": ">= 5"}
    }
  }
}
```

Preset files go in `Data/SKSE/Plugins/ConditionEngine/*.json` and load in name order. Comments (`//`) are allowed, and keys
that start with `_` are ignored. The dial's name (`MyMod_Alarm`) is letters, digits and `_`, must not start
with a digit, and is also its ConditionEngine value: `$MyMod_Alarm`. Prefix names with your mod's name.

## The dial

| Key | Default | Meaning |
|---|---|---|
| `label` | the name | Shown in the editor and ConditionEngine's catalog |
| `description`, `category`, `icon` | | Catalog metadata. `category` defaults to `dials` |
| `kind` | `number` | `number`, `toggle` (0 / 1) or `choice` (named options) |
| `range` | `[0, 100]` | `[min, max]` for numbers; values are always clamped to it |
| `integer` | `false` | Round to whole numbers |
| `options` | | Choice dials: `["Casual", "Formal"]` (0, 1, …) or `{"0": "Off", "5": "Low"}` |
| `default` | `0` (or min) | Starting value; a choice dial may name an option (`"Formal"`) |
| `unit` | | `%`, `h`, … for display |
| `scope` | `global` | `global`: one value for the whole game. `player`: the player's own value only. `actor`: each actor in `track` has their own |
| `track` | | `actor` dials: a condition saying which actors have the dial (`IsPlayer \|\| IsFollower`, `!IsPlayer` for NPCs, `true` for everyone). An actor dial without one is the player's only |
| `persist` | `true` | Save the values with the game |
| `oar` | `false` | Open Animation Replacer submods may read the dial (`ConditionEngineDial`, [oar-conditions.md](oar-conditions.md#a-dial)) |
| `thresholds` | | Numbers; a rate that crosses one sends `Dials_Changed` |
| `precision` | range / 200 | The smallest change worth publishing to ConditionEngine |
| `provides`, `priority` | | Feed combined ConditionEngine values, e.g. `"provides": ["hunger"]` makes the dial a `$hunger` source |
| `conditions` | | Named conditions: `{"Name": "expression"}`, or `{"Name": {"expr": "...", "knowledge": "..."}}` to give SkyrimNet NPCs knowledge while it is true |
| `enabled` | `true` | `false` keeps the dial (and its value) but runs no rules |
| `rules` | `[]` | See below |

**Conditions.** Each entry becomes `@Name` for any mod. The text can be:
- a comparison of this dial: `">= 75"` (means `$id >= 75`),
- one of a choice dial's option labels: `"Formal"` (means `$id == 1`),
- or any expression: `"$wetness > 50 && !InteriorExterior(true)"`.

## Rules

A rule has one **trigger**, one **action**, and optional extras. Rules run in order.

### Triggers

| Trigger | Example | Fires |
|---|---|---|
| `on` | `"on": "Hit"` | On a game event (table below) |
| `event` | `"event": "MyMod_Start"` | On an SKSE mod event (`SendModEvent`), or `Dials.Signal` / `Dials_Signal` |
| `when` | `"when": "Combat"` | When a condition becomes true. `"edge": "falling"` for false, `"both"` for any change |
| `while` | `"while": "@OutsideInRain"` | Every tick while true. `"while": true` always runs |
| `every` | `"every": 5` | Every 5 seconds (game hours with `"clock": "game"`) |
| `key` | `"key": "F7"` | A key press outside menus: `A`–`Z`, `0`–`9`, `F1`–`F12`, `Num0`–`Num9`, `Space`, `0x3F` … |
| `skyrimnet` | `"skyrimnet": "dialogue"` | A SkyrimNet event about the actor ([skyrimnet.md](skyrimnet.md)) |
| `aiAction` | `"aiAction": "CalmDown"` | SkyrimNet's AI chose this action for the NPC; needs `description` |

`when` notices a change on the first check after it happens; checks run every `iTickMs` (200 ms). The first
check after a load, or after an actor comes into range, only records the state: it does not fire.

### Actions

| Action | Example | Does |
|---|---|---|
| `set` | `"set": 10` | Sets the value. With `while`, holds it there |
| `add` | `"add": -5` | Adds |
| `multiply` | `"multiply": 0.5` | Multiplies |
| `toggle` | `"toggle": true` | Flips between min and max |
| `reset` | `"reset": true` | Back to the default |
| `cycle` | `"cycle": 1` | Next option of a choice dial (`-1`: previous); numbers step and wrap |
| `rate` | `"rate": -1` | `while` only: change per second (per game hour with `"clock": "game"`) |
| `approach` | `"approach": 0, "halfLife": 30` | `while` only: move toward a target, halfway every `halfLife` |
| `send` | `"send": "MyMod_FightOver"` | Sends an SKSE mod event for other mods' scripts (below). Does not change the dial |

Amounts are numbers or ConditionEngine value expressions: `"rate": "-$hunger / 100"`, `"set": "$cold"`,
`"approach": "ActorValue(Health, usePercentage=true) * 100"`. Inside a rule, `DialsEvent` reads the event's
number (a mod event's number, the count of items added, the hours slept). An expression that cannot be read
for the actor right now (a missing mod, an unloaded actor) makes the rule do nothing.

### Sending mod events

`send` lets a dial drive other mods without Papyrus of your own:

```json
{"when": "Combat", "edge": "falling", "send": "MyMod_FightOver", "sendNumber": "$alarm", "sendText": "calm"}
```

Scripts receive it as any mod event (`RegisterForModEvent("MyMod_FightOver", "OnFightOver")`): strArg =
`sendText` (empty by default), numArg = `sendNumber` (a number or a value expression; the dial's value by
default), sender = the actor the rule acted on (the player for global dials fired by keys and timers).

- Only rules that fire once can send: `on`, `event`, `when`, `every`, `key`, `skyrimnet`, `aiAction`. Not
  `while` (it would send every tick), and not with `for` (a sent event cannot be undone).
- Events go out at the start of the next tick (`iTickMs`), never at once. Two dials that answer each other's
  events go one round per tick instead of looping; `cooldown` slows them down further.
- A dial does not hear the events its own rules send. Other dials do.
- Rules send at most 32 mod events between two ticks. The rest are dropped, and the editor's activity list and
  the SKSE log say how many.
- `Dials_Changed`, `Dials_Signal` and names starting with `SkyrimNet:` or `SkyrimNetAction:` cannot be sent.
- `if`, `cooldown`, `chance`, `actor`, `tell` and the event filters work as for any rule; the editor's Test
  button sends too.

### Extras

| Key | Meaning |
|---|---|
| `if` | A condition on the actor the rule acts on; the rule only fires when it is true |
| `for` | Undo after this long (seconds, or game hours with `clock: game`). `set` / `toggle` / `cycle` / `reset` restore the value from before; firing again while it lasts extends it. Each `add` takes back its own amount, so adds stack |
| `cooldown` | Seconds (or game hours) before the rule fires again for the same actor |
| `chance` | 0 to 1: the share of times it fires |
| `clock` | `real` (seconds of unpaused play, the default) or `game` (game hours: sleeping and waiting count) |
| `actor` | Who the rule acts on: `auto` (the event's actor), `other` (the event's other actor), `player`, `crosshair` (what the player looks at) |
| `form` | Events: the event's form must be this one: `"0x12EB7\|Skyrim.esm"`, or an editor ID for forms that keep theirs (keywords, globals) |
| `keyword` | Events: the event's form has this keyword (`"WeapTypeSword"`, `"MagicDamageFire"`) |
| `text` | Mod events: the string argument; menus: the menu; animations: the tag; skills: the skill |
| `other` | Events: `player` or `!player` (who the event's other actor is) |
| `otherIf` | Events: a condition on the event's other actor |
| `tell`, `tellAs` | Tell SkyrimNet when the rule fires: a memory (default) or `narration` the NPC reacts to. `{name}`, `{value}`, `{label}` |
| `description` | `aiAction` rules: what the AI reads to decide |
| `label`, `enabled` | A name for the editor and the log; `false` switches the rule off |

### Game events

| Event | The event's actor | Other actor | Form | Number / text |
|---|---|---|---|---|
| `Load` | the player | | | a save loaded or a game started |
| `Hit` | the one hit | the attacker | weapon or spell | |
| `CombatStart` / `CombatEnd` | the fighter | its target | | |
| `Death` | the one who died | the killer | | |
| `Kill` | the killer | the one killed | | |
| `Equip` / `Unequip` | the actor | | the item | |
| `SpellCast` | the caster | | the spell | |
| `MagicEffect` | the target | the caster | the effect | |
| `Activate` | the one activating | the activated actor, if any | the object's base | |
| `ItemAdded` / `ItemRemoved` | the actor | the giver / receiver | the item | number = count |
| `LocationChange` | the actor | | the new location | |
| `SleepStart` / `SleepStop` | the player | | | number = hours slept (stop) |
| `MenuOpen` / `MenuClose` | the player | | | text = menu (`InventoryMenu`, `Dialogue Menu`) |
| `Animation` | the player | | | text = tag (`JumpUp`, `weaponSwing`) |
| `SkillUp` | the player | | | text = skill (`OneHanded`) |

For actor dials an event only counts when its actor has the dial (`track`). Global dials hear every actor's
events; use `if` (`"if": "IsPlayer"`) to narrow them.

## Changes

Every change reaches:
- **ConditionEngine**: `$id` reads the new value at once. Actors without their own value read the default.
- **Papyrus**: the mod event `Dials_Changed` (strArg = dial, numArg = value, sender = the actor) for changes
  made by actions, threshold crossings and reaching the range's ends. Small rate steps do not send it.
- **C API subscribers** (`Dials_Subscribe`): every published change.

A dial does not hear its own `Dials_Changed`, so a rule on it cannot loop. Rules on other dials can listen.

## Where a dial comes from

Each name has up to two layers: the **base** (a preset file's `"dials"`, or a mod's or script's `Define`) and the
player's **edit** (saved by the editor to `Data/SKSE/Plugins/ConditionEngine/User/User.json`, which also holds the
named conditions and values made in the editor). The edit wins. Reverting it
in the editor brings the base back. Definitions made by `Define` are not saved; define them every session.
Values are saved per game, and kept for a name nobody defines (yet) so a mod that loads late does not lose them.

## Using dials

| From | How |
|---|---|
| Expressions | `$manic == 1`, `@Manic`, `$wetness > 50 && !InteriorExterior(true)` |
| SkyrimNet prompts | `{{ get_dials(npc.UUID).wetness }}`, `{% if get_dials(npc.UUID).manic %}` ([skyrimnet.md](skyrimnet.md)) |
| Papyrus | `Dials.GetValue("wetness", akActor)`, `Dials.SetValue`, `Dials.Signal("MyMod_Start")`, `Dials.Define(id, json)`; the mod event `Dials_Changed` |
| C API | `ConditionEngine_DefineDial`, `GetDial`, `SetDial`, `AddDial`, `ResetDial`, `SignalDials`, `SubscribeDials`, `OpenEditor` (`ConditionEngineAPI.h`) |
| Console | `ce dials` (list), `ce dials set wetness 80`, `ce dials signal Bard_Start`, `ce dials log`, `ce dials skyrimnet`, `ce ui` |

Starter ideas live in the editor under **+ New dial** and in [examples/dials.json](examples/dials.json): combat
alarm, fades with hunger, on / off by events, wetness, adrenaline, kills today, hours awake, fear per NPC, mode
switch, on fire for a while, sneak tension, smoothed cold, gifts per follower, manic spells and annoyance (both
with SkyrimNet).

## Settings (`ConditionEngine.ini`)

```ini
[Dials]
iTickMs = 200            ; how often rules run (50-2000)
fMaxCatchUpHours = 48    ; the longest game-time gap a game-clock rule makes up
fTrackSeconds = 2        ; how often "track" is checked again per actor
bVerbose = false         ; log every notified change

[Editor]
iHotkey = Shift+F10      ; the key that opens the editor: Shift+F10, Ctrl+Alt+K, Insert, 0x44; 0 = none
bPauseGame = true
iVRButton = 0            ; VR: a controller button that opens it (1 B/Y, 2 grip, 7 A/X, 32 stick, 33 trigger, 35 touchpad)
iVRModifier = 0          ; VR: a second button held with it

[SkyrimNet]
bEnable = true
fRefreshSeconds = 1.5    ; how fresh condition_engine() answers are
```

## Opening the editor

Shift+F10 opens the editor (also `ce ui` in the console, `Dials.OpenEditor()` and `ConditionEngine_OpenEditor`).
Change the key in the editor itself (**Key to open … Change** at the bottom of the sidebar: push a key, with Shift / Ctrl /
Alt if you like, or pick one, or none). Choosing one there saves it in `User/User.json` and applies at once; it wins over `[Editor] iHotkey`.

- **With InputManager installed**, the key is the InputManager action `ConditionEngine_OpenEditor` ("Open the
  ConditionEngine editor"). The ini key is only its first binding; rebind it in InputManager's menu, a gamepad
  button included, or here.
- **Without it**, a key with Shift, Ctrl or Alt (the default) is watched by ConditionEngine, and a bare key is bound
  by Magelight UI; when another mod already holds a bare key, the editor says so and keeps the old one. Esc closes
  the editor.
- InputManager takes one modifier per action: Shift+F10 binds F10 with Shift as its modifier key.
- **In VR**, set `[Editor] iVRButton` (and `iVRModifier`) to open it from a controller.
