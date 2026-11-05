#include "Definition.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <set>

namespace ConditionEngine::Dials {

    namespace {
        struct EventInfo {
            GameEvent event;
            std::string_view name;
            std::string_view help;
        };

        constexpr std::array<EventInfo, kGameEventCount> kEvents = {{
            {GameEvent::Load, "Load", "a save loaded or a new game started (the player)"},
            {GameEvent::Hit, "Hit", "an actor was hit: the one hit; other = the attacker; form = weapon or spell"},
            {GameEvent::CombatStart, "CombatStart", "an actor entered combat; other = its target"},
            {GameEvent::CombatEnd, "CombatEnd", "an actor left combat"},
            {GameEvent::Death, "Death", "an actor died; other = the killer"},
            {GameEvent::Kill, "Kill", "an actor killed someone: the killer; other = the one killed"},
            {GameEvent::Equip, "Equip", "an actor equipped an item; form = the item"},
            {GameEvent::Unequip, "Unequip", "an actor unequipped an item; form = the item"},
            {GameEvent::SpellCast, "SpellCast", "an actor cast a spell (or used a power, shout or potion); form = the spell"},
            {GameEvent::MagicEffect, "MagicEffect", "a magic effect landed on an actor; other = the caster; form = the effect"},
            {GameEvent::Activate, "Activate", "an actor activated something; form = the activated object's base"},
            {GameEvent::ItemAdded, "ItemAdded", "an actor got items; form = the item; number = the count; other = the giver"},
            {GameEvent::ItemRemoved, "ItemRemoved", "an actor lost items; form = the item; number = the count; other = the receiver"},
            {GameEvent::LocationChange, "LocationChange", "an actor moved to another location; form = the new location"},
            {GameEvent::SleepStart, "SleepStart", "the player went to sleep"},
            {GameEvent::SleepStop, "SleepStop", "the player woke up; number = hours slept"},
            {GameEvent::MenuOpen, "MenuOpen", "a menu opened (the player); text = the menu (InventoryMenu, Dialogue Menu, ...)"},
            {GameEvent::MenuClose, "MenuClose", "a menu closed (the player); text = the menu"},
            {GameEvent::Animation, "Animation", "an animation event on the player; text = its tag (JumpUp, weaponSwing, ...)"},
            {GameEvent::SkillUp, "SkillUp", "a player skill went up; text = the skill (OneHanded, Destruction, ...)"},
        }};

        constexpr std::array<std::string_view, 8> kActionNames = {"set", "add", "multiply", "toggle",
                                                                  "reset", "cycle", "rate", "approach"};
        constexpr std::array<std::string_view, 6> kTriggerNames = {"on", "event", "when", "while", "every", "key"};

        struct KeyEntry {
            std::string_view name;
            std::uint32_t code;
        };
        constexpr KeyEntry kKeys[] = {
            {"Esc", 0x01}, {"1", 0x02}, {"2", 0x03}, {"3", 0x04}, {"4", 0x05}, {"5", 0x06}, {"6", 0x07}, {"7", 0x08},
            {"8", 0x09}, {"9", 0x0A}, {"0", 0x0B}, {"Minus", 0x0C}, {"Equals", 0x0D}, {"Backspace", 0x0E},
            {"Tab", 0x0F}, {"Q", 0x10}, {"W", 0x11}, {"E", 0x12}, {"R", 0x13}, {"T", 0x14}, {"Y", 0x15},
            {"U", 0x16}, {"I", 0x17}, {"O", 0x18}, {"P", 0x19}, {"LeftBracket", 0x1A}, {"RightBracket", 0x1B},
            {"Enter", 0x1C}, {"LeftCtrl", 0x1D}, {"A", 0x1E}, {"S", 0x1F}, {"D", 0x20}, {"F", 0x21}, {"G", 0x22},
            {"H", 0x23}, {"J", 0x24}, {"K", 0x25}, {"L", 0x26}, {"Semicolon", 0x27}, {"Apostrophe", 0x28},
            {"Tilde", 0x29}, {"LeftShift", 0x2A}, {"Backslash", 0x2B}, {"Z", 0x2C}, {"X", 0x2D}, {"C", 0x2E},
            {"V", 0x2F}, {"B", 0x30}, {"N", 0x31}, {"M", 0x32}, {"Comma", 0x33}, {"Period", 0x34}, {"Slash", 0x35},
            {"RightShift", 0x36}, {"NumMultiply", 0x37}, {"LeftAlt", 0x38}, {"Space", 0x39}, {"CapsLock", 0x3A},
            {"F1", 0x3B}, {"F2", 0x3C}, {"F3", 0x3D}, {"F4", 0x3E}, {"F5", 0x3F}, {"F6", 0x40}, {"F7", 0x41},
            {"F8", 0x42}, {"F9", 0x43}, {"F10", 0x44}, {"NumLock", 0x45}, {"ScrollLock", 0x46}, {"Num7", 0x47},
            {"Num8", 0x48}, {"Num9", 0x49}, {"NumMinus", 0x4A}, {"Num4", 0x4B}, {"Num5", 0x4C}, {"Num6", 0x4D},
            {"NumPlus", 0x4E}, {"Num1", 0x4F}, {"Num2", 0x50}, {"Num3", 0x51}, {"Num0", 0x52}, {"NumPeriod", 0x53},
            {"F11", 0x57}, {"F12", 0x58}, {"NumEnter", 0x9C}, {"RightCtrl", 0x9D}, {"NumDivide", 0xB5},
            {"RightAlt", 0xB8}, {"Home", 0xC7}, {"Up", 0xC8}, {"PageUp", 0xC9}, {"Left", 0xCB}, {"Right", 0xCD},
            {"End", 0xCF}, {"Down", 0xD0}, {"PageDown", 0xD1}, {"Insert", 0xD2}, {"Delete", 0xD3},
        };

        std::string Trim(std::string_view s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
            return std::string(s);
        }

        std::optional<double> ParseNumber(std::string_view s) {
            const auto t = Trim(s);
            if (t.empty()) return std::nullopt;
            double v = 0;
            const char* begin = t.data();
            if (*begin == '+') ++begin;
            auto [end, ec] = std::from_chars(begin, t.data() + t.size(), v);
            if (ec != std::errc{} || end != t.data() + t.size()) return std::nullopt;
            return v;
        }

        // Reads a string field; records an error for any other type
        bool ReadString(const nlohmann::json& j, std::string_view key, std::string& out, std::vector<std::string>& errors,
                        const std::string& where) {
            auto it = j.find(key);
            if (it == j.end() || it->is_null()) return false;
            if (!it->is_string()) {
                errors.push_back(where + ": \"" + std::string(key) + "\" must be text");
                return false;
            }
            out = Trim(it->get<std::string>());
            return true;
        }

        bool ReadNumber(const nlohmann::json& j, std::string_view key, double& out, std::vector<std::string>& errors,
                        const std::string& where) {
            auto it = j.find(key);
            if (it == j.end() || it->is_null()) return false;
            if (it->is_number()) {
                out = it->get<double>();
                return true;
            }
            if (it->is_string()) {
                if (auto v = ParseNumber(it->get<std::string>())) {
                    out = *v;
                    return true;
                }
            }
            errors.push_back(where + ": \"" + std::string(key) + "\" must be a number");
            return false;
        }

        bool ReadBool(const nlohmann::json& j, std::string_view key, bool& out, std::vector<std::string>& errors,
                      const std::string& where) {
            auto it = j.find(key);
            if (it == j.end() || it->is_null()) return false;
            if (!it->is_boolean()) {
                errors.push_back(where + ": \"" + std::string(key) + "\" must be true or false");
                return false;
            }
            out = it->get<bool>();
            return true;
        }

        std::string RuleWhere(const DialDef& def, std::size_t index, const nlohmann::json& j) {
            std::string where = "dial '" + def.id + "' rule " + std::to_string(index + 1);
            if (auto it = j.find("label"); it != j.end() && it->is_string() && !it->get<std::string>().empty()) {
                where += " (" + it->get<std::string>() + ")";
            }
            return where;
        }

        const std::set<std::string, std::less<>> kRuleKeys = {
            "label", "enabled", "on", "event", "when", "while", "every", "key", "edge", "form", "keyword", "text",
            "menu", "anim", "skill", "other", "otherIf", "if", "actor", "set", "add", "multiply", "toggle", "reset",
            "cycle", "rate", "approach", "for", "halfLife", "clock", "cooldown", "chance", "skyrimnet", "aiAction",
            "description", "tell", "tellAs"};

        const std::set<std::string, std::less<>> kDialKeys = {
            "label", "description", "category", "icon", "unit", "enabled", "kind", "range", "min", "max", "default",
            "integer", "precision", "options", "scope", "track", "actors", "persist", "thresholds", "provides",
            "priority", "conditions", "rules", "source"};

        std::optional<Rule> ParseRule(const DialDef& def, std::size_t index, const nlohmann::json& j,
                                      std::vector<std::string>& errors, std::vector<std::string>& warnings) {
            const auto where = RuleWhere(def, index, j);
            if (!j.is_object()) {
                errors.push_back(where + ": a rule must be an object");
                return std::nullopt;
            }
            const auto before = errors.size();
            Rule r;
            ReadString(j, "label", r.label, errors, where);
            ReadBool(j, "enabled", r.enabled, errors, where);

            // ── Trigger: exactly one ─────────────────────────────────────────
            int triggers = 0;
            if (auto it = j.find("on"); it != j.end()) {
                ++triggers;
                r.trigger = TriggerKind::Game;
                const auto name = it->is_string() ? it->get<std::string>() : std::string();
                if (auto e = ParseGameEvent(name)) {
                    r.game = *e;
                } else {
                    std::string all;
                    for (const auto& info : kEvents) all += (all.empty() ? "" : ", ") + std::string(info.name);
                    errors.push_back(where + ": unknown game event '" + name + "' (" + all + ")");
                }
            }
            if (j.contains("event")) {
                ++triggers;
                r.trigger = TriggerKind::Event;
                if (!ReadString(j, "event", r.event, errors, where) || r.event.empty()) {
                    errors.push_back(where + ": \"event\" needs the mod event's name");
                }
            }
            if (j.contains("skyrimnet")) {
                ++triggers;
                r.trigger = TriggerKind::Event;
                std::string type;
                if (!ReadString(j, "skyrimnet", type, errors, where) || type.empty()) {
                    errors.push_back(where + ": \"skyrimnet\" needs an event type (dialogue, combat, hit, ...)");
                }
                r.event = "SkyrimNet:" + type;
            }
            if (j.contains("aiAction")) {
                ++triggers;
                r.trigger = TriggerKind::Event;
                std::string name;
                if (!ReadString(j, "aiAction", name, errors, where) || !IsValidId(name)) {
                    errors.push_back(where + ": \"aiAction\" needs a name (letters, digits and _)");
                }
                r.event = "SkyrimNetAction:" + name;
            }
            if (j.contains("when")) {
                ++triggers;
                r.trigger = TriggerKind::When;
                if (!ReadString(j, "when", r.condition, errors, where) || r.condition.empty()) {
                    errors.push_back(where + ": \"when\" needs a condition");
                }
            }
            if (auto it = j.find("while"); it != j.end()) {
                ++triggers;
                r.trigger = TriggerKind::While;
                if (it->is_boolean()) {
                    if (!it->get<bool>()) errors.push_back(where + ": \"while\": false never runs");
                } else {
                    ReadString(j, "while", r.condition, errors, where);
                }
                if (r.condition == "true") r.condition.clear();
            }
            if (j.contains("every")) {
                ++triggers;
                r.trigger = TriggerKind::Every;
                if (ReadNumber(j, "every", r.every, errors, where) && r.every <= 0) {
                    errors.push_back(where + ": \"every\" must be more than 0");
                }
            }
            if (auto it = j.find("key"); it != j.end()) {
                ++triggers;
                r.trigger = TriggerKind::Key;
                r.key = ParseKey(*it);
                if (r.key == 0) errors.push_back(where + ": unknown key " + it->dump());
            }
            if (triggers == 0) {
                errors.push_back(where + ": no trigger (on, event, when, while, every, key, skyrimnet or aiAction)");
            }
            if (triggers > 1) errors.push_back(where + ": more than one trigger");

            // ── Action: exactly one ──────────────────────────────────────────
            int actions = 0;
            auto amount = [&](std::string_view key, ActionKind kind, bool required) {
                auto it = j.find(key);
                if (it == j.end()) return;
                ++actions;
                r.action = kind;
                if (it->is_boolean() && !required) return;
                if (auto a = Amount::FromJson(*it)) {
                    r.value = *a;
                } else {
                    errors.push_back(where + ": \"" + std::string(key) + "\" must be a number or a value expression");
                }
            };
            amount("set", ActionKind::Set, true);
            amount("add", ActionKind::Add, true);
            amount("multiply", ActionKind::Multiply, true);
            amount("rate", ActionKind::Rate, true);
            amount("approach", ActionKind::Approach, true);
            amount("cycle", ActionKind::Cycle, false);
            if (j.contains("toggle")) {
                ++actions;
                r.action = ActionKind::Toggle;
            }
            if (j.contains("reset")) {
                ++actions;
                r.action = ActionKind::Reset;
            }
            if (r.action == ActionKind::Cycle && !r.value.IsSet()) r.value.constant = 1.0;
            if (actions == 0) {
                errors.push_back(where + ": no action (set, add, multiply, toggle, reset, cycle, rate or approach)");
            }
            if (actions > 1) errors.push_back(where + ": more than one action");

            // ── Modifiers ────────────────────────────────────────────────────
            if (auto it = j.find("edge"); it != j.end()) {
                const auto e = it->is_string() ? Lower(it->get<std::string>()) : std::string();
                if (e == "rising" || e == "true") r.edge = Edge::Rising;
                else if (e == "falling" || e == "false") r.edge = Edge::Falling;
                else if (e == "both" || e == "change") r.edge = Edge::Both;
                else errors.push_back(where + ": \"edge\" must be rising, falling or both");
                if (r.trigger != TriggerKind::When) warnings.push_back(where + ": \"edge\" only applies to \"when\"");
            }
            ReadString(j, "form", r.form, errors, where);
            ReadString(j, "keyword", r.keyword, errors, where);
            for (auto key : {"text", "menu", "anim", "skill"}) ReadString(j, key, r.text, errors, where);
            ReadString(j, "other", r.other, errors, where);
            if (!r.other.empty() && !IEquals(r.other, "player") && !IEquals(r.other, "!player") &&
                !IEquals(r.other, "any")) {
                errors.push_back(where + ": \"other\" must be player, !player or any (use otherIf for more)");
            }
            if (IEquals(r.other, "any")) r.other.clear();
            ReadString(j, "otherIf", r.otherIf, errors, where);
            ReadString(j, "if", r.onlyIf, errors, where);
            ReadString(j, "description", r.description, errors, where);
            ReadString(j, "tell", r.tell, errors, where);
            if (auto it = j.find("tellAs"); it != j.end()) {
                const auto t = it->is_string() ? Lower(it->get<std::string>()) : std::string();
                if (t == "narration") r.tellNarration = true;
                else if (t != "memory" && t != "event") errors.push_back(where + ": \"tellAs\" must be memory or narration");
            }
            if (r.IsContinuous() && !r.tell.empty()) {
                errors.push_back(where + ": \"tell\" does not work with \"while\" (it would tell every tick)");
            }
            if (r.IsAiAction() && r.description.empty()) {
                errors.push_back(where + ": an aiAction needs a \"description\" (what the AI reads to decide)");
            }
            const bool eventFilters = !r.form.empty() || !r.keyword.empty() || !r.text.empty() || !r.other.empty() ||
                                      !r.otherIf.empty();
            if (eventFilters && r.trigger != TriggerKind::Game && r.trigger != TriggerKind::Event) {
                warnings.push_back(where + ": form / keyword / text / other filters only apply to \"on\" and \"event\"");
            }

            if (auto it = j.find("actor"); it != j.end()) {
                const auto a = it->is_string() ? Lower(it->get<std::string>()) : std::string();
                if (a == "auto" || a == "self" || a.empty()) r.actor = Role::Auto;
                else if (a == "other") r.actor = Role::Other;
                else if (a == "player") r.actor = Role::Player;
                else if (a == "crosshair") r.actor = Role::Crosshair;
                else errors.push_back(where + ": \"actor\" must be auto, other, player or crosshair");
            }

            if (ReadNumber(j, "for", r.forDuration, errors, where)) {
                if (r.forDuration <= 0) errors.push_back(where + ": \"for\" must be more than 0");
                if (r.IsContinuous()) errors.push_back(where + ": \"for\" does not work with \"while\" (it ends on its own)");
                if (r.action == ActionKind::Multiply) errors.push_back(where + ": \"for\" does not work with multiply");
            }
            if (ReadNumber(j, "halfLife", r.halfLife, errors, where) && r.halfLife <= 0) {
                errors.push_back(where + ": \"halfLife\" must be more than 0");
            }
            if (r.action == ActionKind::Approach && r.halfLife <= 0 && !j.contains("halfLife")) {
                errors.push_back(where + ": approach needs \"halfLife\" (how long to get halfway)");
            }
            if (auto it = j.find("clock"); it != j.end()) {
                const auto c = it->is_string() ? Lower(it->get<std::string>()) : std::string();
                if (c == "real" || c == "seconds") r.clock = Clock::Real;
                else if (c == "game" || c == "gamehours" || c == "hours") r.clock = Clock::Game;
                else errors.push_back(where + ": \"clock\" must be real (seconds) or game (game hours)");
            }
            if (ReadNumber(j, "cooldown", r.cooldown, errors, where) && r.cooldown < 0) {
                errors.push_back(where + ": \"cooldown\" cannot be negative");
            }
            if (ReadNumber(j, "chance", r.chance, errors, where) && (r.chance < 0 || r.chance > 1)) {
                errors.push_back(where + ": \"chance\" is 0 to 1 (0.25 = one time in four)");
            }

            // ── Trigger and action fit together ──────────────────────────────
            const bool continuous = r.action == ActionKind::Rate || r.action == ActionKind::Approach;
            if (r.IsContinuous() && actions == 1 && !continuous && r.action != ActionKind::Set) {
                errors.push_back(where + ": \"while\" works with rate, approach or set (held while true)");
            }
            if (!r.IsContinuous() && continuous && triggers == 1) {
                errors.push_back(where + ": " + std::string(ActionName(r.action)) +
                                 " runs over time, so it needs \"while\" (\"while\": true for always)");
            }

            for (const auto& [key, _] : j.items()) {
                if (!kRuleKeys.contains(key) && !key.starts_with('_')) warnings.push_back(where + ": unknown key '" + key + "'");
            }
            if (errors.size() != before) return std::nullopt;
            return r;
        }
    }

    // ── Names ────────────────────────────────────────────────────────────────

    std::string Lower(std::string_view s) {
        std::string out(s);
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    bool IEquals(std::string_view a, std::string_view b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
               });
    }

    std::string_view GameEventName(GameEvent event) { return kEvents[static_cast<std::size_t>(event)].name; }
    std::string_view GameEventHelp(GameEvent event) { return kEvents[static_cast<std::size_t>(event)].help; }

    std::optional<GameEvent> ParseGameEvent(std::string_view name) {
        for (const auto& info : kEvents) {
            if (IEquals(info.name, name)) return info.event;
        }
        return std::nullopt;
    }

    std::string_view ActionName(ActionKind kind) { return kActionNames[static_cast<std::size_t>(kind)]; }
    std::string_view TriggerName(TriggerKind kind) { return kTriggerNames[static_cast<std::size_t>(kind)]; }

    bool IsValidId(std::string_view id) {
        if (id.empty() || id.size() > 64) return false;
        if (std::isdigit(static_cast<unsigned char>(id.front()))) return false;
        return std::all_of(id.begin(), id.end(),
                           [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
    }

    std::uint32_t ParseKey(const nlohmann::json& j) {
        if (j.is_number_unsigned() || j.is_number_integer()) {
            const auto v = j.get<long long>();
            return v > 0 && v < 0x100 ? static_cast<std::uint32_t>(v) : 0;
        }
        if (!j.is_string()) return 0;
        const auto s = Trim(j.get<std::string>());
        for (const auto& k : kKeys) {
            if (IEquals(k.name, s)) return k.code;
        }
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            unsigned v = 0;
            auto [end, ec] = std::from_chars(s.data() + 2, s.data() + s.size(), v, 16);
            if (ec == std::errc{} && end == s.data() + s.size() && v > 0 && v < 0x100) return v;
        }
        return 0;
    }

    std::string KeyName(std::uint32_t scanCode) {
        for (const auto& k : kKeys) {
            if (k.code == scanCode) return std::string(k.name);
        }
        char buf[8];
        std::snprintf(buf, sizeof(buf), "0x%02X", scanCode);
        return buf;
    }

    // ── Amount ───────────────────────────────────────────────────────────────

    nlohmann::json Amount::ToJson() const {
        if (constant) return *constant;
        return expr;
    }

    std::optional<Amount> Amount::FromJson(const nlohmann::json& j) {
        Amount a;
        if (j.is_number()) {
            a.constant = j.get<double>();
            return a;
        }
        if (j.is_boolean()) {
            a.constant = j.get<bool>() ? 1.0 : 0.0;
            return a;
        }
        if (!j.is_string()) return std::nullopt;
        const auto s = Trim(j.get<std::string>());
        if (s.empty()) return std::nullopt;
        if (auto v = ParseNumber(s)) {
            a.constant = *v;
        } else {
            a.expr = s;
        }
        return a;
    }

    // ── Rule ─────────────────────────────────────────────────────────────────

    nlohmann::json Rule::ToJson() const {
        nlohmann::json j = nlohmann::json::object();
        if (!label.empty()) j["label"] = label;
        switch (trigger) {
            case TriggerKind::Game: j["on"] = GameEventName(game); break;
            case TriggerKind::Event:
                if (event.starts_with("SkyrimNetAction:")) j["aiAction"] = event.substr(16);
                else if (event.starts_with("SkyrimNet:")) j["skyrimnet"] = event.substr(10);
                else j["event"] = event;
                break;
            case TriggerKind::When: j["when"] = condition; break;
            case TriggerKind::While:
                if (condition.empty()) j["while"] = true;
                else j["while"] = condition;
                break;
            case TriggerKind::Every: j["every"] = every; break;
            case TriggerKind::Key: j["key"] = KeyName(key); break;
        }
        if (trigger == TriggerKind::When && edge != Edge::Rising) j["edge"] = edge == Edge::Falling ? "falling" : "both";
        switch (action) {
            case ActionKind::Toggle: j["toggle"] = true; break;
            case ActionKind::Reset: j["reset"] = true; break;
            default: j[std::string(ActionName(action))] = value.ToJson(); break;
        }
        if (!form.empty()) j["form"] = form;
        if (!keyword.empty()) j["keyword"] = keyword;
        if (!text.empty()) j["text"] = text;
        if (!other.empty()) j["other"] = other;
        if (!otherIf.empty()) j["otherIf"] = otherIf;
        if (!onlyIf.empty()) j["if"] = onlyIf;
        if (!description.empty()) j["description"] = description;
        if (!tell.empty()) j["tell"] = tell;
        if (tellNarration) j["tellAs"] = "narration";
        switch (actor) {
            case Role::Other: j["actor"] = "other"; break;
            case Role::Player: j["actor"] = "player"; break;
            case Role::Crosshair: j["actor"] = "crosshair"; break;
            default: break;
        }
        if (forDuration > 0) j["for"] = forDuration;
        if (halfLife > 0) j["halfLife"] = halfLife;
        if (clock == Clock::Game) j["clock"] = "game";
        if (cooldown > 0) j["cooldown"] = cooldown;
        if (chance < 1.0) j["chance"] = chance;
        if (!enabled) j["enabled"] = false;
        return j;
    }

    // ── DialDef ──────────────────────────────────────────────────────────────

    double DialDef::Clamp(double v) const {
        if (!std::isfinite(v)) return defaultValue;
        return std::clamp(v, min, max);
    }

    double DialDef::Exposed(double raw) const { return integer ? std::round(raw) : raw; }

    double DialDef::Precision() const {
        if (integer) return 1.0;
        if (precision > 0) return precision;
        return (max - min) / 200.0;
    }

    std::string DialDef::OptionLabel(double value) const {
        if (options.empty()) return {};
        auto it = options.find(static_cast<int>(std::lround(value)));
        return it == options.end() ? std::string() : it->second;
    }

    std::string DialDef::ConditionExpression(const std::string& text) const {
        const auto t = Trim(text);
        for (std::string_view op : {">=", "<=", "==", "!=", ">", "<"}) {
            if (t.starts_with(op)) return "$" + id + " " + t;
        }
        if (t.starts_with('=')) return "$" + id + " ==" + t.substr(1);
        for (const auto& [value, optionLabel] : options) {
            if (IEquals(optionLabel, t)) return "$" + id + " == " + std::to_string(value);
        }
        return t;
    }

    nlohmann::json DialDef::ToJson() const {
        nlohmann::json j = nlohmann::json::object();
        if (!label.empty() && label != id) j["label"] = label;
        if (!description.empty()) j["description"] = description;
        if (category != "dials") j["category"] = category;
        if (!icon.empty()) j["icon"] = icon;
        if (!unit.empty()) j["unit"] = unit;
        if (!enabled) j["enabled"] = false;
        switch (kind) {
            case Kind::Toggle: j["kind"] = "toggle"; break;
            case Kind::Choice: {
                j["kind"] = "choice";
                nlohmann::json o = nlohmann::json::object();
                for (const auto& [value, text] : options) o[std::to_string(value)] = text;
                j["options"] = o;
                break;
            }
            default:
                j["range"] = {min, max};
                if (integer) j["integer"] = true;
                break;
        }
        if (defaultValue != 0.0 || kind == Kind::Number && min > 0.0) j["default"] = defaultValue;
        if (precision > 0) j["precision"] = precision;
        // Three ways to read the same two scopes: one value, the player's own, each tracked actor's own
        j["scope"] = scope == Scope::Global ? "global" : track.empty() ? "player" : "actor";
        if (scope == Scope::Actor && !track.empty()) j["track"] = track;
        if (!persist) j["persist"] = false;
        if (!thresholds.empty()) j["thresholds"] = thresholds;
        if (!provides.empty()) {
            j["provides"] = provides;
            if (priority != 50) j["priority"] = priority;
        }
        if (!conditions.empty()) {
            nlohmann::json c = nlohmann::json::object();
            for (const auto& [name, text] : conditions) {
                if (auto k = knowledge.find(name); k != knowledge.end()) c[name] = {{"expr", text}, {"knowledge", k->second}};
                else c[name] = text;
            }
            j["conditions"] = c;
        }
        nlohmann::json rs = nlohmann::json::array();
        for (const auto& r : rules) rs.push_back(r.ToJson());
        j["rules"] = rs;
        return j;
    }

    // ── Parsing ──────────────────────────────────────────────────────────────

    ParseResult ParseDial(std::string_view id, const nlohmann::json& j, std::string_view source) {
        ParseResult result;
        auto& errors = result.errors;
        auto& warnings = result.warnings;
        DialDef def;
        def.id = std::string(id);
        def.label = def.id;
        def.source = std::string(source);
        const std::string where = "dial '" + def.id + "'";

        if (!IsValidId(id)) {
            errors.push_back(where + ": the name must be letters, digits and _ (not starting with a digit)");
            return result;
        }
        if (!j.is_object()) {
            errors.push_back(where + ": a dial must be an object");
            return result;
        }

        ReadString(j, "label", def.label, errors, where);
        if (def.label.empty()) def.label = def.id;
        ReadString(j, "description", def.description, errors, where);
        ReadString(j, "category", def.category, errors, where);
        if (def.category.empty()) def.category = "dials";
        ReadString(j, "icon", def.icon, errors, where);
        ReadString(j, "unit", def.unit, errors, where);
        ReadBool(j, "enabled", def.enabled, errors, where);

        // ── Kind and options ─────────────────────────────────────────────────
        std::string kind;
        ReadString(j, "kind", kind, errors, where);
        kind = Lower(kind);
        if (kind.empty()) kind = j.contains("options") ? "choice" : "number";
        if (kind == "number" || kind == "value") def.kind = Kind::Number;
        else if (kind == "toggle" || kind == "bool" || kind == "switch") def.kind = Kind::Toggle;
        else if (kind == "choice" || kind == "options") def.kind = Kind::Choice;
        else errors.push_back(where + ": \"kind\" must be number, toggle or choice");

        if (auto it = j.find("options"); it != j.end()) {
            if (it->is_array()) {
                int v = 0;
                for (const auto& o : *it) {
                    if (o.is_string()) def.options[v] = o.get<std::string>();
                    else errors.push_back(where + ": \"options\" entries must be text");
                    ++v;
                }
            } else if (it->is_object()) {
                for (const auto& [key, text] : it->items()) {
                    const auto v = ParseNumber(key);
                    if (!v || *v != std::floor(*v) || !text.is_string()) {
                        errors.push_back(where + ": options are {\"0\": \"Label\", ...} (whole numbers)");
                        continue;
                    }
                    def.options[static_cast<int>(*v)] = text.get<std::string>();
                }
            } else {
                errors.push_back(where + ": \"options\" must be a list of labels or {\"value\": \"label\"}");
            }
        }

        switch (def.kind) {
            case Kind::Toggle:
                def.min = 0;
                def.max = 1;
                def.integer = true;
                break;
            case Kind::Choice:
                if (def.options.empty()) {
                    errors.push_back(where + ": a choice dial needs \"options\"");
                } else {
                    def.min = def.options.begin()->first;
                    def.max = def.options.rbegin()->first;
                    if (def.min == def.max) def.max = def.min + 1;  // one option: still a valid range
                }
                def.integer = true;
                break;
            default:
                if (auto it = j.find("range"); it != j.end()) {
                    if (it->is_array() && it->size() == 2 && (*it)[0].is_number() && (*it)[1].is_number()) {
                        def.min = (*it)[0].get<double>();
                        def.max = (*it)[1].get<double>();
                    } else {
                        errors.push_back(where + ": \"range\" must be [min, max]");
                    }
                }
                ReadNumber(j, "min", def.min, errors, where);
                ReadNumber(j, "max", def.max, errors, where);
                ReadBool(j, "integer", def.integer, errors, where);
                if (!(def.min < def.max)) errors.push_back(where + ": the range's min must be below its max");
                break;
        }
        if (def.kind != Kind::Number && (j.contains("range") || j.contains("min") || j.contains("max"))) {
            warnings.push_back(where + ": a " + kind + " dial sets its own range");
        }

        def.defaultValue = def.kind == Kind::Number ? std::max(0.0, def.min) : def.min;
        if (def.kind == Kind::Number && def.max < 0) def.defaultValue = def.max;
        if (auto it = j.find("default"); it != j.end()) {
            bool found = false;
            if (it->is_string() && def.kind == Kind::Choice) {
                for (const auto& [value, label] : def.options) {
                    if (IEquals(label, it->get<std::string>())) {
                        def.defaultValue = value;
                        found = true;
                    }
                }
            }
            if (!found && it->is_boolean()) {
                def.defaultValue = it->get<bool>() ? def.max : def.min;
                found = true;
            }
            if (!found) ReadNumber(j, "default", def.defaultValue, errors, where);
        }
        if (def.defaultValue < def.min || def.defaultValue > def.max) {
            warnings.push_back(where + ": the default is outside the range; clamped");
            def.defaultValue = std::clamp(def.defaultValue, def.min, std::max(def.min, def.max));
        }
        ReadNumber(j, "precision", def.precision, errors, where);

        // ── Who has it ───────────────────────────────────────────────────────
        std::string scope;
        ReadString(j, "scope", scope, errors, where);
        scope = Lower(scope);
        if (scope.empty() || scope == "global") def.scope = Scope::Global;
        else if (scope == "actor" || scope == "player" || scope == "peractor") def.scope = Scope::Actor;
        else errors.push_back(where + ": \"scope\" must be global, actor or player");
        if (!ReadString(j, "track", def.track, errors, where)) ReadString(j, "actors", def.track, errors, where);
        if (scope == "player" && !def.track.empty()) {
            warnings.push_back(where + ": scope player ignores \"track\" (use scope actor)");
            def.track.clear();
        }
        if (IEquals(def.track, "IsPlayer")) def.track.clear();
        if (def.scope == Scope::Global && !def.track.empty()) {
            warnings.push_back(where + ": \"track\" only applies to actor dials");
            def.track.clear();
        }
        ReadBool(j, "persist", def.persist, errors, where);

        if (auto it = j.find("thresholds"); it != j.end()) {
            if (it->is_array() && std::all_of(it->begin(), it->end(), [](const auto& v) { return v.is_number(); })) {
                for (const auto& v : *it) def.thresholds.push_back(v.get<double>());
                std::sort(def.thresholds.begin(), def.thresholds.end());
            } else {
                errors.push_back(where + ": \"thresholds\" must be a list of numbers");
            }
        }

        if (auto it = j.find("provides"); it != j.end()) {
            if (it->is_string()) {
                def.provides.push_back(it->get<std::string>());
            } else if (it->is_array()) {
                for (const auto& p : *it) {
                    if (p.is_string()) def.provides.push_back(p.get<std::string>());
                }
            }
            for (auto& p : def.provides) {
                if (p.starts_with('$')) p.erase(0, 1);
                if (!IsValidId(p)) errors.push_back(where + ": provides '" + p + "' is not a value name");
                if (IEquals(p, def.id)) errors.push_back(where + ": a dial cannot provide itself");
            }
        }
        if (auto it = j.find("priority"); it != j.end()) {
            if (it->is_number()) def.priority = it->get<int>();
            else errors.push_back(where + ": \"priority\" must be a number");
        }

        if (auto it = j.find("conditions"); it != j.end()) {
            if (!it->is_object()) {
                errors.push_back(where + ": \"conditions\" must be {\"Name\": \"expression\"}");
            } else {
                for (const auto& [name, entry] : it->items()) {
                    auto bare = name.starts_with('@') ? name.substr(1) : name;
                    // "Name": "expression", or "Name": {"expr": "...", "knowledge": "what NPCs know while true"}
                    const nlohmann::json* expr = entry.is_object() ? (entry.contains("expr") ? &entry["expr"] : nullptr) : &entry;
                    if (!IsValidId(bare)) {
                        errors.push_back(where + ": condition name '" + name + "' must be letters, digits and _");
                    } else if (!expr || !expr->is_string() || Trim(expr->get<std::string>()).empty()) {
                        errors.push_back(where + ": condition '" + name + "' needs an expression");
                    } else {
                        def.conditions.emplace_back(bare, Trim(expr->get<std::string>()));
                        if (entry.is_object()) {
                            if (auto k = entry.find("knowledge"); k != entry.end()) {
                                if (k->is_string() && !Trim(k->get<std::string>()).empty()) def.knowledge[bare] = Trim(k->get<std::string>());
                                else errors.push_back(where + ": condition '" + name + "': \"knowledge\" must be text");
                            }
                        }
                    }
                }
            }
        }

        if (auto it = j.find("rules"); it != j.end()) {
            if (!it->is_array()) {
                errors.push_back(where + ": \"rules\" must be a list");
            } else {
                for (std::size_t i = 0; i < it->size(); ++i) {
                    if (auto r = ParseRule(def, i, (*it)[i], errors, warnings)) def.rules.push_back(std::move(*r));
                }
            }
        }

        for (const auto& [key, _] : j.items()) {
            if (!kDialKeys.contains(key) && !key.starts_with('_')) warnings.push_back(where + ": unknown key '" + key + "'");
        }

        if (errors.empty()) result.def = std::move(def);
        return result;
    }

} // namespace ConditionEngine::Dials
