# Adding named conditions

A named condition is an expression with a name. Anything that takes an expression can use it as `@Name` (or
`Rule("Name")`), so mods can share conditions such as `@IsNight` or `@MyMod.Guarded` without repeating them.

Pick the way that fits your mod:

| Way | Best for | Lasts |
|---|---|---|
| [Preset file](#preset-files) | Data-only mods, no code | Loaded at `kDataLoaded`, every session |
| [Papyrus](#papyrus) | Script mods | Until the next game load |
| [C API, expression](#c-api-expressions) | SKSE plugins | The whole session |
| [C API, callback](#c-api-callbacks) | State only your plugin knows | The whole session |
| [Resolver](#resolvers) | A plugin with its own list of named rules | Queried live, never re-published |

Names are global, so prefix yours with your mod (`MyMod.Guarded`). Dotted names are only a convention.

## Preset files

Put a `*.json` file in `Data/SKSE/Plugins/ConditionEngine/`:

```json
{
  "conditions": {
    "MyMod.IsNight": "Time(20, 6)",
    "MyMod.BadWeather": "Weather(Rainy) || Weather(Snow)",
    "MyMod.Guarded": {
      "expr": "Combat || WeaponDrawn",
      "label": "Guarded",
      "category": "actor",
      "description": "In combat or with a weapon out"
    }
  },
  "sources": { "MyMod": "My Mod" }
}
```

A definition is expression text, or an object with `expr` and optional `label`, `category`, `icon`, `description`,
`offer`, `tags` and `owner`. Presets load once data has loaded, so they can name forms. Use portable form keys
(`"0x000BE127|Skyrim.esm"`) for anything the game keeps no editor ID for (perks, spells, effects, actors,
locations).

## Papyrus

```papyrus
Event OnPlayerLoadGame()
    ConditionEngine.RegisterNamed("MyMod.Guarded", "Combat || WeaponDrawn")
EndEvent
```

Script-registered names are cleared on every load, so register them from `OnPlayerLoadGame` (and from your quest's
`OnInit` for a new game). `UnregisterNamed` removes one.

## C API: expressions

```cpp
char err[256];
CE_Result r = ConditionEngine_RegisterNamed("MyMod.Guarded", "Combat || WeaponDrawn", "MyMod", err, sizeof err);
```

- `definition` is expression text, a JSON condition tree, or a JSON object with `expr` (or `condition`) plus the
  catalog fields above.
- `owner` names your mod. Pass null to use your DLL's file name. `ConditionEngine_UnregisterOwner("MyMod")` removes
  every named condition and value you registered, for example before re-registering after a config change.
- Registering an existing name replaces it. Watches and other expressions that use `@MyMod.Guarded` pick up the new
  definition, because names resolve on every read.

## C API: callbacks

When the answer comes from your plugin's own state, register a function instead of an expression:

```cpp
int32_t IsCursed(void* actor, void* userData) {
    auto* a = static_cast<RE::Actor*>(actor);
    return MyMod::Curses::Has(a) ? 1 : 0;   // main thread
}

ConditionEngine_RegisterNamedCallback("MyMod.IsCursed", IsCursed, nullptr, CE_DEP_HOST, "MyMod");

// Whenever the curse state changes (any thread):
ConditionEngine_NotifyEvent(CE_DEP_HOST, actor);
```

The `dependencies` bits tell watches when to re-check. Use `CE_DEP_HOST` for state only you know about and call
`ConditionEngine_NotifyEvent` when it changes, or `CE_DEP_POLLED` to be re-checked on the poll interval. Other bits
(`CE_DEP_EQUIPMENT`, `CE_DEP_LOCATION`, ...) reuse the engine's own change detection.

If your condition needs parameters (`IsCursed(level=3)`), register a [condition type](registering-condition-types.md)
instead.

## Resolvers

A plugin that keeps its own list of named rules (user-editable rules, for example) can answer lookups instead of
registering each one:

```cpp
int32_t Lookup(const char* key, uint32_t* outDependencies, void* userData) {
    auto* rule = MyMod::Rules::Find(key);
    if (!rule) return 0;
    *outDependencies = rule->dependencies;
    return 1;
}

int32_t Eval(const char* key, void* actor, void* userData) {
    return MyMod::Rules::Evaluate(key, static_cast<RE::Actor*>(actor)) ? 1 : 0;   // main thread
}

ConditionEngine_AddNamedResolver(Lookup, Eval, nullptr);
```

Resolvers are consulted for names the engine doesn't own, at evaluation time, so your rules can change without being
published again. `ConditionEngine_LookupNamed(key, &deps)` tells you whether a name exists, from any source.

## Showing up in editors

Named conditions appear in the catalog (`ConditionEngine_GetCatalog`, Papyrus `GetCatalog`) that editors and pickers
read. To group and label yours:

- Give each one a `category`. Categories are shared and topical (`survival` means survival whoever registered it).
  Register your own with `ConditionEngine_RegisterCategory(R"({"id": "mymod", "label": "My Mod"})", "MyMod", err,
  sizeof err)`, or a `"categories"` array in a preset.
- Label your origin once: `ConditionEngine_RegisterSource("MyMod", "My Mod")`, or `"sources"` in a preset. Pickers
  show it next to each item.
- `"offer": "owner"` offers an item only to pickers that ask as its owner; `"nowhere"` keeps it out of pickers
  (expression-only). Offer is metadata: everything compiles for everyone.
