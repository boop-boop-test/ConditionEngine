#pragma once

// Definition.h - What a dial is: its range, who has it, and the rules that drive it.
//
// Pure data, no game code. Dial files, the C API, Papyrus and the editor page all
// describe dials with the same JSON (docs/format.md):
//
//   "wetness": {
//     "label": "Wetness", "range": [0, 100], "unit": "%", "scope": "actor", "track": "IsPlayer || IsFollower",
//     "rules": [
//       {"while": "@OutsideInRain", "rate": 2},
//       {"when": "Swimming", "set": 100},
//       {"while": "!@OutsideInRain && !Swimming", "approach": 0, "halfLife": 90}
//     ],
//     "conditions": {"Soaked": ">= 75"}
//   }
//
// A rule is one trigger (on / event / when / while / every / key) and one action
// (set / add / multiply / toggle / reset / cycle / rate / approach / send), plus optional
// filters and modifiers (if, for, cooldown, chance, actor, ...).

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ConditionEngine::Dials {

    using ActorKey = std::uint32_t;     // an actor's form ID; 0 = no actor (a global dial)
    constexpr ActorKey kPlayer = 0x14;

    enum class Kind { Number, Toggle, Choice };
    enum class Scope { Global, Actor };
    enum class Clock { Real, Game };     // real seconds while the game runs, or game hours

    enum class TriggerKind { Game, Event, When, While, Every, Key };
    enum class Edge { Rising, Falling, Both };
    // Send: an SKSE mod event (SendModEvent) for other mods' scripts, not a change of the dial
    enum class ActionKind { Set, Add, Multiply, Toggle, Reset, Cycle, Rate, Approach, Send };

    // Mod event names a rule may not send: ConditionEngine's own (Dials_Changed, Dials_Signal) and the
    // names SkyrimNet triggers use inside the engine (SkyrimNet:..., SkyrimNetAction:...)
    bool IsReservedEventName(std::string_view name);

    // Which actor a rule acts on. Auto: the event's own actor (the one hit, the one who
    // cast, the mod event's sender), the player for keys and timers.
    enum class Role { Auto, Other, Player, Crosshair };

    enum class GameEvent {
        Load, Hit, CombatStart, CombatEnd, Death, Kill, Equip, Unequip, SpellCast, MagicEffect, Activate,
        ItemAdded, ItemRemoved, LocationChange, SleepStart, SleepStop, MenuOpen, MenuClose, Animation, SkillUp
    };
    constexpr std::size_t kGameEventCount = static_cast<std::size_t>(GameEvent::SkillUp) + 1;

    std::string_view GameEventName(GameEvent event);
    std::optional<GameEvent> ParseGameEvent(std::string_view name);
    // What the event's actor and its other actor are, and the number it carries (for the editor)
    std::string_view GameEventHelp(GameEvent event);

    std::string_view ActionName(ActionKind kind);
    std::string_view TriggerName(TriggerKind kind);

    // A number or a value expression ("$hunger / 10", "DialsEvent * 2")
    struct Amount {
        std::optional<double> constant;
        std::string expr;

        bool IsSet() const { return constant.has_value() || !expr.empty(); }
        bool IsConstant() const { return constant.has_value(); }
        nlohmann::json ToJson() const;
        static std::optional<Amount> FromJson(const nlohmann::json& j);
    };

    struct Rule {
        std::string label;
        bool enabled = true;

        // ── Trigger ──────────────────────────────────────────────────────────
        TriggerKind trigger = TriggerKind::Event;
        GameEvent game = GameEvent::Load;  // on
        std::string event;                 // event: mod event / signal name
        std::string condition;             // when / while (empty while = always)
        Edge edge = Edge::Rising;          // when
        double every = 0.0;                // every: interval (seconds, or game hours with clock game)
        std::uint32_t key = 0;             // key: DirectInput scan code

        // ── Filters (events) ─────────────────────────────────────────────────
        std::string form;     // the event's form: weapon / spell / item / effect / activated object
        std::string keyword;  // the event's form has this keyword
        std::string text;     // mod event string, menu name, animation event, skill
        std::string other;    // "player" | "!player" (the event's other actor)
        std::string otherIf;  // condition on the event's other actor
        std::string onlyIf;   // "if": condition on the actor the rule acts on

        Role actor = Role::Auto;

        // ── Action ───────────────────────────────────────────────────────────
        ActionKind action = ActionKind::Set;
        Amount value;            // set / add / multiply / rate / approach target / cycle step / send's number
        std::string send;        // send: the mod event's name
        std::string sendText;    // send: its string argument
        double forDuration = 0;  // "for": undo after this long (set, add, toggle, cycle, reset)
        double halfLife = 0;     // approach
        Clock clock = Clock::Real;

        double cooldown = 0;  // per actor, in the rule's clock
        double chance = 1.0;  // 0..1

        // ── SkyrimNet ────────────────────────────────────────────────────────
        // Triggers: "skyrimnet": "dialogue" is event "SkyrimNet:dialogue" (a SkyrimNet event
        // involving the actor); "aiAction": "CalmDown" is event "SkyrimNetAction:CalmDown" (an AI
        // action SkyrimNet offers the NPC, described by `description`, eligible where `if` holds).
        std::string description;
        // When the rule fires, tell SkyrimNet: an event in the actor's memory ("{name} had a manic
        // spell."), or with tellNarration a narrated line the NPC reacts to. {name}, {value}, {label}.
        std::string tell;
        bool tellNarration = false;

        bool IsAiAction() const { return trigger == TriggerKind::Event && event.starts_with("SkyrimNetAction:"); }
        std::string AiActionName() const { return IsAiAction() ? event.substr(16) : std::string(); }

        bool IsContinuous() const { return trigger == TriggerKind::While; }
        nlohmann::json ToJson() const;
    };

    struct DialDef {
        std::string id;  // also its ConditionEngine name: $id
        std::string label;
        std::string description;
        std::string category = "dials";
        std::string icon;
        std::string unit;
        std::string source;  // the file, mod or "User" that defined it
        bool enabled = true;

        Kind kind = Kind::Number;
        Scope scope = Scope::Global;
        double min = 0.0;
        double max = 100.0;
        double defaultValue = 0.0;
        bool integer = false;
        double precision = 0.0;            // smallest change worth publishing (0: range / 200; integer dials: 1)
        std::map<int, std::string> options;  // choice dials: value -> label

        std::string track;  // actor dials: which actors have it (a condition); empty = the player only
        bool persist = true;
        bool oar = false;  // Open Animation Replacer's ConditionEngineDial condition may read it (docs/oar-conditions.md)
        std::vector<double> thresholds;  // a continuous change across one sends Dials_Changed

        std::vector<std::string> provides;  // feeds these combined ConditionEngine values ($hunger ...)
        int priority = 50;

        // Named conditions (@Name): expression text, or shorthand: ">= 50" (compares $id),
        // an option label of a choice dial ("Formal" = $id == 2)
        std::vector<std::pair<std::string, std::string>> conditions;
        // SkyrimNet world knowledge per condition: text NPCs know while the condition is true for them
        // ("conditions": {"Manic": {"expr": "== 1", "knowledge": "You are in a manic phase..."}})
        std::map<std::string, std::string> knowledge;

        std::vector<Rule> rules;

        double Clamp(double v) const;
        double Exposed(double raw) const;   // rounded for integer dials
        double Precision() const;
        std::string OptionLabel(double value) const;  // "" when none
        // The expression a condition entry stands for
        std::string ConditionExpression(const std::string& text) const;
        bool TracksPlayerOnly() const { return scope == Scope::Actor && track.empty(); }

        nlohmann::json ToJson() const;
    };

    struct ParseResult {
        std::optional<DialDef> def;
        std::vector<std::string> errors;    // the dial is refused
        std::vector<std::string> warnings;  // unknown keys and the like: accepted
    };

    bool IsValidId(std::string_view id);
    // Parses one dial. `source` names its origin in messages and on the dial.
    ParseResult ParseDial(std::string_view id, const nlohmann::json& j, std::string_view source);
    // "F5", "K", "Num1", "0x3F", "63" -> scan code (0 = unknown)
    std::uint32_t ParseKey(const nlohmann::json& j);
    std::string KeyName(std::uint32_t scanCode);

    // Lower-case copy, for case-insensitive names
    std::string Lower(std::string_view s);
    bool IEquals(std::string_view a, std::string_view b);

} // namespace ConditionEngine::Dials
