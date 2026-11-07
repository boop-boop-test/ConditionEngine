# OAR conditions

With Open Animation Replacer installed, `ConditionEngine.dll` adds three conditions that any OAR submod can use in its
`config.json`, and that OAR's in-game editor offers:

| OAR condition | Field | Reads |
|---|---|---|
| `ConditionEngine` | `Preset` | A named condition: from a preset file, a script, or another plugin. The leading `@` is optional |
| `ConditionEngineExpression` | `Expression` | Any expression, compiled once per distinct text |
| `ConditionEngineDial` | `Dial`, `Comparison`, `Value` | A [dial](dials.md) that opted in, compared with a number |

## A named condition

Define the condition once in a preset (`Data/SKSE/Plugins/ConditionEngine/MyMod.json`):

```json
{ "conditions": { "MyMod.Guarded": "Combat || WeaponDrawn" } }
```

and use it in the submod's `config.json`:

```json
{
    "condition": "ConditionEngine",
    "requiredPlugin": "ConditionEngine",
    "requiredVersion": "1.2.0.0",
    "Preset": "MyMod.Guarded"
}
```

## An inline expression

```json
{
    "condition": "ConditionEngineExpression",
    "requiredPlugin": "ConditionEngine",
    "requiredVersion": "1.2.0.0",
    "Expression": "@IsNight && !Sneaking"
}
```

## A dial

A dial is only readable from OAR when it says so. In the dial's JSON:

```json
{ "dials": { "MyMod_Alarm": { "range": [0, 10], "oar": true, "rules": [{"when": "Combat", "set": 10}] } } }
```

or in the editor (`ce ui`): the dial's **More settings**, **Animation mods (OAR) can read it**. The editor's *Use the
dial in prompts and scripts* section then has a `config.json` line to copy. Then in the submod:

```json
{
    "condition": "ConditionEngineDial",
    "requiredPlugin": "ConditionEngine",
    "Dial": "MyMod_Alarm",
    "Comparison": ">=",
    "Value": { "value": 5 }
}
```

- `Value` is OAR's usual number field: a fixed number, a global, an actor value or a graph variable.
- Toggles read 0 (off) and 1 (on). Choice dials read the option's number: `["Calm", "Tense"]` is 0 and 1.
- A global dial has one value for every actor. An actor dial reads that actor's value, or the default for an actor
  without one.
- The value is read at once, on any thread: no watch and no false first read, unlike the two conditions above. It
  is the value the dial last published, at most one tick old (200 ms) and within the dial's `precision`.
- A dial without `"oar": true`, or a name no dial has, reads false. OAR's editor shows the dial's current value for
  the selected actor, or why there is none.
- The dial's named conditions (`@Name`) work in the `ConditionEngine` condition whether or not the dial opts in;
  `oar` only governs `ConditionEngineDial`.

## How it behaves

- The subject is the actor OAR evaluates for; non-actors read false. `negated` and `disabled` work as for any OAR
  condition.
- A missing preset, an invalid expression, or a condition whose plugins or integrations are unavailable reads false.
  OAR's editor shows why next to the condition.
- OAR evaluates conditions inside the animation update, mostly on worker threads. ConditionEngine evaluates on the
  main thread, so OAR reads the latest result of a watch ConditionEngine keeps for that text and actor. The first
  evaluation for an actor reads false and starts the watch; pairs not evaluated for a minute are dropped.
- Without OAR nothing registers, and the log says so in one line.

Configs need ConditionEngine 1.2.0 or later, the first release with the OAR conditions.

Use `ce eval <expression>` in the console to check what an expression reads for the selected actor.
