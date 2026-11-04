# OAR conditions

With Open Animation Replacer installed, `ConditionEngine.dll` adds two conditions that any OAR submod can use in its
`config.json`, and that OAR's in-game editor offers:

| OAR condition | Field | Reads |
|---|---|---|
| `ConditionEngine` | `Preset` | A named condition: from a preset file, a script, or another plugin. The leading `@` is optional |
| `ConditionEngineExpression` | `Expression` | Any expression, compiled once per distinct text |

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
    "requiredVersion": "1.1.0.0",
    "Preset": "MyMod.Guarded"
}
```

## An inline expression

```json
{
    "condition": "ConditionEngineExpression",
    "requiredPlugin": "ConditionEngine",
    "requiredVersion": "1.1.0.0",
    "Expression": "@IsNight && !Sneaking"
}
```

## How it behaves

- The subject is the actor OAR evaluates for; non-actors read false. `negated` and `disabled` work as for any OAR
  condition.
- A missing preset, an invalid expression, or a condition whose plugins or integrations are unavailable reads false.
  OAR's editor shows why next to the condition.
- OAR evaluates conditions inside the animation update, mostly on worker threads. ConditionEngine evaluates on the
  main thread, so OAR reads the latest result of a watch ConditionEngine keeps for that text and actor. The first
  evaluation for an actor reads false and starts the watch; pairs not evaluated for a minute are dropped.
- Without OAR nothing registers, and the log says so in one line.

Use `ce eval <expression>` in the console to check what an expression reads for the selected actor.
