#pragma once

// People.h - Important people: named groups of NPCs most players care about.
//
// One question for every mod: "is this actor somebody who matters?" Jarls and their courts,
// leaders, guild members, the main quest's people, your followers, spouse and household, and
// anyone you or a mod adds. A group lists members (NPCs, placed actors, factions, keywords)
// and can also count every current follower or every essential actor.
//
// Groups come from the "people" section of presets (merged: a later file adds members to a
// group an earlier one made), from mods at run time (C API, Papyrus) and from the player's own
// list (User/People.json, written by the console and Papyrus additions):
//
//   "people": {
//     "court":   { "label": "Jarls & their courts", "order": 10,
//                  "members": ["0x50920|Skyrim.esm", "0x50922|Skyrim.esm"] },
//     "mymod":   { "label": "My Mod's cast", "members": ["0x801|MyMod.esp"] },
//     "followers": { "members": ["0x5C84E|Skyrim.esm"], "teammates": true }
//   }
//
// Conditions read it as ImportantPerson() (any group) or ImportantPerson(court, followers).
// The player is never an important person unless the condition asks (includePlayer=true).

#include "ConditionEngine/Common.h"

#include <filesystem>

namespace ConditionEngine {
    class Registry;
}

namespace ConditionEngine::People {

    struct Group {
        std::string id;           // lowercase, stable ("court")
        std::string label;
        std::string description;
        std::string icon;
        int order = 100;
        std::vector<std::string> members;  // form strings or editor IDs: NPC, Actor, Faction, Keyword
        bool teammates = false;            // every current follower (player teammate)
        bool essential = false;            // every essential actor (quest-protected)
        std::vector<std::string> sources;  // who defined or added to it

        // {"id", "label", "description", "icon", "order", "members", "teammates", "essential", "sources"}
        nlohmann::json ToJson() const;
    };

    // The group the player's own additions go to by default
    inline constexpr const char* kUserGroup = "custom";
    inline constexpr const char* kUserSource = "User";

    // Merges a preset's "people" section. Unknown groups are created, known ones gain members
    // (and a label / description / icon when they had none). Returns the number of members added.
    std::size_t AddFromJson(const nlohmann::json& section, const std::string& source,
                            std::vector<std::string>* warnings = nullptr);

    // Adds one member (form string, "0x14" / editor ID) to a group, creating it when new.
    // False when the text is empty or the member is already in the group.
    bool AddMember(const std::string& group, const std::string& member, const std::string& source);
    bool RemoveMember(const std::string& group, const std::string& member);

    // A form as a member string ("0x1A6B|Skyrim.esm"). Actors are stored as their base NPC so the
    // entry survives respawns; factions and keywords as themselves. Empty for forms created in game.
    std::string MemberString(RE::TESForm* form);

    std::vector<Group> ListGroups();  // sorted by order, then label
    std::optional<Group> FindGroup(const std::string& id);

    // The first group (in display order) the actor belongs to, limited to `groups` (ids or labels,
    // any case) when not empty.
    // Empty when none. Never the player unless includePlayer.
    std::string MatchGroup(RE::Actor* actor, const std::vector<std::string>& groups = {}, bool includePlayer = false);
    // Every group the actor belongs to, in display order
    std::vector<std::string> GroupsOf(RE::Actor* actor, bool includePlayer = false);

    // Forgets every group (tests, and before presets are read again)
    void Clear();

    // ── The player's own list ────────────────────────────────────────────────
    // Data/SKSE/Plugins/ConditionEngine/User/People.json: {"people": {...}} in the preset format.
    std::filesystem::path UserFile();
    std::size_t LoadUserFile();
    // Adds / removes a member of the player's list and rewrites the file. False when nothing changed.
    bool AddUserMember(const std::string& group, const std::string& member, std::string* error = nullptr);
    bool RemoveUserMember(const std::string& group, const std::string& member, std::string* error = nullptr);

    // The ImportantPerson condition type and the "people" category
    void RegisterTypes(Registry& registry);

} // namespace ConditionEngine::People
