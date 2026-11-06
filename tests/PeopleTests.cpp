// PeopleTests.cpp - Out-of-game tests for important people (docs/people.md): the built-in groups,
// merging preset sections, members added and removed, the ImportantPerson type in expressions and
// in the catalog. Matching actors needs the game and is not tested here.

#include "ConditionEngine/ConditionEngine.h"
#include "ConditionEngine/Expression.h"
#include "ConditionEngine/People.h"
#include "ConditionEngine/Presets.h"
#include "ConditionEngine/Registry.h"

#include <algorithm>

namespace {
    using json = nlohmann::json;
    using namespace ConditionEngine;

    using CheckFn = void (*)(bool ok, const std::string& what);
    CheckFn Check = nullptr;

    bool HasMember(const People::Group& g, const std::string& member) {
        return std::find(g.members.begin(), g.members.end(), member) != g.members.end();
    }

    std::string RoundTrip(const std::string& text) {
        std::string error;
        auto condition = Expression::CompileAny(text, &error);
        return condition ? Expression::ToExpression(*condition) : "error: " + error;
    }
}

void RunPeopleTests(CheckFn check) {
    Check = check;

    // ── The built-in groups (the value tests loaded the built-in presets) ───────────
    const auto groups = People::ListGroups();
    std::vector<std::string> ids;
    for (const auto& g : groups) ids.push_back(g.id);
    const std::vector<std::string> expected = {"court",  "leaders",   "guilds",    "quest", "followers",
                                               "spouse", "household", "essential", "custom"};
    Check(ids == expected, "built-in people groups, in display order");
    {
        const auto court = People::FindGroup("court");
        Check(court && court->label == "Jarls & their courts" && HasMember(*court, "0x50920|Skyrim.esm"),
              "court: label and JobJarlFaction");
        const auto followers = People::FindGroup("Followers");
        Check(followers && followers->teammates && HasMember(*followers, "0x5C84E|Skyrim.esm"),
              "followers: every teammate plus CurrentFollowerFaction (ids ignore case)");
        const auto essential = People::FindGroup("essential");
        Check(essential && essential->essential && essential->members.empty(), "essential: by flag, no members");
        const auto spouse = People::FindGroup("spouse");
        Check(spouse && HasMember(*spouse, "0xC6472|Skyrim.esm"), "spouse: PlayerMarriedFaction");
    }

    // ── Presets merge ───────────────────────────────────────────────────────────────
    {
        const json preset = json::parse(R"({
            "people": {
                "court": { "label": "Not the label", "order": 999, "members": ["0x801|MyCourt.esp", "0x50920|Skyrim.esm"] },
                "My Mod's Cast": { "label": "My cast", "order": 35, "members": ["0x802|MyMod.esp"], "factions": "0x803|MyMod.esp" },
                "bare": ["0x804|MyMod.esp"]
            }
        })");
        const auto added = LoadPresetJson(preset, "PeopleTest.json");
        Check(added == 4, "a people section counts the members it adds (a duplicate is not added)");
        const auto court = People::FindGroup("court");
        Check(court && court->label == "Jarls & their courts" && court->order == 10 &&
                  HasMember(*court, "0x801|MyCourt.esp") &&
                  std::count(court->members.begin(), court->members.end(), "0x50920|Skyrim.esm") == 1,
              "a later preset adds members to a group but keeps its label and order");
        Check(court && std::find(court->sources.begin(), court->sources.end(), "PeopleTest.json") != court->sources.end(),
              "a group lists every source that added to it");
        const auto cast = People::FindGroup("my_mod_s_cast");
        Check(cast && cast->label == "My cast" && cast->order == 35 && HasMember(*cast, "0x802|MyMod.esp") &&
                  HasMember(*cast, "0x803|MyMod.esp"),
              "a new group: id from the name, members from members and factions");
        const auto bare = People::FindGroup("bare");
        Check(bare && bare->label == "Bare" && HasMember(*bare, "0x804|MyMod.esp"), "a bare list is a group's members");
        const auto list = People::ListGroups();
        const auto at = [&](const char* id) {
            return std::find_if(list.begin(), list.end(), [&](const People::Group& g) { return g.id == id; }) - list.begin();
        };
        Check(at("guilds") < at("my_mod_s_cast") && at("my_mod_s_cast") < at("quest"), "groups sort by order");
    }

    // ── Members at run time ─────────────────────────────────────────────────────────
    Check(People::AddMember("custom", "0x900|Test.esp", "test"), "add a member");
    Check(!People::AddMember("CUSTOM", "0x900|test.esp", "test"), "the same member again (any case) is not added");
    Check(People::AddMember("brand new", "0x901|Test.esp", "test") && People::FindGroup("brand_new"),
          "adding to an unknown group makes it");
    Check(People::RemoveMember("custom", "0x900|Test.esp"), "remove a member");
    Check(!People::RemoveMember("custom", "0x900|Test.esp"), "removing it again changes nothing");
    Check(!People::AddMember("custom", "  ", "test"), "an empty member is refused");

    // ── The condition type ──────────────────────────────────────────────────────────
    Check(RoundTrip("ImportantPerson()") == "ImportantPerson", "ImportantPerson() = any group");
    Check(RoundTrip("ImportantPerson([court, spouse])") == "ImportantPerson([court, spouse])", "a group list");
    Check(RoundTrip("IsImportant(court)") == "ImportantPerson([court])", "the IsImportant alias, one group");
    Check(RoundTrip("ImportantPerson(\"Court, Spouse\")") == "ImportantPerson([court, spouse])",
          "groups as comma-separated text, any case");
    Check(RoundTrip("ImportantPerson(includePlayer=true)") == "ImportantPerson(includePlayer=true)", "includePlayer");
    {
        const auto text = RoundTrip("!ImportantPerson() && Combat");
        Check(text == "!ImportantPerson && Combat(true)", "inside an expression: " + text);
    }
    {
        std::string error;
        auto unknown = Expression::CompileAny("ImportantPerson([nobody])", &error);
        const auto problems = unknown ? unknown->Validate() : std::vector<std::string>{};
        Check(problems.size() == 1 && problems[0].find("nobody") != std::string::npos, "an unknown group is reported");
        auto known = Expression::CompileAny("ImportantPerson([court, followers])", &error);
        Check(known && known->Validate().empty(), "known groups validate");
    }

    // ── The catalog ─────────────────────────────────────────────────────────────────
    {
        const auto catalog = Registry::Get().GetCatalog();
        const auto& types = catalog["types"];
        const auto type = std::find_if(types.begin(), types.end(),
                                       [](const json& t) { return t.value("name", "") == "ImportantPerson"; });
        Check(type != types.end() && (*type)["category"] == "people", "ImportantPerson is in the people category");
        bool court = false, cast = false;
        if (type != types.end()) {
            for (const auto& field : (*type)["fields"]) {
                if (field.value("key", "") != "groups") continue;
                for (const auto& o : field["options"]) {
                    court = court || (o["value"] == "court" && o["label"] == "Jarls & their courts");
                    cast = cast || o["value"] == "my_mod_s_cast";
                }
            }
        }
        Check(court && cast, "the groups field offers every group, those presets added too");
        const auto& categories = catalog["categories"];
        Check(std::any_of(categories.begin(), categories.end(),
                          [](const json& c) { return c.value("id", "") == "people" && c.value("label", "") == "Important People"; }),
              "the Important People category");
    }
}
