// ValueTests.cpp - Out-of-game tests for value expressions (API v5): parsing, printing,
// arithmetic, named values, pushed values and Compare. Nothing here touches game state.

#include "ConditionEngine/ConditionEngine.h"
#include "ConditionEngine/Availability.h"
#include "ConditionEngine/Explain.h"
#include "ConditionEngine/Expression.h"
#include "ConditionEngine/FirstMatchList.h"
#include "ConditionEngine/Presets.h"
#include "ConditionEngine/Stats.h"
#include "ConditionEngine/Values/PushedValues.h"
#include "ConditionEngine/Values/RefreshOn.h"
#include "ConditionEngine/Values/ValueNodes.h"

#include <cmath>
#include <thread>

namespace {
    using json = nlohmann::json;
    using namespace ConditionEngine;

    using CheckFn = void (*)(bool ok, const std::string& what);
    CheckFn Check = nullptr;

    json ParseCondition(const std::string& src) {
        Expression::ParseError error;
        auto result = Expression::ParseToJson(src, &error);
        Check(result.has_value(), "parse '" + src + "': " + error.message);
        return result.value_or(json());
    }

    json ParseValue(const std::string& src) {
        Expression::ParseError error;
        auto result = Expression::ParseValueToJson(src, &error);
        Check(result.has_value(), "parse value '" + src + "': " + error.message);
        return result.value_or(json());
    }

    void ExpectConditionJson(const std::string& src, const json& expected) {
        auto actual = ParseCondition(src);
        Check(actual == expected, "json of '" + src + "'\n  got:      " + actual.dump() + "\n  expected: " + expected.dump());
    }

    void ExpectValueJson(const std::string& src, const json& expected) {
        auto actual = ParseValue(src);
        Check(actual == expected, "value json of '" + src + "'\n  got:      " + actual.dump() + "\n  expected: " + expected.dump());
    }

    void ExpectError(const std::string& src, const std::string& fragment, bool valueMode = false) {
        Expression::ParseError error;
        auto result = valueMode ? Expression::ParseValueToJson(src, &error) : Expression::ParseToJson(src, &error);
        Check(!result.has_value(), "expected error for '" + src + "'");
        Check(error.message.find(fragment) != std::string::npos,
              "error for '" + src + "' should mention '" + fragment + "', got '" + error.message + "'");
    }

    // Condition text → json → text → json is stable
    void ExpectConditionRoundTrip(const std::string& src, const std::string& expectedText) {
        auto first = ParseCondition(src);
        auto printed = Expression::FromJson(first);
        Check(printed == expectedText, "print '" + src + "'\n  got:      " + printed + "\n  expected: " + expectedText);
        Check(ParseCondition(printed) == first, "reparse '" + printed + "' changed the tree");
    }

    void ExpectValueRoundTrip(const std::string& src, const std::string& expectedText) {
        auto first = ParseValue(src);
        auto printed = Expression::ValueFromJson(first);
        Check(printed == expectedText, "print value '" + src + "'\n  got:      " + printed + "\n  expected: " + expectedText);
        Check(ParseValue(printed) == first, "reparse value '" + printed + "' changed the tree");
    }

    ValueResult Read(const std::string& src) {
        std::string error;
        auto value = Expression::CompileValueAny(src, &error);
        Check(value != nullptr, "compile value '" + src + "': " + error);
        if (!value) return ValueResult::Fail(ValueStatus::Error, error);
        EvaluationContext context(nullptr);
        return value->Read(context);
    }

    void ExpectRead(const std::string& src, double expected) {
        const auto result = Read(src);
        Check(result.Ok() && std::abs(result.value - expected) < 1e-9,
              "read '" + src + "' = " + std::to_string(result.value) + " (" + ValueStatusName(result.status) + " " +
                  result.reason + "), expected " + std::to_string(expected));
    }

    bool EvaluateCondition(const std::string& src) {
        std::string error;
        auto condition = Expression::CompileAny(src, &error);
        Check(condition != nullptr, "compile '" + src + "': " + error);
        EvaluationContext context(nullptr);
        return condition && condition->Evaluate(context);
    }

    void Define(const std::string& name, const json& definition) {
        std::string error;
        auto def = NamedValueDef::Parse(name, definition, "value-tests", &error);
        Check(def != nullptr, "define $" + name + ": " + error);
        if (def) Registry::Get().RegisterNamedValue(def);
    }

    json Named(const std::string& name) { return {{"type", "NamedValue"}, {"name", name}}; }
    json Math(const std::string& op, json args) { return {{"type", "Math"}, {"operator", op}, {"args", std::move(args)}}; }
    json Compare(json left, const std::string& op, json right) {
        return {{"type", "Compare"}, {"left", std::move(left)}, {"operator", op}, {"right", std::move(right)}};
    }
}

void RunValueTests(CheckFn check) {
    Check = check;
    const json health = {{"type", "ActorValue"}, {"actorValue", "Health"}};
    const json magicka = {{"type", "ActorValue"}, {"actorValue", "Magicka"}};
    const json combat = {{"type", "Combat"}};
    const json truthCombat = {{"type", "Truth"}, {"condition", combat}};

    // ── Parsing ──────────────────────────────────────────────────────────────
    ExpectConditionJson("$hunger > 80", Compare(Named("hunger"), ">", 80));
    ExpectConditionJson("ActorValue(Health) * 2 < ActorValue(Magicka)",
                        Compare(Math("*", {health, 2}), "<", magicka));
    // The classic form is unchanged; a non-literal right side makes it a Compare
    ExpectConditionJson("ActorValue(Health) < 30",
                        {{"type", "ActorValue"}, {"actorValue", "Health"}, {"value", 30}, {"operator", "<"}});
    ExpectConditionJson("ActorValue(Health) < 30 * 2", Compare(health, "<", Math("*", {30, 2})));
    ExpectConditionJson("ActorValue(Health) >= -5",
                        {{"type", "ActorValue"}, {"actorValue", "Health"}, {"value", -5}, {"operator", ">="}});
    ExpectConditionJson("80 < $hunger", Compare(80, "<", Named("hunger")));
    ExpectConditionJson("$flag", Compare(Named("flag"), "!=", 0));  // a bare value is "!= 0"
    ExpectConditionJson("!$a > 3 && Combat",
                        {{"type", "Logical"}, {"operator", "And"},
                         {"children", {{{"type", "Logical"}, {"operator", "Not"},
                                        {"children", {Compare(Named("a"), ">", 3)}}},
                                       combat}}});
    ExpectConditionJson("$x >= ${Threshold}", Compare(Named("x"), ">=", "${Threshold}"));

    ExpectValueJson("$a - 1", Math("-", {Named("a"), 1}));
    ExpectValueJson("$a -1", Math("-", {Named("a"), 1}));
    ExpectValueJson("-$a", Math("neg", json::array({Named("a")})));
    ExpectValueJson("-3", -3);
    ExpectValueJson("2 + 3 * 4", Math("+", {2, Math("*", {3, 4})}));
    ExpectValueJson("(2 + 3) * 4", Math("*", {Math("+", {2, 3}), 4}));
    ExpectValueJson("clamp($x, 0, 1)", Math("clamp", {Named("x"), 0, 1}));
    ExpectValueJson("MAX($a, $b, 2)", Math("max", {Named("a"), Named("b"), 2}));
    ExpectValueJson("$hunger * (1 - Combat)", Math("*", {Named("hunger"), Math("-", {1, truthCombat})}));
    ExpectValueJson("ActorValue(Health)", health);  // a numeric call reads its number
    ExpectValueJson("Combat", truthCombat);          // a condition reads 0 / 1
    ExpectValueJson("1.5e-3", 0.0015);

    ExpectError("Sneaking > 1", "does not support comparisons");
    ExpectError("Weather(Rainy) > $x", "does not support comparisons");
    ExpectError("clamp(1, 2)", "takes 3", true);
    ExpectError("max(1)", "two or more", true);
    ExpectError("$ > 1", "value name");
    ExpectError("$a >", "Expected a condition or value");
    ExpectError("$a > 1 > 2", "Unexpected input");

    // ── Printing ─────────────────────────────────────────────────────────────
    ExpectConditionRoundTrip("$a + $b * 2 > 3", "$a + $b * 2 > 3");
    ExpectConditionRoundTrip("($a + $b) * 2 > 3", "($a + $b) * 2 > 3");
    ExpectConditionRoundTrip("ActorValue(Health) * 2 < ActorValue(Magicka)",
                             "ActorValue(Health) * 2 < ActorValue(Magicka)");
    ExpectConditionRoundTrip("$flag", "$flag != 0");
    ExpectConditionRoundTrip("!($a > 1) || Combat", "!$a > 1 || Combat");
    ExpectValueRoundTrip("$a - ($b - 1)", "$a - ($b - 1)");
    ExpectValueRoundTrip("($a - $b) - 1", "$a - $b - 1");
    ExpectValueRoundTrip("-($a + 1)", "-($a + 1)");
    ExpectValueRoundTrip("Combat && Swimming", "Combat && Swimming");
    ExpectValueRoundTrip("(ActorValue(Health) < 30) * 2", "(ActorValue(Health) < 30) * 2");
    ExpectValueRoundTrip("(Combat || Swimming) + 1", "(Combat || Swimming) + 1");
    ExpectValueRoundTrip("clamp($hunger * (1 - $light), 0, 100)", "clamp($hunger * (1 - $light), 0, 100)");

    // ── Arithmetic ───────────────────────────────────────────────────────────
    ExpectRead("2 + 3 * 4", 14);
    ExpectRead("(2 + 3) * 4", 20);
    ExpectRead("10 - 4 - 3", 3);
    ExpectRead("max(1, 5, 3)", 5);
    ExpectRead("min(4, -2)", -2);
    ExpectRead("clamp(7, 0, 5)", 5);
    ExpectRead("lerp(0, 10, 0.25)", 2.5);
    ExpectRead("abs(-4)", 4);
    ExpectRead("-(2)", -2);
    ExpectRead("1 / 0", 0);  // division by zero reads 0
    ExpectRead("true * 3", 3);
    ExpectRead("false + 1", 1);
    ExpectRead("{\"type\": \"Math\", \"operator\": \"+\", \"args\": [1, 2]}", 3);
    {
        std::string error;
        auto bad = Value::FromJson(json::parse(R"({"type": "Math", "operator": "clamp", "args": [1, 2]})"), &error);
        Check(!bad && error.find("takes 3") != std::string::npos, "Math arity checked from JSON: " + error);
        bad = Value::FromJson("${Unset}", &error);
        Check(!bad && error.find("unresolved parameter") != std::string::npos, "unresolved ${param} is not a value");
    }

    // ── piecewise / steps ────────────────────────────────────────────────────
    ExpectRead("piecewise(160, 0, 0, 160, 25, 340, 50, 520, 75, 1000, 100)", 25);
    ExpectRead("piecewise(250, 0, 0, 160, 25, 340, 50, 520, 75, 1000, 100)", 37.5);
    ExpectRead("piecewise(760, 0, 0, 160, 25, 340, 50, 520, 75, 1000, 100)", 87.5);
    ExpectRead("piecewise(-5, 0, 0, 160, 25, 340, 50)", 0);      // flat below the first point
    ExpectRead("piecewise(5000, 0, 0, 160, 25, 340, 50)", 50);   // and above the last
    ExpectRead("piecewise(85, 0, 50, 50, 25, 110, 0)", 25.0 * 25.0 / 60.0);  // a falling scale (110 full = 0)
    ExpectRead("piecewise(80, 110, 0, 50, 25, 0, 50)", 25.0 * 30.0 / 60.0);  // falling x's work too
    ExpectRead("piecewise(50, 0, 0, 50, 10, 50, 20, 100, 30)", 10);          // a step: the first point at x
    ExpectRead("steps(49, 25, 50, 75)", 1);
    ExpectRead("steps(50, 25, 50, 75)", 2);
    ExpectRead("steps(100, 25, 50, 75)", 3);
    ExpectRead("steps(-1, 25, 50, 75)", 0);
    ExpectValueRoundTrip("piecewise($x, 0, 0, 40, 25, 120, 100)", "piecewise($x, 0, 0, 40, 25, 120, 100)");
    ExpectValueRoundTrip("steps($hunger, 25, 50, 75)", "steps($hunger, 25, 50, 75)");
    ExpectError("piecewise(1, 0, 0, 1)", "two or more x, y points", true);
    ExpectError("piecewise(1, 0, 0)", "two or more x, y points", true);
    ExpectError("steps(1)", "one or more thresholds", true);
    ExpectError("clamp(1, 2)", "takes 3", true);
    {
        std::string error;
        auto bad = Value::FromJson(json::parse(R"({"type": "Math", "operator": "piecewise", "args": [1, 0, 0, 1, 1, 2]})"), &error);
        Check(!bad && error.find("points") != std::string::npos, "piecewise arity checked from JSON: " + error);
    }

    // ── Named values ─────────────────────────────────────────────────────────
    auto& registry = Registry::Get();
    Define("base", "2 + 3");
    Define("doubled", "$base * 2");
    ExpectRead("$doubled", 10);
    ExpectRead("doubled", 10);  // a bare name compiles as $name
    ExpectRead("$BASE", 5);     // names ignore case
    Check(EvaluateCondition("$doubled > 9") && !EvaluateCondition("$doubled > 10"), "named values in conditions");

    // A compiled $name follows redefinitions
    {
        std::string error;
        auto doubled = Expression::CompileValueAny("$doubled", &error);
        EvaluationContext context(nullptr);
        Define("base", "7");
        Check(doubled && doubled->Read(context).value == 14, "redefinition reaches compiled references");
    }

    {
        const auto missing = Read("$nothing_here");
        Check(missing.status == ValueStatus::Unavailable && missing.reason.find("no value named") != std::string::npos,
              "unknown name reads Unavailable");
        Check(!EvaluateCondition("$nothing_here > -1000") && !EvaluateCondition("$nothing_here < 1000"),
              "comparisons with an unavailable side are false");
    }

    Define("loop_a", "$loop_b + 1");
    Define("loop_b", "$loop_a + 1");
    {
        const auto loop = Read("$loop_a");
        Check(loop.status == ValueStatus::Error && loop.reason.find("circular") != std::string::npos,
              "circular named values report an error");
        std::string error;
        auto value = Expression::CompileValueAny("$loop_a", &error);
        Check(value && HasDependency(value->GetEventDependencies(), EventDependency::Value),
              "dependencies of a cycle terminate");
    }

    Define("with_fallback", json{{"expr", "$nothing_here"}, {"fallback", 7}, {"unit", "%"}, {"range", {0, 100}}});
    {
        const auto r = Read("$with_fallback");
        Check(r.status == ValueStatus::Unavailable && r.value == 7, "fallback is read while unavailable");
        auto def = registry.FindNamedValue("with_fallback");
        Check(def && def->info.unit == "%" && def->info.max == 100.0, "unit and range parsed");
    }

    Define("gated", json{{"expr", "5"}, {"enabledWhen", "false"}});
    Check(Read("$gated").status == ValueStatus::Unavailable, "enabledWhen false makes the value unavailable");

    // Scope inference
    Define("world_hour", "Time");
    Define("actor_health", "ActorValue(Health, usePercentage=true)");
    Define("constant_two", "2");
    Define("forced_actor", json{{"expr", "2"}, {"scope", "actor"}});
    Check(registry.FindNamedValue("world_hour")->global, "Time is global");
    Check(!registry.FindNamedValue("actor_health")->global, "actor values are per actor");
    Check(registry.FindNamedValue("constant_two")->global, "literals are global");
    Check(!registry.FindNamedValue("forced_actor")->global, "scope override wins");

    {
        std::string error;
        Check(!NamedValueDef::Parse("bad name", "1", "t", &error), "names must be identifiers");
        Check(!NamedValueDef::Parse("empty", json::object(), "t", &error) && error.find("expr") != std::string::npos,
              "a definition needs an expression or pushed");
        Check(!NamedValueDef::Parse("broken", "1 +", "t", &error), "invalid expressions are rejected");
    }

    // Availability: unknown names suspend, known ones are ready
    {
        std::string error;
        auto known = Expression::CompileValueAny("$doubled + 1", &error);
        auto unknown = Expression::CompileValueAny("$nothing_here + 1", &error);
        Check(known && Availability::Get().CheckValue(*known).Ready(), "known named value is available");
        Check(unknown && !Availability::Get().CheckValue(*unknown).Ready(), "unknown named value is unavailable");
        auto compare = Expression::CompileAny("$nothing_here > 1", &error);
        Check(compare && !Availability::Get().Check(*compare).Ready(), "Compare walks its values for availability");
    }

    // requiresDlls: the DLL must be loaded in this process
    Define("needs_kernel", json{{"expr", "1"}, {"requiresDlls", {"kernel32.dll"}}});
    Define("needs_missing", json{{"expr", "1"}, {"requiresDlls", {"NoSuchNative.dll"}}});
    {
        std::string error;
        auto present = Expression::CompileValueAny("$needs_kernel", &error);
        auto missing = Expression::CompileValueAny("$needs_missing", &error);
        Check(present && Availability::Get().CheckValue(*present).Ready(), "a loaded DLL satisfies requiresDlls");
        const auto status = missing ? Availability::Get().CheckValue(*missing) : AvailabilityStatus{};
        Check(!status.Ready() && status.reason.find("NoSuchNative.dll") != std::string::npos,
              "a missing DLL makes the value unavailable: " + status.reason);
    }

    // ── Pushed values ────────────────────────────────────────────────────────
    auto& pushed = PushedValues::Get();
    Check(Read("$mood").status == ValueStatus::Unavailable, "never pushed = unavailable");
    Check(pushed.Set(nullptr, "mood", 0.5), "push to an undeclared name");
    ExpectRead("$mood", 0.5);
    Check(registry.FindNamedValue("mood") && registry.FindNamedValue("mood")->source == "implicit",
          "push registers an implicit value");
    Check(!pushed.Set(nullptr, "not valid", 1), "invalid names cannot be pushed");

    Check(pushed.SetGlobal("world_mood", 3), "global push");
    ExpectRead("$world_mood", 3);

    Define("hungry", json{{"pushed", {{"default", 1}, {"timeoutSeconds", 600}}}, {"range", {0, 1}}});
    ExpectRead("$hungry", 1);  // the pushed default before any push
    pushed.Set(nullptr, "hungry", 0.25);
    ExpectRead("$hungry", 0.25);
    pushed.Set(nullptr, "hungry", 0.75, 0.001);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ExpectRead("$hungry", 1);  // timed out, back to the default

    // A push overrides an expression while active
    Define("overridable", json{{"expr", "10"}, {"pushed", true}});
    ExpectRead("$overridable", 10);
    pushed.Set(nullptr, "overridable", 4);
    ExpectRead("$overridable", 4);
    pushed.Clear(nullptr, "overridable");
    ExpectRead("$overridable", 10);

    // graceSeconds holds the last good value after a failed read
    Define("grace_source", json{{"pushed", true}});
    Define("graced", json{{"expr", "$grace_source"}, {"graceSeconds", 60}});
    Define("ungraced", json{{"expr", "$grace_source"}});
    pushed.Set(nullptr, "grace_source", 5);
    ExpectRead("$graced", 5);
    pushed.Clear(nullptr, "grace_source");
    ExpectRead("$graced", 5);
    Check(Read("$ungraced").status == ValueStatus::Unavailable, "without grace a failed read is unavailable");

    // Reasons for a missing push
    Define("reasoned", json{{"pushed", true}});
    Check(Read("$reasoned").reason.find("nothing pushed") != std::string::npos, "never pushed says so");
    pushed.Set(nullptr, "reasoned", 1, 0.001);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    Check(Read("$reasoned").reason.find("expired") != std::string::npos, "an expired push says so: " + Read("$reasoned").reason);
    pushed.Set(nullptr, "reasoned", 1);
    pushed.Clear(nullptr, "reasoned");
    Check(Read("$reasoned").reason.find("cleared") != std::string::npos, "a cleared push says so");

    pushed.ClearAll();
    Check(Read("$mood").status == ValueStatus::Unavailable, "ClearAll forgets pushes (nothing is saved)");

    // ── Three-valued enabledWhen: waiting is not "disabled" ─────────────────
    TypeBuilder("TestPendingValue", "Test pending value", "other")
        .Value({})
        .Compare("value")
        .Factory([](const json& j) {
            return std::make_unique<CallbackValueCondition>(
                "pending", [](const EvaluationContext&) { return ValueResult::Fail(ValueStatus::Pending, "waiting for test"); },
                EventDependency::Value, j, "value");
        })
        .Register(registry);
    Define("tri_pending", json{{"expr", "5"}, {"enabledWhen", "TestPendingValue == 0"}});
    Define("tri_false", json{{"expr", "5"}, {"enabledWhen", "false && TestPendingValue == 0"}});
    Define("tri_true", json{{"expr", "5"}, {"enabledWhen", "true || TestPendingValue == 0"}});
    Define("tri_not", json{{"expr", "5"}, {"enabledWhen", "!(TestPendingValue == 0)"}});
    Define("tri_compare", json{{"expr", "5"}, {"enabledWhen", "TestPendingValue * 2 > 1"}});
    {
        const auto pending = Read("$tri_pending");
        Check(pending.status == ValueStatus::Pending && pending.reason == "waiting for test",
              "undecided enabledWhen reads Pending with its reason (" + pending.reason + ")");
        Check(Read("$tri_false").status == ValueStatus::Unavailable, "false && pending is decided false");
        ExpectRead("$tri_true", 5);  // true || pending is decided true
        Check(Read("$tri_not").status == ValueStatus::Pending, "not pending stays pending");
        Check(Read("$tri_compare").status == ValueStatus::Pending, "Compare over a pending value is pending");
        Check(Read("TestPendingValue == 0").status == ValueStatus::Pending, "a pending condition read as a value is pending");
        Check(!EvaluateCondition("TestPendingValue == 0"), "plain Evaluate still treats pending as false");

        // NTJH finding: a comparison over a named value whose enabledWhen and expr are both
        // pending must read Pending, not OK 0
        Define("tri_both", json{{"expr", "TestPendingValue"}, {"enabledWhen", "TestPendingValue == 0"}});
        Check(Read("$tri_both").status == ValueStatus::Pending, "named value with pending gate and expr is pending");
        Check(Read("$tri_both > 80").status == ValueStatus::Pending, "a comparison over it read as a value is pending");

        // Unavailable propagates instead of collapsing to OK 0
        const auto unavailable = Read("$nothing_here > 1");
        Check(unavailable.status == ValueStatus::Unavailable && unavailable.reason.find("no value named") != std::string::npos,
              "a comparison over an unavailable value reads unavailable with its reason: " + unavailable.reason);
        Define("tri_unreadable_gate", json{{"expr", "5"}, {"enabledWhen", "$nothing_here > 0"}});
        const auto gate = Read("$tri_unreadable_gate");
        Check(gate.status == ValueStatus::Unavailable && gate.reason.find("enabledWhen cannot be read") != std::string::npos,
              "an unreadable enabledWhen says so: " + gate.reason);
        Define("tri_or_unreadable", json{{"expr", "5"}, {"enabledWhen", "true || $nothing_here > 0"}});
        ExpectRead("$tri_or_unreadable", 5);  // true || unavailable is decided
        Check(Read("!($nothing_here > 0)").status == ValueStatus::Unavailable, "not unavailable stays unavailable");
        Check(Read("TestPendingValue == 0 && $nothing_here > 0").status == ValueStatus::Pending, "pending wins over unavailable");
    }

    // ── PapyrusCall (syntax only; calls need the game) ──────────────────────
    ExpectConditionJson("PapyrusCall(SeverActionsNative, Native_Survival_GetHunger) > 80",
                        {{"type", "PapyrusCall"}, {"script", "SeverActionsNative"},
                         {"function", "Native_Survival_GetHunger"}, {"value", 80}, {"operator", ">"}});
    ExpectValueJson("PapyrusCall(SeverActions_Survival, GetHunger, form=\"0xD62|SeverActions.esp\", interval=2)",
                    {{"type", "PapyrusCall"}, {"script", "SeverActions_Survival"}, {"function", "GetHunger"},
                     {"form", "0xD62|SeverActions.esp"}, {"interval", 2}});
    {
        std::string error;
        auto condition = Expression::CompileAny("PapyrusCall(SeverActionsNative, Native_Survival_IsExcluded) == 0", &error);
        Check(condition && Expression::ToExpression(*condition) ==
                               "PapyrusCall(SeverActionsNative, Native_Survival_IsExcluded) == 0.0",
              "PapyrusCall prints: " + (condition ? Expression::ToExpression(*condition) : error));
        auto value = Expression::CompileValueAny("PapyrusCall(SeverActionsNative, Native_Survival_GetHunger) / 100", &error);
        Check(value && HasDependency(value->GetEventDependencies(), EventDependency::Value),
              "PapyrusCall values depend on Value (results arrive as events)");
        // The SeverActions definition shared with NTJH must parse
        Define("vampire_hunger_example", json::parse(R"json({
            "label": "Vampire hunger (SeverActions)", "unit": "%", "range": [0, 100],
            "expr": "PapyrusCall(SeverActionsNative, Native_Survival_GetHunger, interval=2)",
            "enabledWhen": "ScriptValue(SeverActions_Survival, Enabled, bool, true, form=\"0xD62|SeverActions.esp\") && ScriptValue(SeverActions_Survival, HungerEnabled, bool, true, form=\"0xD62|SeverActions.esp\") && PapyrusCall(SeverActionsNative, Native_Survival_IsExcluded, interval=10) == 0",
            "requiresPlugins": ["SeverActions.esp"], "graceSeconds": 30, "fallback": 0
        })json"));
        auto hungerDef = Registry::Get().FindNamedValue("vampire_hunger_example");
        Check(hungerDef && !hungerDef->global && hungerDef->enabledWhen && hungerDef->graceSeconds == 30.0,
              "SeverActions hunger definition parses as a per-actor value");
        auto plugin = Expression::CompileValueAny(
            "PapyrusCall(SeverActions_Survival, GetHunger, form=\"0xD62|SeverActions.esp\")", &error);
        Check(plugin && !Availability::Get().CheckValue(*plugin).Ready(),
              "a bound form's plugin is required (SeverActions.esp is not loaded in tests)");
    }

    // ── Compare and parameters ───────────────────────────────────────────────
    {
        std::string error;
        auto condition = Expression::CompileAny("$doubled >= ${Threshold}", &error);
        Check(condition != nullptr, "compile parameterized Compare: " + error);
        if (condition) {
            Check(!condition->Validate().empty(), "unresolved parameter is a validation error");
            auto resolved = condition->ResolveParameters({{"Threshold", "14"}});
            EvaluationContext context(nullptr);
            Check(resolved && resolved->Evaluate(context), "Compare resolves ${param}");
            Check(resolved && resolved->ToJson()["right"] == 14, "resolved parameter is a number");
        }
        auto again = Expression::CompileAny(Expression::ToExpression(*Expression::CompileAny("$a * 2 <= 3", &error)), &error);
        Check(again && Expression::ToExpression(*again) == "$a * 2 <= 3", "Compare prints and recompiles");
    }

    // ── Catalog ──────────────────────────────────────────────────────────────
    {
        const auto catalog = registry.GetCatalog();
        Check(catalog.contains("namedValues") && catalog["namedValues"].is_array(), "catalog lists named values");
        bool sawActorValue = false;
        for (const auto& t : catalog["types"]) {
            if (t["name"] == "ActorValue") sawActorValue = t.contains("value");
        }
        Check(sawActorValue, "numeric types carry value metadata");
        bool sawDoubled = false;
        for (const auto& v : catalog["namedValues"]) sawDoubled |= v["name"] == "doubled";
        Check(sawDoubled, "named value appears in the catalog");
    }

    // ── Catalog organisation (v6) ────────────────────────────────────────────
    {
        std::string error;
        Check(registry.RegisterCategory({"survival", "Survival", 50, "", "", "Food, sleep, cold"}, "SeverActions", &error),
              "register a topical category");
        Check(registry.RegisterCategory({"survival", "Hunger Things", 60, "", "🍖", ""}, "NoTalkJustHands", &error),
              "registering an existing id is reuse");
        Check(registry.RegisterCategory({"survival.hunger", "", 10, "survival"}, "SeverActions", &error),
              "a child category with a label from its id");
        Check(registry.RegisterCategory({"loop_a", "A", 1, "loop_b"}, "t", &error) &&
                  !registry.RegisterCategory({"loop_b", "B", 1, "loop_a"}, "t", &error),
              "a parent cycle is refused");
        registry.RegisterSource("NoTalkJustHands", "No Talk Just Hands");

        Define("cat_hunger", json{{"expr", "3"}, {"category", "survival.hunger"}, {"owner", "SeverActions"},
                                  {"tags", {"food"}}, {"icon", "🩸"}, {"description", "How hungry"}});
        Define("cat_private", json{{"expr", "1"}, {"category", "relationships"}, {"owner", "NoTalkJustHands"},
                                   {"offer", "owner"}});
        NamedConditionInfo info;
        info.source = "value-tests";
        info.owner = "NoTalkJustHands";
        info.label = "Talking";
        info.category = "speech";
        registry.RegisterNamed("cat_talking", std::make_shared<ConstantCondition>(true), info);

        auto find = [](const json& list, const char* key, const std::string& id) -> json {
            for (const auto& item : list) {
                if (item.value(key, "") == id) return item;
            }
            return nullptr;
        };
        const auto all = registry.GetCatalog();
        const auto survival = find(all["categories"], "id", "survival");
        Check(survival.is_object() && survival["label"] == "Survival" && survival["source"] == "SeverActions" &&
                  survival["description"] == "Food, sleep, cold" && survival["icon"] == "🍖",
              "first registrant keeps the label, reuse fills the icon: " + survival.dump());
        const auto hunger = find(all["categories"], "id", "survival.hunger");
        Check(hunger.is_object() && hunger["label"] == "Hunger" && hunger["parent"] == "survival", "child category: " + hunger.dump());
        const auto relationships = find(all["categories"], "id", "relationships");
        Check(relationships.is_object() && relationships["label"] == "Relationships" && !relationships.contains("parent"),
              "an unknown category id is created at the top level, labelled from the id");

        const auto value = find(all["namedValues"], "name", "cat_hunger");
        Check(value.is_object() && value["category"] == "survival.hunger" && value["source"] == "SeverActions" &&
                  value["sourceLabel"] == "SeverActions" && value["offer"] == "everywhere" && value["icon"] == "🩸",
              "named value carries category, origin and offer: " + value.dump());
        const auto tags = value.value("tags", json::array());
        Check(std::find(tags.begin(), tags.end(), "food") != tags.end() &&
                  std::find(tags.begin(), tags.end(), "value") != tags.end() &&
                  std::find(tags.begin(), tags.end(), "global") != tags.end(),
              "free and automatic tags: " + tags.dump());
        const auto talking = find(all["namedConditions"], "id", "cat_talking");
        Check(talking.is_object() && talking["label"] == "Talking" && talking["category"] == "speech" &&
                  talking["sourceLabel"] == "No Talk Just Hands",
              "named condition carries metadata and the origin label: " + talking.dump());
        const auto level = find(all["types"], "name", "Level");
        Check(level.is_object() && level["source"] == "ConditionEngine" && level["category"] == "actor" &&
                  level["offer"] == "everywhere",
              "built-in types keep their category and show ConditionEngine as origin");
        const auto logical = find(all["types"], "name", "Logical");
        Check(logical.is_object() && logical["offer"] == "nowhere" && logical["hidden"] == true, "hidden = offer nowhere");
        Check(find(all["sources"], "id", "NoTalkJustHands").value("label", "") == "No Talk Just Hands",
              "sources list carries labels");

        // A picker's view
        const auto other = registry.GetCatalog(json{{"for", "OtherMod"}});
        Check(find(other["namedValues"], "name", "cat_private").is_null(), "owner-only values are absent from other pickers");
        Check(find(other["types"], "name", "Logical").is_null(), "expression-only types are not offered");
        Check(find(other["namedValues"], "name", "cat_hunger").is_object(), "everywhere values are offered");
        const auto own = registry.GetCatalog(json{{"for", "notalkjusthands"}});
        Check(find(own["namedValues"], "name", "cat_private").is_object(), "owner-only values are offered to their owner");
        const auto everything = registry.GetCatalog(json{{"for", "OtherMod"}, {"offered", false}});
        Check(find(everything["namedValues"], "name", "cat_private").is_object(), "offered:false shows everything");
        const auto valuesOnly = registry.GetCatalog(json{{"include", {"values"}}});
        Check(valuesOnly["types"].empty() && !valuesOnly["namedValues"].empty(), "include limits the sections");

        registry.UnregisterNamed("cat_talking");
    }

    // ── Providers: combined values by priority (issue 089) ───────────────────
    {
        Define("prov_low", json{{"pushed", true}, {"provides", "pv_hunger"}, {"priority", 10}});
        Define("prov_high", json{{"pushed", true}, {"provides", {"pv_hunger"}}, {"priority", 50}});
        Check(registry.FindNamedValue("pv_hunger") != nullptr, "a source declares its combined value implicitly");
        auto r = Read("$pv_hunger");
        Check(r.status == ValueStatus::Unavailable && r.reason.find("prov_high") != std::string::npos &&
                  r.reason.find("prov_low") != std::string::npos,
              "nothing available lists the sources tried: " + r.reason);
        pushed.Set(nullptr, "prov_low", 10);
        r = Read("$pv_hunger");
        Check(r.Ok() && r.value == 10 && r.FromText() == "prov_low", "the only available source answers");
        pushed.Set(nullptr, "prov_high", 70);
        r = Read("$pv_hunger");
        Check(r.Ok() && r.value == 70 && r.FromText() == "prov_high", "the higher priority source wins");
        pushed.Clear(nullptr, "prov_high");
        r = Read("$pv_hunger");
        Check(r.Ok() && r.value == 10 && r.FromText() == "prov_low", "falls back to the next source");

        Define("prov_pending", json{{"expr", "TestPendingValue"}, {"provides", "pv_hunger"}, {"priority", 100}});
        r = Read("$pv_hunger");
        Check(r.status == ValueStatus::Pending && r.reason.find("prov_pending") != std::string::npos,
              "a pending higher source holds the result pending: " + r.reason);
        registry.UnregisterNamedValue("prov_pending");

        Define("prov_tie_b", json{{"expr", "2"}, {"provides", "pv_tie"}, {"priority", 5}});
        Define("prov_tie_a", json{{"expr", "1"}, {"provides", "pv_tie"}, {"priority", 5}});
        Check(Read("$pv_tie").FromText() == "prov_tie_a", "ties are broken by name");

        Define("pv_cold", json{{"expr", "5"}, {"label", "Cold"}});
        Define("cold_src", json{{"pushed", true}, {"provides", "pv_cold"}});
        r = Read("$pv_cold");
        Check(r.Ok() && r.value == 5 && r.FromText().find("own expression") != std::string::npos,
              "a declared combined value's own expr is the last source");
        pushed.Set(nullptr, "cold_src", 9);
        Check(Read("$pv_cold").value == 9, "a provider beats the own expression");

        Define("pv_fatigue", json{{"combined", true}, {"fallback", 3}});
        r = Read("$pv_fatigue");
        Check(r.status == ValueStatus::Unavailable && r.value == 3 && r.reason.find("no source provides") != std::string::npos,
              "a combined value with no source reads its fallback");

        std::string error;
        Check(!NamedValueDef::Parse("self_source", json{{"expr", "1"}, {"provides", "self_source"}}, "t", &error),
              "a value cannot provide itself");

        auto value = Expression::CompileValueAny("$pv_hunger", &error);
        Check(value && Availability::Get().CheckValue(*value).Ready(), "a combined value is available while a source is");
        Check(Read("$pv_hunger > 5").value == 1, "combined values work in comparisons");

        const auto catalog = registry.GetCatalog();
        json combined;
        for (const auto& v : catalog["namedValues"]) {
            if (v["name"] == "pv_hunger") combined = v;
        }
        Check(combined.contains("providers") && combined["providers"].size() == 2 &&
                  combined["providers"][0]["name"] == "prov_high",
              "the catalog lists sources, highest first: " + combined.dump());
        pushed.ClearAll();
    }

    // ── First-match lists (issue 090) ────────────────────────────────────────
    {
        // A rule whose state the test sets
        struct Switch : Condition {
            TriState* state;
            EventDependency deps;
            Switch(TriState* s, EventDependency d) : state(s), deps(d) {}
            bool Evaluate(const EvaluationContext&) const override { return *state == TriState::True; }
            TriResult EvaluateTri(const EvaluationContext&) const override { return {*state, "test"}; }
            std::string GetDescription() const override { return "switch"; }
            nlohmann::json ToJson() const override { return {{"type", "Constant"}, {"value", true}}; }
            std::unique_ptr<Condition> Clone() const override { return std::make_unique<Switch>(state, deps); }
            EventDependency GetEventDependencies() const override { return deps; }
        };
        TriState a = TriState::False, b = TriState::True, c = TriState::True;
        FirstMatchList list({std::make_shared<Switch>(&a, EventDependency::Time),
                             std::make_shared<Switch>(&b, EventDependency::Weather),
                             std::make_shared<Switch>(&c, EventDependency::Location)});
        EvaluationContext context(nullptr);
        Check(list.EvaluateAll(context) == 1 && list.EvaluationCount() == 2, "first true rule wins, the rest unread");
        auto count = list.EvaluationCount();
        Check(list.EvaluateChanged(context, EventDependency::Location) == 1 && list.EvaluationCount() == count,
              "a change below the winner evaluates nothing");
        a = TriState::True;
        Check(list.EvaluateChanged(context, EventDependency::Time) == 0, "a rule above that turns true wins");
        a = TriState::False;
        Check(list.EvaluateChanged(context, EventDependency::Time) == 1, "the winner turning false scans down");
        a = TriState::Pending;
        Check(list.EvaluateChanged(context, EventDependency::Time) == 1, "a pending rule above holds the winner");
        a = TriState::False;
        b = TriState::False;
        Check(list.EvaluateChanged(context, EventDependency::Weather) == 2, "the next true rule below takes over");
        c = TriState::Unavailable;
        Check(list.EvaluateChanged(context, EventDependency::Location) == -1, "unavailable counts as not matching");
        Check(HasDependency(list.ListenedDependencies(), EventDependency::Location), "no winner: listens to every rule");
        c = TriState::True;
        list.EvaluateAll(context);
        Check(HasDependency(list.ListenedDependencies(), EventDependency::Location),
              "winner 2: listens to rules 0..2");
        b = TriState::True;
        list.EvaluateAll(context);
        Check(!HasDependency(list.ListenedDependencies(), EventDependency::Location),
              "winner 1: the rule below is not listened to");

        // Keyed value changes wake only rules that read the value
        Define("lk_a", json{{"pushed", true}});
        Define("lk_b", json{{"pushed", true}});
        std::string error;
        FirstMatchList keyed({std::shared_ptr<const Condition>(Expression::CompileAny("$lk_a > 5", &error)),
                              std::shared_ptr<const Condition>(Expression::CompileAny("$lk_b > 5", &error))});
        pushed.Set(nullptr, "lk_b", 9);
        Check(keyed.EvaluateAll(context) == 1, "keyed list: second rule wins");
        pushed.Set(nullptr, "lk_a", 9);
        const std::set<std::string> other{"$somethingelse"};
        count = keyed.EvaluationCount();
        Check(keyed.EvaluateChanged(context, EventDependency::Value, &other) == 1 && keyed.EvaluationCount() == count,
              "a change to another value evaluates nothing");
        const std::set<std::string> touched{"$lk_a"};
        Check(keyed.EvaluateChanged(context, EventDependency::Value, &touched) == 0, "the keyed change reaches its rule");
        Check(keyed.ListensToKeys(touched) && !keyed.ListensToKeys(std::set<std::string>{"$lk_b"}),
              "winner 0 listens only to its own keys");
        pushed.ClearAll();

        const auto keys = CollectValueKeys(Expression::CompileAny("$pv_hunger > 1", &error)->ToJsonComplete());
        Check(keys.contains("$pv_hunger") && keys.contains("$prov_high") && keys.contains("$prov_low"),
              "value keys follow combined values to their sources");
    }

    // ── Constant folding and cheapest-first order (issue 088 steps 4-5) ──────
    {
        static int cheapCalls = 0, expensiveCalls = 0;
        TypeBuilder("TestCheapFalse", "Cheap false", "other")
            .Cost(1)
            .Factory([](const json& j) {
                return std::make_unique<CallbackCondition>(
                    "cheap", [](const EvaluationContext&) { ++cheapCalls; return false; }, EventDependency::Time, j);
            })
            .Register(registry);
        TypeBuilder("TestExpensiveTrue", "Expensive true", "other")
            .Cost(5)
            .Factory([](const json& j) {
                return std::make_unique<CallbackCondition>(
                    "expensive", [](const EvaluationContext&) { ++expensiveCalls; return true; }, EventDependency::Polled, j);
            })
            .Register(registry);
        std::string error;
        EvaluationContext context(nullptr);

        auto ordered = Expression::CompileAny("TestExpensiveTrue && TestCheapFalse", &error);
        Check(ordered && !ordered->Evaluate(context) && cheapCalls == 1 && expensiveCalls == 0,
              "the cheap false term short-circuits the expensive one written before it");
        Check(Expression::ToExpression(*ordered) == "TestExpensiveTrue && TestCheapFalse", "the written order is kept");
        Check(ordered->Cost() == 6, "a group costs its parts");

        cheapCalls = 0;
        auto duplicated = Expression::CompileAny("TestCheapFalse || TestCheapFalse || TestCheapFalse", &error);
        Check(duplicated && !duplicated->Evaluate(context) && cheapCalls == 1, "duplicate children are evaluated once");

        auto decided = Expression::CompileAny("false && TestExpensiveTrue", &error);
        Check(decided && decided->ConstantResult() == false, "false && x folds to false");
        expensiveCalls = 0;
        Check(!decided->Evaluate(context) && expensiveCalls == 0, "a folded group reads nothing");
        Check(Expression::CompileAny("true || TestCheapFalse", &error)->ConstantResult() == true, "true || x folds to true");
        Check(!Expression::CompileAny("true && TestCheapFalse", &error)->ConstantResult(), "neutral constants do not fold");
        Check(Expression::CompileAny("!(true && true)", &error)->ConstantResult() == false, "not of a constant folds");
        Check(Expression::CompileAny("1 < 2", &error)->ConstantResult() == true, "literal comparisons fold");
        Check(Expression::CompileValueAny("2 + 3 * 4", &error)->ConstantValue() == 14.0, "literal math folds");
        Check(!Expression::CompileValueAny("$doubled + 1", &error)->ConstantValue(), "named values never fold");

        // Built-in cost classes
        Check(Expression::CompileAny("Time(20, 6)", &error)->Cost() == 1, "Time is a snapshot read");
        Check(Expression::CompileAny("PapyrusCall(A, B)", &error)->Cost() == 5, "PapyrusCall is the most expensive");
    }

    // ── StorageUtil (syntax only; reading needs the game) ────────────────────
    {
        ExpectConditionJson("StorageUtil(hunger) > 50",
                            {{"type", "StorageUtil"}, {"key", "hunger"}, {"value", 50}, {"operator", ">"}});
        ExpectValueJson("StorageUtil(mood, kind=int, global=true)",
                        {{"type", "StorageUtil"}, {"key", "mood"}, {"kind", "int"}, {"global", true}});
        std::string error;
        auto condition = Expression::CompileAny("StorageUtil(hunger, interval=5) > 50", &error);
        Check(condition && Expression::ToExpression(*condition) == "StorageUtil(hunger, interval=5.0) > 50.0",
              "StorageUtil prints as itself: " + (condition ? Expression::ToExpression(*condition) : error));
        Check(condition && condition->Cost() == 5, "StorageUtil costs like a Papyrus call");
        const auto a = CollectValueKeys(json{{"type", "StorageUtil"}, {"key", "a"}});
        const auto b = CollectValueKeys(json{{"type", "StorageUtil"}, {"key", "b"}});
        Check(a.size() == 1 && b.size() == 1 && *a.begin() != *b.begin(), "each key is its own cached call");
        const auto g = CollectValueKeys(json{{"type", "StorageUtil"}, {"key", "a"}, {"global", true}});
        Check(*g.begin() != *a.begin(), "global and per-actor storage are different calls");
        ExpectConditionJson("PapyrusCall(MyScript, GetLevel, args=[\"x\", 2]) > 1",
                            {{"type", "PapyrusCall"}, {"script", "MyScript"}, {"function", "GetLevel"},
                             {"args", {"x", 2}}, {"value", 1}, {"operator", ">"}});
    }

    // ── Explain ──────────────────────────────────────────────────────────────
    Check(Explain::FormatNumber(8.290821075439453) == "8.29" && Explain::FormatNumber(42) == "42" &&
              Explain::FormatNumber(0.5) == "0.5" && Explain::FormatNumber(-0.001) == "0",
          "explanations round numbers to 2 decimals");
    {
        std::string error;
        EvaluationContext context(nullptr);
        Define("ex_h", json{{"pushed", true}, {"unit", "%"}});
        pushed.Set(nullptr, "ex_h", 42);

        auto condition = Expression::CompileAny("TestExpensiveTrue && $ex_h > 80", &error);
        const auto e = Explain::ExplainCondition(*condition, context);
        Check(e["state"] == "false" && e["children"].size() == 2, "explains the group: " + e.dump());
        Check(e["children"][0]["state"] == "skipped", "the expensive part was not read (cheaper part decided)");
        const auto compare = e["children"][1];
        Check(compare["state"] == "false" && compare["children"][0]["value"] == 42 && compare["children"][1]["value"] == 80,
              "the comparison shows both numbers: " + compare.dump());
        const auto text = Explain::ToText(e);
        Check(text.find("[F] TestExpensiveTrue && $ex_h > 80") != std::string::npos &&
                  text.find("$ex_h = 42%") != std::string::npos && text.find("[-] TestExpensiveTrue") != std::string::npos,
              "text form:\n" + text);

        const auto folded = Explain::ExplainCondition(*Expression::CompileAny("false && TestCheapFalse", &error), context);
        Check(folded["state"] == "false" && folded.contains("note"), "a folded group says so");

        pushed.Set(nullptr, "prov_low", 10);
        auto combined = Expression::CompileValueAny("$pv_hunger", &error);
        const auto v = Explain::ExplainValue(*combined, context);
        Check(v["value"] == 10 && v["from"] == "prov_low" && v["children"].size() == 2,
              "a combined value lists its sources and which answered: " + v.dump());

        auto missing = Expression::CompileAny("$nothing_here > 1", &error);
        const auto m = Explain::ExplainCondition(*missing, context);
        Check(m["state"] == "unavailable" && m.value("reason", "").find("no value named") != std::string::npos,
              "an unavailable part explains why: " + m.dump());
        pushed.ClearAll();
    }

    // ── refreshOn (issue 089) ────────────────────────────────────────────────
    {
        Define("refreshed", json{{"expr", "1"}, {"refreshOn", {"Test_Tick", "Test_Food"}}});
        Define("refreshed_too", json{{"expr", "$refreshed + 1"}, {"refreshOn", {"test_tick"}}});
        Check(registry.ValuesRefreshedBy("TEST_TICK").size() == 2, "event names ignore case and index both values");
        Check(registry.ValuesRefreshedBy("Test_Food").size() == 1, "each listed event is indexed");
        Check(registry.ValuesRefreshedBy("Other_Event").empty(), "unlisted events refresh nothing");
        Check(RefreshOn::OnModEvent("Test_Tick", nullptr) == 2, "a mod event refreshes its values (no watches: no-op)");
        Check(registry.FindNamedValue("refreshed")->ToJson()["refreshOn"].size() == 2, "refreshOn appears in the catalog");
        registry.UnregisterNamedValue("refreshed_too");
        Check(registry.ValuesRefreshedBy("test_tick").size() == 1, "unregistering updates the index");
    }

    // ── Cached availability (issue 088 step 2) ───────────────────────────────
    {
        std::string error;
        auto value = Expression::CompileValueAny("$cache_probe + 1", &error);
        auto condition = Expression::CompileAny("$cache_probe > 0", &error);
        Check(value && !value->CheckAvailability().Ready(), "cached value availability starts unavailable");
        Check(condition && !condition->CheckAvailability().Ready(), "cached condition availability starts unavailable");
        Define("cache_probe", "5");  // a registration moves the revision
        Check(value->CheckAvailability().Ready(), "a named value registration refreshes cached value availability");
        Check(condition->CheckAvailability().Ready(), "and cached condition availability");
        registry.UnregisterNamedValue("cache_probe");
        Check(!condition->CheckAvailability().Ready(), "removal is seen too");

        auto group = std::make_unique<LogicalCondition>(LogicalOperator::And);
        group->AddChild(std::make_unique<ConstantCondition>(true));
        Check(group->CheckAvailability().Ready(), "group available");
        group->AddChild(Expression::CompileAny("$cache_probe > 0", &error));
        Check(!group->CheckAvailability().Ready(), "AddChild invalidates the cached result");
        auto copy = group->CloneComplete();
        Check(copy && !copy->CheckAvailability().Ready(), "a clone computes its own");

        // Host-resolved rules change without moving the revision: never cached
        static bool hostRuleExists = false;
        registry.AddNamedResolver([](const std::string& key) -> std::shared_ptr<const Condition> {
            if (key != "CacheHostRule" || !hostRuleExists) return nullptr;
            return std::make_shared<ConstantCondition>(true);
        });
        auto host = Expression::CompileAny("@CacheHostRule", &error);
        Check(host && !host->CheckAvailability().Ready(), "missing host rule is unavailable");
        hostRuleExists = true;  // the host adds it without telling the engine
        Check(host->CheckAvailability().Ready(), "host-resolved rules are re-checked, not cached");
    }

    // ── Stats (issue 088) ────────────────────────────────────────────────────
    {
        Stats::Reset();
        Stats::SetEnabled(true);
        std::string error;
        auto condition = Expression::CompileAny("$doubled > 9 && true", &error);
        EvaluationContext context(nullptr);
        Check(condition && condition->EvaluateAvailable(context), "stats test condition evaluates");
        Stats::SetEnabled(false);
        const auto snapshot = Stats::Snapshot();
        auto row = [&](const std::string& name) {
            for (const auto& t : snapshot["types"]) {
                if (t["name"] == name) return t;
            }
            return json();
        };
        Check(row("Compare").value("evaluations", 0) >= 1, "Compare evaluations counted: " + snapshot.dump());
        Check(row("NamedValue").value("reads", 0) >= 2, "named value reads counted ($doubled -> $base)");
        Check(row("Logical").value("evaluations", 0) == 1, "the root counted once");
        const auto logical = row("Logical");
        Check(logical.value("totalUs", 0.0) >= logical.value("selfUs", 0.0), "self time excludes children");
        Check(snapshot["availability"].value("checks", 0) >= 1, "availability checks counted");
        Stats::Reset();
        Check(Stats::Snapshot()["types"].empty(), "Reset clears per-type counters");
    }

    // ── A source whose plugin is not loaded is skipped unread ────────────────
    {
        Availability::Get().SetLoadedPlugins({"Skyrim.esm"});
        Define("skip_present", json{{"pushed", true}, {"provides", "skip_need"}, {"priority", 10}});
        Define("skip_absent", json{{"pushed", true}, {"provides", "skip_need"}, {"priority", 90},
                                   {"requiresPlugins", {"NotInstalled.esp"}}});
        PushedValues::Get().Set(nullptr, "skip_present", 12);
        PushedValues::Get().Set(nullptr, "skip_absent", 99);
        auto r = Read("$skip_need");
        Check(r.Ok() && r.value == 12 && r.FromText() == "skip_present",
              "a missing plugin's source is skipped: " + std::to_string(r.value) + " " + r.FromText());
        PushedValues::Get().Clear(nullptr, "skip_present");
        r = Read("$skip_need");
        Check(r.status == ValueStatus::Unavailable && r.reason.find("Plugin not loaded: NotInstalled.esp") != std::string::npos,
              "the skipped source is listed with why: " + r.reason);
        PushedValues::Get().Clear(nullptr, "skip_absent");
    }

    Check(registry.UnregisterSource("value-tests") > 0, "named values unregister by source");
    Check(registry.FindNamedValue("doubled") == nullptr, "named value removed");

    // ── Built-in survival preset (presets/ConditionEngine_Survival.json) ─────
    {
        const auto loaded = LoadBuiltinPresets();
        Check(loaded == 135, "built-in presets: survival 33 values and 20 conditions, weather 14 values and 30 conditions, "
                             "people 38 members, got " +
                                std::to_string(loaded));
        const char* values[] = {"hunger", "fatigue", "cold", "hunger_stage", "fatigue_stage", "cold_stage",
                                "frostfall_cold", "sunhelm_hunger", "sunhelm_fatigue", "sunhelm_cold",
                                "lastseed_hunger", "lastseed_fatigue", "ineed_hunger", "ineed_fatigue",
                                "survivalmode_hunger", "survivalmode_fatigue", "survivalmode_cold",
                                "severactions_hunger", "severactions_fatigue", "severactions_cold",
                                "warmth", "coverage", "frostfall_warmth", "frostfall_coverage", "dirt", "dirt_stage", "blood",
                                "bathing_dirt", "bathing_dirt_npc", "bathingoriginal_dirt", "dirtandblood_dirt",
                                "dirtandblood_blood", "keepitclean_dirt"};
        for (const char* name : values) {
            auto def = registry.FindNamedValue(name);
            Check(def != nullptr, std::string("built-in value $") + name);
            if (!def) continue;
            // HasMagicEffect checks its forms when validated, which needs the game's data
            const bool readsEffects = def->value && def->value->ToJson().dump().find("HasMagicEffect") != std::string::npos;
            for (const auto& problem : def->value && !readsEffects ? def->value->Validate() : std::vector<std::string>{}) {
                Check(false, std::string("$") + name + ": " + problem);
            }
            const bool source = !def->provides.empty();
            // Dirt and Blood reads magic effects that are absent on actors it doesn't dirty, so it needs no enabledWhen
            const bool allActors = std::string(name).starts_with("dirtandblood_");
            Check(!source || (def->value && (def->enabledWhen || allActors) && !def->requiresPlugins.empty()),
                  std::string("$") + name + " has an expression, an enabledWhen and its plugin");
        }
        auto order = [&](const std::string& need) {
            std::string names;
            for (const auto& source : registry.ProvidersOf(need)) names += (names.empty() ? "" : ",") + source->name;
            return names;
        };
        Check(order("hunger") == "sunhelm_hunger,lastseed_hunger,ineed_hunger,survivalmode_hunger,severactions_hunger",
              "hunger source order: " + order("hunger"));
        Check(order("fatigue") ==
                  "sunhelm_fatigue,lastseed_fatigue,ineed_fatigue,survivalmode_fatigue,severactions_fatigue",
              "fatigue source order: " + order("fatigue"));
        Check(order("cold") == "frostfall_cold,sunhelm_cold,survivalmode_cold,severactions_cold",
              "cold source order: " + order("cold"));
        Check(order("dirt") == "bathing_dirt,bathing_dirt_npc,bathingoriginal_dirt,dirtandblood_dirt,keepitclean_dirt",
              "dirt source order: " + order("dirt"));
        Check(order("warmth") == "frostfall_warmth" && order("blood") == "dirtandblood_blood", "warmth and blood sources");
        for (const char* name : {"HungerMild", "HungerModerate", "HungerSevere", "FatigueMild", "FatigueModerate",
                                 "FatigueSevere", "ColdMild", "ColdModerate", "ColdSevere", "WarmthPoor", "WarmthGood",
                                 "CoveragePoor", "CoverageGood", "OutsideUnderdressed", "OutsideGettingSoaked", "DirtMild",
                                 "DirtModerate", "DirtSevere", "BloodMild", "BloodSevere"}) {
            Check(registry.FindNamed(name) != nullptr, std::string("built-in condition @") + name);
        }
        // Nothing installed and no actor: the combined values read unavailable with their fallback
        auto r = Read("$hunger");
        Check(r.status == ValueStatus::Unavailable && r.value == 0 && r.reason.find("sunhelm_hunger") != std::string::npos,
              "no survival mod: $hunger unavailable, reason lists the sources: " + r.reason);
        r = Read("$cold_stage");
        Check(!r.Ok() && r.value == 0, "a stage of an unavailable need is not OK and reads its fallback");
    }
}
