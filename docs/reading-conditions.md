# Reading conditions

Compile an expression once, then evaluate it for any actor as often as you need. Expression syntax and the built-in
types are in the [README](../README.md#expression-syntax).

## From C or C++

```cpp
char err[256];
CE_Handle night = ConditionEngine_Compile("Time(20, 6) && !InteriorExterior(true)", err, sizeof err);
if (!night) {
    SKSE::log::warn("bad expression: {}", err);
    return;
}

// Main thread only. 1 = true, 0 = false (or unavailable), -1 = invalid handle.
const int32_t result = ConditionEngine_Evaluate(night, actor);   // actor null = the player

ConditionEngine_Release(night);
```

- `ConditionEngine_Compile` also takes a JSON condition tree: any source that starts with `{`.
- `ConditionEngine_EvaluateFormID(handle, formId)` evaluates for an actor by FormID.
- `ConditionEngine_EvaluateNamed("IsNight", actor)` evaluates a named condition without compiling: 1, 0, or -1 when
  no condition has that name.
- Evaluate on the main thread. `Game(...)` conditions call the engine's own condition functions, which aren't safe
  elsewhere. To react to changes from another thread, use a [watch](watching-conditions.md) and read its result.

### Inspecting a compiled condition

| Call | Returns |
|---|---|
| `ConditionEngine_ToExpression` | Canonical expression text |
| `ConditionEngine_ToJson` | The JSON condition tree, for saving |
| `ConditionEngine_Describe` | A human-readable summary |
| `ConditionEngine_Validate` | Configuration errors, one per line; 1 (an empty string) when valid |
| `ConditionEngine_GetDependencies` | The `CE_DEP_*` events that can change the result |
| `ConditionEngine_GetAvailability` | `{"state", "available", "reason"}`: whether every plugin and integration it needs is present |

Each string call follows the buffer convention: it returns the size needed, including the terminating null.

```cpp
std::string ToText(CE_Handle h) {
    const uint32_t size = ConditionEngine_ToExpression(h, nullptr, 0);
    std::string text(size, '\0');
    ConditionEngine_ToExpression(h, text.data(), size);
    text.resize(size ? size - 1 : 0);
    return text;
}
```

### Missing mods

A type whose plugin isn't installed still compiles, so saved conditions survive. When any part of an expression is
unavailable or still pending, the whole expression evaluates to false, even under `!` and `||`, so a missing mod
never turns into a match. `ConditionEngine_GetAvailability` says which part and why.

## From Papyrus

```papyrus
If ConditionEngine.Evaluate(akActor, "@IsNight && !Sneaking")
    ; ...
EndIf

Bool hungry = ConditionEngine.EvaluateNamed(akActor, "VeryHungry")
```

`Evaluate` and `EvaluateNamed` are latent: the script waits one frame while the main thread evaluates. `None`
evaluates against the player, and an invalid expression returns `False`. `Validate`, `Describe`, `ToJson` and
`Normalize` inspect an expression without evaluating it.

## Why is it true or false?

`ConditionEngine_Explain(handle, actor, asText, buf, size)` evaluates the way the engine does (cheapest part first,
with the same short-circuits) and reports every part:

```
[F] $hunger > 80 && !Sneaking
  [F] $hunger > 80
    [ok] $hunger = 42  (from severactions_hunger)
    [ok] 80.0 = 80
  [-] !Sneaking  [not read: a cheaper part decided the group]
```

`[T]` true, `[F]` false, `[P]` pending, `[U]` unavailable, `[-]` skipped. With `asText` 0 it returns JSON for a UI
(`expression`, `state`, `reason`, `value`, `from`, `note`, `children`). Main thread only.

The same output is available from Papyrus (`ConditionEngine.Explain(akActor, "expression")`, latent) and from the
in-game console: `ce eval <expression>` for the selected actor or the player, `ce evalfor <actor> <expression>` for
any other. Quote an expression that contains commas: `ce eval "Time(20, 6) && Sneaking"`.
