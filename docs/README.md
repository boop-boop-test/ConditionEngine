# ConditionEngine guides

How-to guides for using ConditionEngine from another mod. The top-level [README](../README.md) is the reference
overview; `include/ConditionEngine/ConditionEngineAPI.h` and `ConditionEngine.psc` are the full API.

| Guide | What it covers |
|---|---|
| [Getting started](getting-started.md) | Loading `ConditionEngine.dll` from an SKSE plugin, checking the API version, conventions |
| [Reading conditions](reading-conditions.md) | Compiling and evaluating expressions from C, C++ and Papyrus; explaining a result |
| [Adding named conditions](adding-named-conditions.md) | `@Name` conditions from preset files, Papyrus, the C API, callbacks and resolvers |
| [Registering your own condition types](registering-condition-types.md) | New types with a schema and a callback, so they work in every expression and editor |
| [Watching conditions](watching-conditions.md) | Getting told when a result changes: watches, list watches, mod events, poll intervals |
| [Values](values.md) | Reading numbers, pushing values, named values, value types and value watches |
| [OAR conditions](oar-conditions.md) | Using presets, expressions and dials in Open Animation Replacer submods |
| [Survival needs](survival.md) | The built-in `$hunger`, `$fatigue`, `$cold`, `$warmth`, `$coverage`, `$dirt`, `$blood`: which mod answers, the shared scale, adding a source |
| [Dials](dials.md) | Values that rules drive: events, conditions, rates, timers, keys; per actor; saved; the editor |
| [SkyrimNet](skyrimnet.md) | `get_dials()` and `condition_engine()` in prompts; SkyrimNet events, memories, world knowledge and AI actions from dials |
| [Important people](people.md) | `ImportantPerson()`: Jarls and courts, leaders, guilds, quest people, your followers, spouse and household; adding people from a preset, Papyrus, the C API or the console |
| [Weather](weather.md) | The built-in weather values and conditions: how any weather mod's weathers are read, tested mods, correcting a weather |

All examples use the C API, which works from any SKSE plugin whatever its compiler, CRT or JSON library. A host that
builds ConditionEngine into itself as a static library can use the C++ API (`ConditionEngine/ConditionEngine.h`)
instead; [Registering your own condition types](registering-condition-types.md#c-static-library-hosts) shows that
path.
