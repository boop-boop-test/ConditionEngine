// ExpressionTests.cpp - Out-of-game tests for the expression parser/printer,
// the type registry and parameter substitution. Nothing here touches game state.

#include "ConditionEngine/ConditionEngine.h"
#include "ConditionEngine/Expression.h"
#include "ConditionEngine/Availability.h"
#include "ConditionEngine/Conditions/CallbackCondition.h"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace {
    using json = nlohmann::json;
    using namespace ConditionEngine;

    int g_failures = 0;
    int g_checks = 0;

    void Check(bool ok, const std::string& what) {
        ++g_checks;
        if (!ok) {
            ++g_failures;
            std::printf("FAIL: %s\n", what.c_str());
        }
    }

    json Parse(const std::string& src) {
        Expression::ParseError error;
        auto result = Expression::ParseToJson(src, &error);
        Check(result.has_value(), "parse '" + src + "': " + error.message);
        return result.value_or(json());
    }

    void ExpectJson(const std::string& src, const json& expected) {
        auto actual = Parse(src);
        Check(actual == expected, "json of '" + src + "'\n  got:      " + actual.dump() + "\n  expected: " + expected.dump());
    }

    void ExpectError(const std::string& src, const std::string& fragment) {
        Expression::ParseError error;
        auto result = Expression::ParseToJson(src, &error);
        Check(!result.has_value(), "expected error for '" + src + "'");
        Check(error.message.find(fragment) != std::string::npos,
              "error for '" + src + "' should mention '" + fragment + "', got '" + error.message + "'");
    }

    // text → json → text → json must be stable
    void ExpectRoundTrip(const std::string& src, const std::string& expectedText) {
        auto first = Parse(src);
        auto printed = Expression::FromJson(first);
        Check(printed == expectedText, "print '" + src + "'\n  got:      " + printed + "\n  expected: " + expectedText);
        auto second = Parse(printed);
        Check(first == second, "reparse '" + printed + "' changed the tree");
    }

    // text → Condition → ToJson → text (exercises real factories and ToJson)
    void ExpectCompiled(const std::string& src, const std::string& expectedText) {
        std::string error;
        auto condition = Expression::CompileAny(src, &error);
        Check(condition != nullptr, "compile '" + src + "': " + error);
        if (!condition) return;
        auto printed = Expression::ToExpression(*condition);
        Check(printed == expectedText, "compiled '" + src + "'\n  got:      " + printed + "\n  expected: " + expectedText);
        auto again = Expression::CompileAny(printed, &error);
        Check(again && again->ToJsonComplete() == condition->ToJsonComplete(),
              "recompile '" + printed + "' changed the condition");
    }
}

void RunValueTests(void (*check)(bool ok, const std::string& what));  // ValueTests.cpp
int RunBenchmark();                                                    // Benchmark.cpp

int main(int argc, char** argv) {
    // --bench: timing benchmark (issue 088), no checks
    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0) return RunBenchmark();

    // --dump-catalog <file> / --dump-schema <file>: write generated JSON and exit
    for (int i = 1; i + 1 < argc; i += 2) {
        nlohmann::json out;
        if (std::strcmp(argv[i], "--dump-catalog") == 0) out = Registry::Get().GetCatalog();
        else if (std::strcmp(argv[i], "--dump-schema") == 0) out = Registry::Get().GetJsonSchema();
        else continue;
        std::ofstream(argv[i + 1]) << out.dump(2) << "\n";
    }
    if (argc > 1) return 0;

    // ── Basic calls ──────────────────────────────────────────────────────────
    ExpectJson("Time(20, 6)", {{"type", "Time"}, {"startHour", 20}, {"endHour", 6}});
    ExpectJson("combat", {{"type", "Combat"}});
    ExpectJson("TimeCondition(1, 2)", {{"type", "Time"}, {"startHour", 1}, {"endHour", 2}});  // alias
    ExpectJson("Weather(Rainy)", {{"type", "Weather"}, {"weatherType", "Rainy"}});
    ExpectJson("Faction(\"0x00019809|Skyrim.esm\") >= 1",
               {{"type", "Faction"}, {"factionId", "0x00019809|Skyrim.esm"}, {"minRank", 1}, {"operator", ">="}});
    ExpectJson("ActorValue(Health, usePercentage=true) < 30",
               {{"type", "ActorValue"}, {"actorValue", "Health"}, {"usePercentage", true}, {"value", 30}, {"operator", "<"}});
    ExpectJson("HasSlotsEquipped([32, 37], matchMode=all)",
               {{"type", "HasSlotsEquipped"}, {"slots", {32, 37}}, {"matchMode", "all"}});
    ExpectJson("ActorValue(Health) < ${Threshold}",
               {{"type", "ActorValue"}, {"actorValue", "Health"}, {"value", "${Threshold}"}, {"operator", "<"}});
    ExpectJson("Random(25, salt='a b')", {{"type", "Random"}, {"chance", 25}, {"salt", "a b"}});
    ExpectJson("Temperature(-5.5)", {{"type", "Temperature"}, {"temperatureRange", -5.5}});

    // ── Named references and constants ───────────────────────────────────────
    ExpectJson("@IsNight", {{"type", "Rule"}, {"ruleName", "IsNight"}});
    ExpectJson("@\"Combat Ready\"", {{"type", "Rule"}, {"ruleName", "Combat Ready"}});
    ExpectJson("Rule(\"Combat Ready\")", {{"type", "Rule"}, {"ruleName", "Combat Ready"}});
    ExpectJson("true", {{"type", "Constant"}, {"value", true}});

    // ── Logic and precedence ─────────────────────────────────────────────────
    const json combat = {{"type", "Combat"}};
    const json swimming = {{"type", "Swimming"}};
    const json sleeping = {{"type", "Sleeping"}};
    ExpectJson("Combat || Swimming && Sleeping",
               {{"type", "Logical"}, {"operator", "Or"},
                {"children", {combat, {{"type", "Logical"}, {"operator", "And"}, {"children", {swimming, sleeping}}}}}});
    ExpectJson("Combat and Swimming and Sleeping",
               {{"type", "Logical"}, {"operator", "And"}, {"children", {combat, swimming, sleeping}}});
    ExpectJson("!Combat", {{"type", "Logical"}, {"operator", "Not"}, {"children", {combat}}});
    ExpectJson("not (Combat or Swimming)",
               {{"type", "Logical"}, {"operator", "Not"},
                {"children", {{{"type", "Logical"}, {"operator", "Or"}, {"children", {combat, swimming}}}}}});

    // ── Printing ─────────────────────────────────────────────────────────────
    ExpectRoundTrip("(Combat || Swimming) && Sleeping", "(Combat || Swimming) && Sleeping");
    ExpectRoundTrip("Combat || Swimming && Sleeping", "Combat || (Swimming && Sleeping)");  // clarifying parens
    ExpectRoundTrip("!(Combat && Swimming)", "!(Combat && Swimming)");
    ExpectRoundTrip("not   @IsNight", "!@IsNight");
    ExpectRoundTrip("ActorValue(Health, usePercentage=true) < 30", "ActorValue(Health, usePercentage=true) < 30");
    ExpectRoundTrip("Rule(\"Combat Ready\")", "Rule(\"Combat Ready\")");
    ExpectRoundTrip("Weather(\"true\")", "Weather(\"true\")");

    // ── Through real conditions (factory + ToJson) ───────────────────────────
    ExpectCompiled("Time(20, 6) && InteriorExterior(false)",
                   "Time(20.0, 6.0) && InteriorExterior(false)");
    ExpectCompiled("ActorValue(Health, usePercentage=true) < 30", "ActorValue(Health, usePercentage=true) < 30.0");
    ExpectCompiled("@IsNight", "@IsNight");
    ExpectCompiled("true", "true");
    ExpectCompiled("{\"type\": \"Combat\", \"expectedState\": false}", "Combat(false)");

    // ── New condition types (parse + factory + ToJson, no game access) ───────
    ExpectJson("Game(GetLevel) >= 20", {{"type", "Game"}, {"function", "GetLevel"}, {"value", 20}, {"operator", ">="}});
    ExpectJson("Game(WornHasKeyword, ArmorHeavy)",
               {{"type", "Game"}, {"function", "WornHasKeyword"}, {"param1", "ArmorHeavy"}});
    ExpectCompiled("Game(IsSneaking)", "Game(IsSneaking)");
    ExpectCompiled("Game(GetActorValuePercent, Health) < 0.5", "Game(GetActorValuePercent, Health) < 0.5");
    ExpectCompiled("Game(GetDistance, player, runOn=Target) < 500",
                   "Game(GetDistance, player, runOn=Target) < 500.0");
    ExpectCompiled("Sneaking && !InDialogue", "Sneaking(true) && !InDialogue(true)");
    ExpectCompiled("WeaponDrawn(false)", "WeaponDrawn(false)");
    ExpectCompiled("Level >= 20", "Level >= 20");
    ExpectCompiled("Level(10)", "Level >= 10");
    ExpectCompiled("Sex(Female)", "Sex(Female)");
    ExpectCompiled("HasKeyword([Vampire, ActorTypeUndead])", "HasKeyword([Vampire, ActorTypeUndead])");
    ExpectCompiled("WornHasKeyword(ClothingRich, matchAny=false)", "WornHasKeyword(ClothingRich, false)");
    ExpectCompiled("HasPerk(\"0x000BE127|Skyrim.esm\")", "HasPerk(\"0x000BE127|Skyrim.esm\")");
    ExpectCompiled("HasItem(Gold001)", "ItemCount(Gold001) >= 1");  // HasItem is ItemCount >= 1
    ExpectCompiled("ItemCount(Torch01) < 2", "ItemCount(Torch01) < 2");
    ExpectCompiled("EquippedWeaponType([Bow, Crossbow])", "EquippedWeaponType([Bow, Crossbow])");
    ExpectCompiled("Holding(Spell, hand=Left)", "EquippedWeaponType(Spell, Left)");
    ExpectCompiled("Worldspace(DLC2SolstheimWorld)", "Worldspace(DLC2SolstheimWorld)");
    ExpectCompiled("Hold(Whiterun)", "Hold(Whiterun)");
    ExpectCompiled("InLocation(\"Dragonsreach\", false)", "InLocation(Dragonsreach, false)");
    ExpectCompiled("InCell(WhiterunBanneredMare)", "InCell(WhiterunBanneredMare)");
    ExpectCompiled("IsPluginLoaded(\"Frostfall.esp\")", "IsPluginLoaded(\"Frostfall.esp\")");
    ExpectCompiled("Holiday([Saturalia, NewLifeFestival])", "Holiday([Saturalia, NewLifeFestival])");
    Check(HolidayCondition::HolidayOn(11, 25) == "Saturalia", "Saturalia is 25 Evening Star");
    Check(HolidayCondition::HolidayOn(3, 1).empty(), "no holiday on 1 Rain's Hand");
    ExpectError("Sneaking > 1", "does not support comparisons");

    // ── Parameter substitution then compile ──────────────────────────────────
    {
        auto tree = Parse("ActorValue(Health) < ${Threshold}");
        SubstituteJsonValues(tree, {{"Threshold", "42"}});
        Check(tree["value"] == 42, "substituted threshold is numeric");
        auto condition = Condition::FromJson(tree);
        Check(condition != nullptr, "substituted tree compiles");
    }

    // ── Errors ───────────────────────────────────────────────────────────────
    ExpectError("Foo(1)", "Unknown condition type");
    ExpectError("Time(1, 2, 3)", "at most 2");
    ExpectError("(Combat", "')'");
    ExpectError("Weather(Rainy) > 3", "does not support comparisons");
    ExpectError("Combat &&", "Expected a condition");
    ExpectError("Weather(\"open", "Unterminated string");
    ExpectError("Combat Swimming", "Unexpected input");

    // ── Registry ─────────────────────────────────────────────────────────────
    {
        auto& registry = Registry::Get();
        Check(registry.FindType("logicalcondition") != nullptr, "aliases are case-insensitive");
        TypeInfo dup;
        dup.name = "Time";
        dup.factory = [](const json&) { return std::unique_ptr<Condition>(); };
        Check(!registry.RegisterType(dup), "duplicate type rejected");

        registry.RegisterNamed("TestAlways", std::make_shared<ConstantCondition>(true), "tests");
        Check(registry.FindNamed("TestAlways") != nullptr, "named lookup");
        Check(registry.UnregisterSource("tests") == 1, "unregister by source");
        Check(registry.FindNamed("TestAlways") == nullptr, "named removed");
    }

    // ── Catalog / schema ─────────────────────────────────────────────────────
    {
        auto& registry = Registry::Get();
        const auto catalog = registry.GetCatalog();
        std::unordered_set<std::string> categories;
        for (const auto& c : catalog["categories"]) categories.insert(c["id"].get<std::string>());

        std::size_t visible = 0;
        for (const auto& t : catalog["types"]) {
            const auto name = t["name"].get<std::string>();
            Check(categories.contains(t["category"].get<std::string>()), name + " has a registered category");
            Check(!t["label"].get<std::string>().empty(), name + " has a label");
            if (!t.value("hidden", false)) {
                ++visible;
                Check(!t["description"].get<std::string>().empty(), name + " has a description");
            }
            // Every field key a type declares must survive a factory round trip
            json defaults{{"type", name}};
            for (const auto& f : t["fields"]) {
                if (f.contains("default")) defaults[f["key"].get<std::string>()] = f["default"];
            }
            auto condition = Condition::FromJson(defaults);
            Check(condition != nullptr, name + " builds from its schema defaults");
            if (t.contains("compareField")) {
                bool hasComparison = false;
                for (const auto& f : t["fields"]) hasComparison |= f["type"] == "comparison";
                Check(hasComparison, name + " exposes a comparison field");
            }
        }
        Check(visible >= 50, "at least 50 pickable types (" + std::to_string(visible) + ")");

        const auto schema = registry.GetJsonSchema();
        Check(schema["oneOf"].size() == catalog["types"].size(), "schema has one variant per type");

        // Schema metadata round-trips through the plugin-facing JSON form
        auto field = FieldFromJson(FieldToJson(Field::Enum("hand", "Hand", Field::Options({"Left", "Right"}), "Left")));
        Check(field.type == FieldType::Enum && field.options.size() == 2 && field.defaultValue == "Left",
              "field schema round-trips");
    }


    // Integration availability is distinct from a condition's boolean value.
    {
        auto& availability = Availability::Get();
        auto& registry = Registry::Get();
        int evaluated = 0, notifications = 0;
        TypeBuilder("TestIntegration", "Test integration", "world")
            .RequiresPlugins({"TestDependency.esp"}).Aliases({"TestAlias"})
            .Factory([&](const json& j) {
                return std::make_unique<CallbackCondition>("test", [&](const EvaluationContext&) {
                    ++evaluated; return false;
                }, EventDependency::None, j);
            }).Register(registry);
        std::string error;
        auto condition = Expression::CompileAny("!TestIntegration", &error);
        Check(condition && availability.Check(*condition).state == AvailabilityState::Pending, "plugin snapshot starts pending");
        const auto subscription = availability.Subscribe([&](std::uint64_t revision) {
            ++notifications;
            Check(availability.Revision() >= revision, "availability callback can query without deadlock");
            (void)registry.GetCatalog();
        });
        availability.SetLoadedPlugins({"Skyrim.esm"});
        Check(!availability.Check(*condition).Ready(), "missing required plugin suspends negated expression");
        EvaluationContext context(nullptr);
        Check(!condition->EvaluateAvailable(context) && evaluated == 0, "NOT cannot turn unavailable into true or invoke callback");
        auto disjunction = Expression::CompileAny("true || TestIntegration", &error);
        Check(!disjunction->EvaluateAvailable(context), "unavailable dependency suspends whole OR expression");
        const auto before = notifications;
        availability.SetLoadedPlugins({"skyrim.ESM"});
        Check(notifications == before, "unchanged plugin snapshot does not notify");
        availability.SetLoadedPlugins({"Skyrim.esm", "TESTDEPENDENCY.ESP"});
        Check(availability.Check(*condition).Ready() && condition->EvaluateAvailable(context), "plugin recovery resumes NOT normally");
        Check(availability.SetTypeStatus("testalias", {AvailabilityState::Unavailable, "Integration disabled"}), "status resolves aliases");
        Check(!condition->EvaluateAvailable(context), "runtime integration disable suspends rule");
        Check(availability.Check(*condition).reason == "Integration disabled", "runtime reason retained");
        availability.SetTypeStatus("TestIntegration", {AvailabilityState::Pending, "Waiting for data"});
        Check(availability.Check(*condition).state == AvailabilityState::Pending, "runtime pending stays distinct");
        availability.SetTypeStatus("TestIntegration", {});
        Check(condition->EvaluateAvailable(context), "runtime recovery resumes saved expression");
        auto form = Expression::CompileAny("!Faction(\"0x000002|Absent.esp\")", &error);
        Check(form && !availability.Check(*form).Ready(), "missing form owner suspends generic faction condition");
        auto pluginTest = Expression::CompileAny("IsPluginLoaded(\"Absent.esp\")", &error);
        Check(pluginTest && availability.Check(*pluginTest).Ready(), "plugin-presence check is itself always available");
        registry.RegisterNamed("AvailabilityNamed", std::shared_ptr<const Condition>(Expression::CompileAny("TestIntegration", &error)));
        auto named = Expression::CompileAny("!@AvailabilityNamed", &error);
        availability.SetTypeStatus("TestIntegration", {AvailabilityState::Unavailable, "Missing native API"});
        Check(named && !named->EvaluateAvailable(context), "named condition propagates unavailable dependency");
        registry.RegisterNamed("AvailabilityCycle", std::shared_ptr<const Condition>(Expression::CompileAny("@AvailabilityCycle", &error)));
        auto cycle = Expression::CompileAny("@AvailabilityCycle", &error);
        Check(cycle && !availability.Check(*cycle).Ready(), "named dependency cycles do not recurse forever");
        Check(HasDependency(condition->GetEffectiveEventDependencies(), EventDependency::Availability), "watches listen for availability changes");
        const auto catalog = registry.GetCatalog();
        bool found = false;
        for (const auto& type : catalog["types"]) if (type["name"] == "TestIntegration") {
            found = true;
            Check(type["requiresPlugins"] == json::array({"TestDependency.esp"}) &&
                  type["availability"]["available"] == false, "catalog keeps unavailable type with requirement and reason");
        }
        Check(found, "unavailable custom type stays registered");

        const auto savedJson = condition->ToJsonComplete();
        availability.SetTypeStatus("TestIntegration", {});
        Check(condition->ToJsonComplete() == savedJson, "readiness changes preserve saved condition settings");
        auto future = Expression::CompileAny("!@FutureAvailability", &error);
        Check(future && !availability.Check(*future).Ready(), "missing named preset suspends expression");
        const auto beforeNamed = notifications;
        registry.RegisterNamed("FutureAvailability", std::shared_ptr<const Condition>(Expression::CompileAny("true", &error)));
        Check(notifications > beforeNamed && availability.Check(*future).Ready(), "named preset recovery notifies subscribers");
        registry.UnregisterNamed("FutureAvailability");
        Check(!availability.Check(*future).Ready(), "named preset removal suspends saved expression again");
        registry.RegisterNamed("OpaqueAvailability", std::make_shared<CallbackCondition>("opaque",
            [](const EvaluationContext&) { return false; }, EventDependency::None,
            json{{"type", "Rule"}, {"ruleName", "OpaqueAvailability"}}));
        auto opaque = Expression::CompileAny("!@OpaqueAvailability", &error);
        Check(opaque && availability.Check(*opaque).Ready(), "opaque named callback is not mistaken for a dependency cycle");
        registry.UnregisterNamed("OpaqueAvailability");
        availability.Unsubscribe(subscription);
        const auto afterUnsubscribe = notifications;
        availability.SetTypeStatus("TestIntegration", {});
        Check(notifications == afterUnsubscribe, "unsubscribed listener receives no future updates");
        registry.UnregisterNamed("AvailabilityNamed");
        registry.UnregisterNamed("AvailabilityCycle");
    }

    RunValueTests(&Check);

    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
