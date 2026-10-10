// ExpressionTests.cpp - Out-of-game tests for the expression parser/printer,
// the type registry and parameter substitution. Nothing here touches game state.

#include "ConditionEngine/ConditionEngine.h"
#include "ConditionEngine/Expression.h"

#include <cstdio>

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

int main() {
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

    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
