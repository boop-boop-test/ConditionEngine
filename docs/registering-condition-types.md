# Registering your own condition types

A condition type is a new word in the expression language, such as `MyMod_IsCursed(3)`. Once registered it works in
every expression, preset, named condition, watch and OAR condition, and it appears in the catalog, so editors can
offer it with its fields, labels and defaults without knowing anything about your mod.

Use a type when the condition takes parameters. For a fixed yes/no answer, a
[callback named condition](adding-named-conditions.md#c-api-callbacks) is simpler. For a number, register a
[value type](values.md#registering-a-value-type): it reads as a value and compares as a condition.

## From an SKSE plugin (C API)

Register at `kPostLoad`, once `ConditionEngine.dll` is loaded:

```cpp
// Called on the main thread. paramsJson is the condition's JSON object,
// e.g. {"type": "MyMod_IsCursed", "level": 3}. Return non-zero for true.
int32_t EvalIsCursed(void* actor, const char* paramsJson, void* userData) {
    const auto params = nlohmann::json::parse(paramsJson);
    const int minLevel = params.value("level", 1);
    return MyMod::Curses::Level(static_cast<RE::Actor*>(actor)) >= minLevel ? 1 : 0;
}

void RegisterTypes() {
    ConditionEngine_RegisterSource("MyMod", "My Mod");

    char err[256];
    const CE_Result r = ConditionEngine_RegisterType(R"({
        "name": "MyMod_IsCursed",
        "label": "Is Cursed",
        "category": "actor",
        "description": "The actor carries a curse of at least this level",
        "source": "MyMod",
        "fields": [{"key": "level", "label": "Curse level", "type": "int", "default": 1}],
        "requiresPlugins": ["MyMod.esp"],
        "cost": 2
    })", EvalIsCursed, nullptr, CE_DEP_HOST, err, sizeof err);

    if (r != CE_OK) SKSE::log::error("MyMod_IsCursed: {}", err);
}
```

After that, all of these work:

```
MyMod_IsCursed                      // level = its default, 1
MyMod_IsCursed(3)                   // positional: fills "level"
MyMod_IsCursed(level=3) && !Combat  // named argument
```

### The schema

| Key | Meaning |
|---|---|
| `name` | The type's name in expressions. Prefix it with your mod. Required |
| `label`, `description`, `category`, `icon` | What editors show |
| `source` | Your mod's origin id. Without it the DLL's file name is used, with a warning |
| `fields` | The parameters: `key`, `label`, `type`, optional `default` |
| `positional` | Field keys that positional arguments fill, in order. Default: every field in declared order |
| `compareField` | The field a trailing comparison with a literal sets (`MyMod_Curse > 2`) |
| `aliases` | Other names that compile to this type |
| `requiresPlugins` | Plugins the type needs; without them, expressions using it are unavailable (false) |
| `cost` | Evaluation cost class, 0 (constant) to 5 (a Papyrus call). Default 3. And/Or groups run cheaper parts first |
| `offer`, `tags` | Where pickers offer it (`everywhere`, `owner`, `nowhere`) and free tags |

**Field types:** `int`, `float`, `string`, `bool`, `enum`, `enumList`, `form`, `formList`, `stringList`, `intList`,
`comparison`, `namedCondition`, `custom`, `value`. `include/ConditionEngine/Schema.h` lists their options (ranges,
enum options, form record types).

**Reserved field keys:** `type` and `forcePolled` (they're the condition's own JSON keys), and `operator` /
`operation` except on a `comparison` field. A schema that uses them, or repeats a key, is refused with
`CE_ERR_INVALID_ARG`. A name that's already registered returns `CE_ERR_ALREADY_EXISTS`.

### Telling watches when to re-check

The `dependencies` argument says which events can change your result:

- **`CE_DEP_HOST`**: state only your plugin knows. Call `ConditionEngine_NotifyEvent(CE_DEP_HOST, actor)` from any
  thread whenever it changes (null actor = every watched actor). Notifications in one frame merge.
- **Engine events** such as `CE_DEP_EQUIPMENT`, `CE_DEP_LOCATION`, `CE_DEP_COMBAT` or `CE_DEP_MAGIC_EFFECT`: the
  engine already detects these, so your type is re-checked with no extra work.
- **`CE_DEP_POLLED`**: no change signal at all; re-checked on the poll interval (10 s by default).

### When your integration isn't ready

`requiresPlugins` covers missing plugins. For readiness only your plugin knows (a connection, a config load), publish
it:

```cpp
ConditionEngine_SetTypeAvailability("MyMod_IsCursed", 0, "loading curse data");   // 0 pending
ConditionEngine_SetTypeAvailability("MyMod_IsCursed", 1, nullptr);                 // 1 available
ConditionEngine_SetTypeAvailability("MyMod_IsCursed", 2, "MyMod is disabled");     // 2 unavailable
```

While a type is pending or unavailable, expressions that use it evaluate to false as a whole, `GetAvailability`
reports the reason, and watches re-check when the state changes.

## C++: static library hosts

A host that builds ConditionEngine into itself (`add_subdirectory(ConditionEngine)`, linking
`condition-engine-lib`) can register a native condition class through `TypeBuilder` (`ConditionEngine/Registry.h`):

```cpp
#include <ConditionEngine/ConditionEngine.h>

namespace CE = ConditionEngine;

class IsCursedCondition : public CE::Condition {
public:
    explicit IsCursedCondition(int level) : level_(level) {}

    bool Evaluate(const CE::EvaluationContext& context) const override {
        return MyMod::Curses::Level(context.GetActor()) >= level_;
    }
    std::string GetDescription() const override { return "is cursed (level " + std::to_string(level_) + "+)"; }
    nlohmann::json ToJson() const override { return {{"type", "MyMod_IsCursed"}, {"level", level_}}; }
    std::unique_ptr<CE::Condition> Clone() const override { return std::make_unique<IsCursedCondition>(*this); }
    CE::EventDependency GetEventDependencies() const override { return CE::EventDependency::Dial; }  // host-signalled

private:
    int level_;
};

void RegisterTypes() {
    CE::TypeBuilder("MyMod_IsCursed", "Is Cursed", "actor")
        .Describe("The actor carries a curse of at least this level")
        .Source("MyMod")
        .Fields({CE::Field::Int("level", "Curse level", 1)})
        .RequiresPlugins({"MyMod.esp"})
        .Cost(2)
        .Factory([](const nlohmann::json& j) -> std::unique_ptr<CE::Condition> {
            return std::make_unique<IsCursedCondition>(j.value("level", 1));
        })
        .Register(CE::Registry::Get());
}
```

`EventDependency::Dial` is the C++ name of `CE_DEP_HOST`. The `Condition` base class has more optional overrides
(three-valued results for pending state, constant folding, a cost, validation, reading as a value); see
`ConditionEngine/Condition.h`.
