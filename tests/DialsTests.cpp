// DialsTests.cpp - Out-of-game tests for the dial format and engine (src/Dials/Definition, Engine).
// OarValues, the copy OAR reads. A fake host stands in for ConditionEngine: conditions are flags, values are numbers, by name.

#include "../src/Dials/Definition.h"
#include "../src/Dials/Engine.h"
#include "../src/Dials/OarValues.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

using namespace ConditionEngine::Dials;

namespace {
    using CheckFn = void (*)(bool ok, const std::string& what);
    CheckFn Check = nullptr;

    bool Near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }
    std::string Num(double v) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g", v);
        return buf;
    }

    // Conditions: a name ("IsSwimming"), "!name" or "true". Values: a number name ("$hunger"),
    // "-name", "name * k" or DialsEvent. Anything containing "bad" does not compile.
    class FakeHost : public Host {
    public:
        std::map<std::string, std::map<ActorKey, bool>> flags;    // name -> actor (0 = everyone) -> value
        std::map<std::string, std::map<ActorKey, double>> values;  // name -> actor -> value
        std::map<std::uint32_t, std::set<std::string>> keywords;   // form -> keywords
        double random = 0.5;
        double eventNumber = 0;
        std::map<Handle, std::string> sources;
        Handle next = 1;
        int released = 0;

        Handle CompileCondition(const std::string& source, std::string& error) override {
            if (source.find("bad") != std::string::npos) {
                error = "unknown name";
                return 0;
            }
            sources[next] = source;
            return next++;
        }
        Handle CompileValue(const std::string& source, std::string& error) override { return CompileCondition(source, error); }
        void ReleaseCondition(Handle) override { ++released; }
        void ReleaseValue(Handle) override { ++released; }

        std::optional<bool> Flag(const std::string& name, ActorKey actor) {
            if (name == "true") return true;
            if (name == "Unavailable") return std::nullopt;
            auto it = flags.find(name);
            if (it == flags.end()) return false;
            const ActorKey key = actor ? actor : kPlayer;
            if (auto a = it->second.find(key); a != it->second.end()) return a->second;
            if (auto a = it->second.find(0); a != it->second.end()) return a->second;
            return false;
        }

        std::optional<bool> Evaluate(Handle condition, ActorKey actor) override {
            std::string s = sources.at(condition);
            if (s.starts_with('!')) {
                auto v = Flag(s.substr(1), actor);
                return v ? std::optional<bool>(!*v) : std::nullopt;
            }
            if (auto pos = s.find(" || "); pos != std::string::npos) {
                return Flag(s.substr(0, pos), actor).value_or(false) || Flag(s.substr(pos + 4), actor).value_or(false);
            }
            return Flag(s, actor);
        }

        std::optional<double> Value(const std::string& name, ActorKey actor) {
            if (name == "DialsEvent") return eventNumber;
            auto it = values.find(name);
            if (it == values.end()) return std::nullopt;
            const ActorKey key = actor ? actor : kPlayer;
            if (auto a = it->second.find(key); a != it->second.end()) return a->second;
            if (auto a = it->second.find(0); a != it->second.end()) return a->second;
            return std::nullopt;
        }

        std::optional<double> EvaluateValue(Handle value, ActorKey actor) override {
            std::string s = sources.at(value);
            double sign = 1;
            if (s.starts_with('-')) {
                sign = -1;
                s = s.substr(1);
            }
            double k = 1;
            if (auto pos = s.find(" * "); pos != std::string::npos) {
                k = std::stod(s.substr(pos + 3));
                s = s.substr(0, pos);
            }
            auto v = Value(s, actor);
            if (!v) return std::nullopt;
            return sign * k * *v;
        }

        bool FormMatches(const std::string& filter, std::uint32_t formID) override {
            return std::stoul(filter, nullptr, 16) == formID;
        }
        bool HasKeyword(const std::string& keyword, std::uint32_t formID) override {
            return keywords[formID].contains(keyword);
        }
        double Random01() override { return random; }
        void SetEventNumber(double number) override { eventNumber = number; }
    };

    DialDef Parse(const std::string& id, const std::string& json) {
        auto r = ParseDial(id, nlohmann::json::parse(json), "test");
        for (const auto& e : r.errors) std::cout << "  parse error: " << e << "\n";
        Check(r.def.has_value(), "dial '" + id + "' parses");
        return r.def.value_or(DialDef{});
    }

    std::vector<std::string> Errors(const std::string& json) {
        return ParseDial("d", nlohmann::json::parse(json), "test").errors;
    }

    bool HasError(const std::string& json, const std::string& part) {
        for (const auto& e : Errors(json)) {
            if (e.find(part) != std::string::npos) return true;
        }
        std::cout << "  no error containing '" << part << "' for " << json << "\n";
        return false;
    }

    double Get(Engine& e, const std::string& id, ActorKey actor = 0) { return e.Get(id, actor).value_or(-999); }

    void Define(Engine& e, const DialDef& def) {
        auto r = e.Define(def);
        for (const auto& err : r.errors) std::cout << "  define error: " << err << "\n";
        Check(r.ok, "define " + def.id);
    }

    // ── The format ───────────────────────────────────────────────────────────

    void FormatTests() {
        auto wet = Parse("wetness", R"({
            "label": "Wetness", "range": [0, 100], "unit": "%", "scope": "actor", "track": "IsPlayer || IsFollower",
            "rules": [
                {"while": "OutsideInRain", "rate": 2},
                {"when": "IsSwimming", "set": 100},
                {"while": "!OutsideInRain", "approach": 0, "halfLife": 90}
            ],
            "conditions": {"Soaked": ">= 75", "Dry": "$wetness < 5"}
        })");
        Check(wet.scope == Scope::Actor && wet.track == "IsPlayer || IsFollower", "wetness: actor dial with a track");
        Check(wet.rules.size() == 3 && wet.rules[0].action == ActionKind::Rate && wet.rules[0].IsContinuous(),
              "wetness: 3 rules, the first a rate while");
        Check(wet.rules[1].trigger == TriggerKind::When && wet.rules[1].value.constant == 100.0, "when ... set 100");
        Check(wet.ConditionExpression(">= 75") == "$wetness >= 75", "condition shorthand '>= 75'");
        Check(wet.ConditionExpression("$wetness < 5") == "$wetness < 5", "a full expression stays");
        Check(wet.ConditionExpression("= 3") == "$wetness == 3", "'= 3' becomes ==");

        // Round trip
        auto again = ParseDial("wetness", wet.ToJson(), "test");
        Check(again.def && again.def->ToJson() == wet.ToJson(), "wetness survives ToJson -> ParseDial: " + wet.ToJson().dump());

        auto mood = Parse("outfit_mode", R"({"options": ["Casual", "Formal", "Battle"], "default": "Formal",
                                            "rules": [{"key": "F5", "cycle": true}],
                                            "conditions": {"InBattleMode": "Battle"}})");
        Check(mood.kind == Kind::Choice && mood.min == 0 && mood.max == 2 && mood.integer, "choice dial from a list");
        Check(mood.defaultValue == 1, "default by option label");
        Check(mood.rules[0].key == 0x3F && mood.rules[0].action == ActionKind::Cycle && mood.rules[0].value.constant == 1.0,
              "key F5 cycles by 1");
        Check(mood.ConditionExpression("Battle") == "$outfit_mode == 2", "an option label as a condition");
        Check(mood.OptionLabel(2) == "Battle", "option label of 2");

        auto toggle = Parse("bard", R"({"kind": "toggle", "rules": [{"event": "Bard_Start", "set": 1}, {"event": "Bard_Stop", "set": 0}]})");
        Check(toggle.kind == Kind::Toggle && toggle.min == 0 && toggle.max == 1 && toggle.integer, "toggle dial");

        auto expr = Parse("morale", R"({"rules": [{"while": true, "rate": "-$hunger / 100", "clock": "game"}]})");
        Check(expr.rules[0].value.expr == "-$hunger / 100" && expr.rules[0].clock == Clock::Game && expr.rules[0].condition.empty(),
              "rate as an expression, game clock, while true");
        auto numText = Parse("n", R"({"rules": [{"on": "Hit", "add": "5"}]})");
        Check(numText.rules[0].value.constant == 5.0, "\"5\" is a number, not an expression");

        Check(HasError(R"({"rules": [{"set": 1}]})", "no trigger"), "a rule without a trigger");
        Check(HasError(R"({"rules": [{"on": "Hit", "event": "X", "set": 1}]})", "more than one trigger"), "two triggers");
        Check(HasError(R"({"rules": [{"on": "Hit", "set": 1, "add": 2}]})", "more than one action"), "two actions");
        Check(HasError(R"({"rules": [{"on": "Hit"}]})", "no action"), "no action");
        Check(HasError(R"({"rules": [{"on": "Hit", "rate": 1}]})", "needs \"while\""), "rate without while");
        Check(HasError(R"({"rules": [{"while": true, "add": 1}]})", "works with rate"), "while with add");
        Check(HasError(R"({"rules": [{"while": true, "approach": 0}]})", "halfLife"), "approach without halfLife");
        Check(HasError(R"({"rules": [{"on": "Explode", "set": 1}]})", "unknown game event"), "unknown game event");
        Check(HasError(R"({"rules": [{"key": "Banana", "set": 1}]})", "unknown key"), "unknown key");
        Check(HasError(R"({"rules": [{"on": "Hit", "set": 1, "chance": 25}]})", "chance"), "chance out of range");
        Check(HasError(R"({"rules": [{"while": true, "rate": 1, "for": 3}]})", "\"for\" does not work"), "for with while");
        Check(HasError(R"({"range": [10, 0]})", "min must be below"), "range upside down");
        Check(HasError(R"({"kind": "choice"})", "needs \"options\""), "choice without options");
        Check(HasError(R"({"scope": "everywhere"})", "scope"), "bad scope");
        Check(HasError(R"({"conditions": {"Bad Name": ">= 1"}})", "condition name"), "bad condition name");
        Check(!ParseDial("1abc", nlohmann::json::object(), "t").def, "a name cannot start with a digit");
        Check(!ParseDial("has space", nlohmann::json::object(), "t").def, "a name has no spaces");

        auto warn = ParseDial("w", nlohmann::json::parse(R"({"colour": "red", "_comment": "ok"})"), "t");
        Check(warn.def && warn.warnings.size() == 1, "an unknown key warns (a _comment does not)");

        // Three scopes: global, the player only, per actor
        auto global = Parse("sg", R"({})");
        auto player = Parse("sp", R"({"scope": "player"})");
        auto bareActor = Parse("sa", R"({"scope": "actor"})");
        auto followers = Parse("sf", R"({"scope": "actor", "track": "IsFollower"})");
        Check(global.scope == Scope::Global && global.ToJson()["scope"] == "global", "no scope: global");
        Check(player.scope == Scope::Actor && player.TracksPlayerOnly() && player.ToJson()["scope"] == "player",
              "scope player: the player's own value");
        Check(bareActor.TracksPlayerOnly() && bareActor.ToJson()["scope"] == "player", "an actor dial without track is the player's");
        Check(!followers.TracksPlayerOnly() && followers.ToJson()["scope"] == "actor" && followers.ToJson()["track"] == "IsFollower",
              "an actor dial with a track");
        Check(Parse("st", R"({"scope": "actor", "track": "IsPlayer"})").TracksPlayerOnly(), "track IsPlayer is the player only");

        Check(ParseKey("0x3F") == 0x3F && ParseKey(63) == 63 && ParseKey("num5") == 0x4C, "key forms");
        Check(KeyName(0x3F) == "F5", "key name");
        Check(ParseGameEvent("hit") == GameEvent::Hit && !ParseGameEvent("nope"), "game event names ignore case");
    }

    // ── The user's examples ──────────────────────────────────────────────────

    void ExampleTests() {
        // "Listen for an event, set a dial to true, then another event turns it off"
        {
            FakeHost host;
            Engine e(host);
            Define(e, Parse("bard", R"({"kind": "toggle", "rules": [{"event": "Bard_Start", "set": 1}, {"event": "Bard_Stop", "set": 0}]})"));
            EventInfo info;
            e.OnModEvent("bard_start", info);
            Check(Get(e, "bard") == 1, "event on (names ignore case)");
            e.OnModEvent("Bard_Stop", info);
            Check(Get(e, "bard") == 0, "another event off");
            auto changes = e.TakeChanges();
            Check(changes.size() == 2 && changes[0].notify && changes[0].value == 1 && changes[1].value == 0,
                  "two notified changes");
        }
        // "Wait for a condition, then set a dial to 10, lowering it 1 per second / by current hunger"
        {
            FakeHost host;
            Engine e(host);
            Define(e, Parse("alarm", R"({"range": [0, 10], "rules": [
                {"when": "InCombat", "set": 10},
                {"while": "!InCombat", "rate": -1}]})"));
            Define(e, Parse("hungry_alarm", R"({"range": [0, 10], "rules": [
                {"when": "InCombat", "set": 10},
                {"while": "!InCombat", "rate": "-$hunger * 0.01"}]})"));
            host.values["$hunger"][0] = 200;  // 2 per second
            e.Tick(0.25, 0, {kPlayer});        // baseline: not in combat
            host.flags["InCombat"][0] = true;
            e.Tick(0.25, 0, {kPlayer});
            Check(Get(e, "alarm") == 10 && Get(e, "hungry_alarm") == 10, "the condition became true: 10");
            host.flags["InCombat"][0] = false;
            for (int i = 0; i < 12; ++i) e.Tick(0.25, 0, {kPlayer});  // 3 s
            Check(Near(Get(e, "alarm"), 7), "1 per second for 3 s: 7, got " + Num(Get(e, "alarm")));
            Check(Near(Get(e, "hungry_alarm"), 4), "hunger 200 -> 2 per second: 4, got " + Num(Get(e, "hungry_alarm")));
            for (int i = 0; i < 40; ++i) e.Tick(0.25, 0, {kPlayer});
            Check(Get(e, "alarm") == 0, "stops at the range's min");
            host.flags["InCombat"][0] = true;
            e.Tick(0.25, 0, {kPlayer});
            Check(Get(e, "alarm") == 10, "fires again on the next rising edge");
        }
    }

    // ── Actions ──────────────────────────────────────────────────────────────

    void ActionTests() {
        FakeHost host;
        Engine e(host);
        Define(e, Parse("n", R"({"range": [0, 100], "default": 20})"));
        Define(e, Parse("i", R"({"range": [0, 10], "integer": true})"));
        Define(e, Parse("c", R"({"options": {"0": "Off", "5": "Low", "10": "High"}})"));
        Define(e, Parse("t", R"({"kind": "toggle"})"));

        Check(Get(e, "n") == 20, "default before anything");
        e.Set("n", 0, 150, "test");
        Check(Get(e, "n") == 100, "set clamps to max");
        e.Add("n", 0, -30, "test");
        Check(Get(e, "n") == 70, "add");
        e.Reset("n", 0, "test");
        Check(Get(e, "n") == 20, "reset to default");
        e.Set("i", 0, 3.6, "test");
        Check(Get(e, "i") == 4, "integer dials round");
        e.Cycle("c", 0, 1, "test");
        Check(Get(e, "c") == 5, "cycle to the next option");
        e.Cycle("c", 0, 1, "test");
        e.Cycle("c", 0, 1, "test");
        Check(Get(e, "c") == 0, "cycle wraps");
        e.Cycle("c", 0, -1, "test");
        Check(Get(e, "c") == 10, "cycle back wraps");
        e.Cycle("i", 0, 7, "test");
        Check(Get(e, "i") == 0, "cycle a number past max wraps to min (4 + 7 > 10)");
        e.Toggle("t", 0, "test");
        Check(Get(e, "t") == 1, "toggle on");
        e.Toggle("t", 0, "test");
        Check(Get(e, "t") == 0, "toggle off");
        Check(!e.Set("missing", 0, 1, "test") && !e.Get("missing", 0), "an unknown dial");
        Check(e.Get("N", 0).has_value(), "dial names ignore case");

        // Rules with expressions, multiply, the event number
        Define(e, Parse("hits", R"({"range": [0, 1000], "rules": [
            {"event": "Damage", "add": "DialsEvent * 2"},
            {"event": "Halve", "multiply": 0.5},
            {"event": "Missing", "add": "$nothing"}]})"));
        EventInfo info;
        info.number = 7;
        e.OnModEvent("Damage", info);
        Check(Get(e, "hits") == 14, "add DialsEvent * 2 with number 7");
        e.OnModEvent("Halve", info);
        Check(Get(e, "hits") == 7, "multiply 0.5");
        e.OnModEvent("Missing", info);
        Check(Get(e, "hits") == 7, "an unavailable amount does nothing");
    }

    // ── for, approach, game clock, every ────────────────────────────────────

    void TimeTests() {
        {
            FakeHost host;
            Engine e(host);
            Define(e, Parse("burning", R"({"kind": "toggle", "rules": [{"event": "Fire", "set": 1, "for": 5}]})"));
            Define(e, Parse("buff", R"({"range": [0, 100], "rules": [{"event": "Buff", "add": 10, "for": 10}]})"));
            EventInfo info;
            e.OnModEvent("Fire", info);
            e.Tick(3, 0, {kPlayer});
            e.OnModEvent("Fire", info);  // again at 3 s: ends at 8 s, back to 0 (not 1)
            e.Tick(4, 0, {kPlayer});     // 7 s
            Check(Get(e, "burning") == 1, "still burning at 7 s");
            e.Tick(1.5, 0, {kPlayer});   // 8.5 s
            Check(Get(e, "burning") == 0, "set ... for 5: back to what it was, refreshed by the second event");

            e.OnModEvent("Buff", info);  // +10 until 18.5
            e.Tick(5, 0, {kPlayer});     // 13.5
            e.OnModEvent("Buff", info);  // +10 until 23.5
            Check(Get(e, "buff") == 20, "two stacked adds");
            e.Tick(5.5, 0, {kPlayer});   // 19
            Check(Get(e, "buff") == 10, "the first add wore off");
            e.Tick(5, 0, {kPlayer});
            Check(Get(e, "buff") == 0, "the second add wore off");
        }
        {
            FakeHost host;
            Engine e(host);
            Define(e, Parse("fade", R"({"default": 100, "rules": [{"while": true, "approach": 0, "halfLife": 10}]})"));
            for (int i = 0; i < 40; ++i) e.Tick(0.25, 0, {kPlayer});
            Check(Near(Get(e, "fade"), 50, 0.01), "half-life 10 s: 50 after 10 s, got " + Num(Get(e, "fade")));
        }
        {
            FakeHost host;
            Engine e(host);
            Define(e, Parse("awake", R"({"range": [0, 1000], "rules": [
                {"while": true, "rate": 1, "clock": "game"},
                {"on": "SleepStop", "reset": true}]})"));
            e.SetMaxCatchUpHours(48);
            e.Tick(0.25, 10.0, {kPlayer});  // first tick: the clock's baseline
            e.Tick(0.25, 12.5, {kPlayer});
            Check(Near(Get(e, "awake"), 2.5), "1 per game hour: 2.5 after 2.5 hours");
            e.Tick(0.25, 20.5, {kPlayer});  // slept 8 hours
            Check(Near(Get(e, "awake"), 10.5), "catches up a jump of 8 game hours");
            e.Tick(0.25, 200, {kPlayer});
            Check(Near(Get(e, "awake"), 58.5), "catch-up stops at 48 hours, got " + Num(Get(e, "awake")));
            EventInfo info;
            info.actor = kPlayer;
            e.OnGameEvent(GameEvent::SleepStop, info);
            Check(Get(e, "awake") == 0, "on SleepStop reset");
        }
        {
            FakeHost host;
            Engine e(host);
            Define(e, Parse("ticks", R"({"range": [0, 100], "rules": [{"every": 2, "add": 1}]})"));
            for (int i = 0; i < 20; ++i) e.Tick(0.25, 0, {kPlayer});  // 5 s
            Check(Get(e, "ticks") == 2, "every 2 s for 5 s: twice, got " + Num(Get(e, "ticks")));
        }
    }

    // ── Actors ───────────────────────────────────────────────────────────────

    void ActorTests() {
        constexpr ActorKey kLydia = 0xA2C94, kBandit = 0x123456;
        FakeHost host;
        host.flags["IsFollower"][kLydia] = true;
        Engine e(host);
        Define(e, Parse("fear", R"({"scope": "actor", "track": "IsPlayer || IsFollower", "rules": [
            {"on": "Hit", "add": 10},
            {"on": "Hit", "add": 100, "other": "player", "actor": "other"},
            {"while": "Calm", "rate": -1}]})"));
        host.flags["IsPlayer"][kPlayer] = true;

        EventInfo hit;
        hit.actor = kLydia;
        hit.other = kBandit;
        e.OnGameEvent(GameEvent::Hit, hit);
        Check(Get(e, "fear", kLydia) == 10, "a follower hit: her own fear");
        Check(Get(e, "fear", kPlayer) == 0, "the player's fear is separate");
        hit.actor = kBandit;
        hit.other = kLydia;
        e.OnGameEvent(GameEvent::Hit, hit);
        Check(Get(e, "fear", kBandit) == 0 && e.Values("fear").size() == 1, "an untracked bandit gets no fear");
        hit.actor = kBandit;
        hit.other = kPlayer;
        e.OnGameEvent(GameEvent::Hit, hit);  // actor "other" = the player hit the bandit -> player's fear? the player is the other
        Check(Get(e, "fear", kPlayer) == 100, "actor: other acts on the event's other actor (the player)");

        host.flags["Calm"][kLydia] = true;
        e.Tick(1, 0, {kPlayer, kLydia, kBandit});
        Check(Near(Get(e, "fear", kLydia), 9), "rates run per tracked actor where their condition holds");
        Check(Get(e, "fear", kPlayer) == 100, "the player is not calm");
        auto tracked = e.TrackedActors("fear");
        Check(tracked.size() == 2, "player and Lydia are tracked, the bandit is not");

        // Global dials act once, whoever caused it; "if" checks the event's actor
        Define(e, Parse("kills", R"({"rules": [{"on": "Kill", "add": 1, "if": "IsPlayer"}]})"));
        EventInfo kill;
        kill.actor = kPlayer;
        e.OnGameEvent(GameEvent::Kill, kill);
        kill.actor = kLydia;
        e.OnGameEvent(GameEvent::Kill, kill);
        Check(Get(e, "kills") == 1 && Get(e, "kills", kLydia) == 1, "a global dial: only the player's kill counted");

        // Keys and the crosshair
        Define(e, Parse("marked", R"({"kind": "toggle", "scope": "actor", "track": "true", "rules": [
            {"key": "M", "toggle": true, "actor": "crosshair"}]})"));
        e.OnKey(0x32, kBandit);
        Check(Get(e, "marked", kBandit) == 1, "key M toggles the actor under the crosshair");
        e.OnKey(0x32, 0);
        Check(Get(e, "marked", kPlayer) == 0, "no crosshair target: nothing happens");
    }

    // ── Filters, cooldown, chance ────────────────────────────────────────────

    void FilterTests() {
        FakeHost host;
        host.keywords[0x1000] = {"WeapTypeSword"};
        Engine e(host);
        Define(e, Parse("f", R"({"range": [0, 1000], "rules": [
            {"on": "Equip", "add": 1, "form": "0x1000"},
            {"on": "Equip", "add": 10, "keyword": "WeapTypeSword"},
            {"event": "Say", "add": 100, "text": "hello"},
            {"on": "Hit", "add": 100, "cooldown": 5},
            {"event": "Lucky", "add": 1, "chance": 0.25}]})"));
        EventInfo equip;
        equip.actor = kPlayer;
        equip.form = 0x1000;
        e.OnGameEvent(GameEvent::Equip, equip);
        Check(Get(e, "f") == 11, "form and keyword filters both match");
        equip.form = 0x2000;
        e.OnGameEvent(GameEvent::Equip, equip);
        Check(Get(e, "f") == 11, "another form matches neither");
        EventInfo say;
        say.text = "HELLO";
        e.OnModEvent("Say", say);
        say.text = "bye";
        e.OnModEvent("Say", say);
        Check(Get(e, "f") == 111, "the text filter (ignores case)");

        e.Set("f", 0, 0, "test");
        EventInfo hit;
        hit.actor = kPlayer;
        e.OnGameEvent(GameEvent::Hit, hit);
        e.Tick(2, 0, {kPlayer});
        e.OnGameEvent(GameEvent::Hit, hit);
        Check(Get(e, "f") == 100, "a second hit inside the cooldown does nothing");
        e.Tick(4, 0, {kPlayer});
        e.OnGameEvent(GameEvent::Hit, hit);
        Check(Get(e, "f") == 200, "after the cooldown it fires again");

        e.Set("f", 0, 0, "test");
        host.random = 0.5;
        e.OnModEvent("Lucky", {});
        host.random = 0.1;
        e.OnModEvent("Lucky", {});
        Check(Get(e, "f") == 1, "chance 0.25: fires on a roll under 0.25 only");
    }

    // ── Publishing ───────────────────────────────────────────────────────────

    void PublishTests() {
        FakeHost host;
        Engine e(host);
        Define(e, Parse("p", R"({"range": [0, 100], "thresholds": [50], "rules": [{"while": true, "rate": 0.1}]})"));
        e.TakeChanges();
        for (int i = 0; i < 4; ++i) e.Tick(1, 0, {kPlayer});  // 0.4: below the 0.5 precision
        Check(e.TakeChanges().empty(), "changes under the precision are not published");
        e.Tick(1, 0, {kPlayer});  // 0.5
        auto c = e.TakeChanges();
        Check(c.size() == 1 && !c[0].notify && Near(c[0].value, 0.5), "published at the precision, without a mod event");
        e.Set("p", 0, 49.8, "test");
        e.TakeChanges();
        for (int i = 0; i < 3; ++i) e.Tick(1, 0, {kPlayer});  // crosses 50
        c = e.TakeChanges();
        Check(!c.empty() && c.back().notify, "crossing a threshold sends a mod event");
        Check(!e.RecentLog().empty() && e.RecentLog().back().id == "p", "the log keeps notified changes");
    }

    // ── Definitions over time, saving ────────────────────────────────────────

    void PersistenceTests() {
        constexpr ActorKey kLydia = 0xA2C94;
        FakeHost host;
        host.flags["IsFollower"][kLydia] = true;
        Engine e(host);
        Define(e, Parse("g", R"({"range": [0, 100], "rules": [{"event": "Tmp", "add": 5, "for": 30}]})"));
        Define(e, Parse("a", R"({"scope": "actor", "track": "IsFollower"})"));
        Define(e, Parse("temp", R"({"persist": false})"));
        e.Set("g", 0, 40, "test");
        e.Set("a", kLydia, 7, "test");
        e.Set("temp", 0, 9, "test");
        e.OnModEvent("Tmp", {});
        e.Tick(10, 0, {kPlayer});  // 20 s left on the add

        // A dial nobody defines in this session: its values must survive the round trip
        Engine other(host);
        Define(other, Parse("modded", "{}"));
        other.Set("modded", 0, 33, "test");
        auto otherSave = other.Save();

        auto bytes = e.Save();
        Engine loaded(host);
        Define(loaded, Parse("g", R"({"range": [0, 100], "rules": [{"event": "Tmp", "add": 5, "for": 30}]})"));
        Define(loaded, Parse("a", R"({"scope": "actor", "track": "IsFollower"})"));
        Define(loaded, Parse("temp", R"({"persist": false})"));
        Check(loaded.Load(bytes, [](std::uint32_t id) { return std::optional<std::uint32_t>(id); }), "load");
        loaded.OnSessionStart();
        Check(Get(loaded, "g") == 45 && Get(loaded, "a", kLydia) == 7, "values come back");
        Check(Get(loaded, "temp") == 0, "persist: false is not saved");
        auto changes = loaded.TakeChanges();
        Check(changes.size() == 2, "session start publishes every loaded value: " + std::to_string(changes.size()));
        loaded.Tick(19, 0, {kPlayer});
        Check(Get(loaded, "g") == 45, "the add has 1 s left");
        loaded.Tick(2, 0, {kPlayer});
        Check(Get(loaded, "g") == 40, "the add ends after load, on the time it had left");

        // Form IDs that no longer resolve are dropped
        Engine gone(host);
        Define(gone, Parse("a", R"({"scope": "actor", "track": "IsFollower"})"));
        gone.Load(bytes, [](std::uint32_t) { return std::optional<std::uint32_t>(); });
        Check(gone.Values("a").empty(), "an actor that is gone loses its value");

        // Orphans: saved, kept, adopted when the dial shows up (a Papyrus Define after load)
        Engine later(host);
        later.Load(otherSave, nullptr);
        auto resaved = later.Save();
        Engine latest(host);
        latest.Load(resaved, nullptr);
        Define(latest, Parse("modded", "{}"));
        Check(Get(latest, "modded") == 33, "an undefined dial's value is kept across saves and adopted");

        // Redefining keeps values, clamped to the new range
        Define(e, Parse("g", R"({"range": [0, 10]})"));
        Check(Get(e, "g") == 10, "a narrower range clamps the value");
        Check(e.Remove("g") && !e.Exists("g"), "remove");
        Define(e, Parse("g", R"({"range": [0, 100]})"));
        Check(Get(e, "g") == 10, "removed and defined again in one session: the value comes back");

        // Compile errors refuse the dial and release what compiled
        const int released = host.released;
        auto r = e.Define(Parse("broken", R"({"rules": [{"when": "IsSwimming", "set": 1}, {"when": "bad thing", "set": 2}]})"));
        Check(!r.ok && !r.errors.empty() && r.errors[0].find("rule 2") != std::string::npos, "a bad expression names its rule");
        Check(host.released == released + 1, "the rule that compiled was released");
        Check(!e.Exists("broken"), "a dial that does not compile is not added");

        // Sources
        auto s1 = Parse("s1", "{}");
        s1.source = "ModA";
        auto s2 = Parse("s2", "{}");
        s2.source = "ModA";
        Define(e, s1);
        Define(e, s2);
        Check(e.RemoveSource("moda") == 2 && !e.Exists("s1"), "remove by source");

        // Fire a rule by hand (the editor's Test button)
        Define(e, Parse("tb", R"({"range": [0, 100], "rules": [{"on": "Hit", "add": 5}, {"while": true, "rate": 3}]})"));
        Check(e.FireRule("tb", 0, 0) && Get(e, "tb") == 5, "test-fire an event rule");
        Check(e.FireRule("tb", 1, 0) && Get(e, "tb") == 8, "test-fire a rate: one second of it");
        Check(!e.FireRule("tb", 9, 0), "no rule 10");

        e.ClearValues();
        Check(Get(e, "tb") == 0, "ClearValues: defaults again");
    }

    // ── SkyrimNet: triggers, tell, knowledge, AI actions ─────────────────────
    void SkyrimNetTests() {
        auto manic = Parse("manic", R"({
            "kind": "toggle", "scope": "actor", "track": "IsFollower",
            "rules": [
                {"every": 1, "clock": "game", "chance": 0.05, "cooldown": 20, "set": 1, "for": 0.5,
                 "tell": "{name} feels a manic surge."},
                {"skyrimnet": "dialogue", "add": 0, "actor": "other"},
                {"aiAction": "CalmDown", "description": "Calm down after a manic spell", "if": "Manic", "set": 0}
            ],
            "conditions": {"Manic": {"expr": "== 1", "knowledge": "You are in a manic phase."}, "Calm": "== 0"}
        })");
        Check(manic.rules[1].trigger == TriggerKind::Event && manic.rules[1].event == "SkyrimNet:dialogue",
              "skyrimnet trigger is the event SkyrimNet:dialogue");
        Check(manic.rules[2].IsAiAction() && manic.rules[2].AiActionName() == "CalmDown", "aiAction trigger");
        Check(manic.knowledge.size() == 1 && manic.knowledge.at("Manic") == "You are in a manic phase.", "condition knowledge");
        bool manicExpr = false;
        for (const auto& [name, text] : manic.conditions) manicExpr = manicExpr || (name == "Manic" && manic.ConditionExpression(text) == "$manic == 1");
        Check(manicExpr, "an object condition keeps its expression");
        const auto again = ParseDial("manic", manic.ToJson(), "test");
        Check(again.def && again.def->ToJson() == manic.ToJson(), "SkyrimNet keys survive a round trip: " + manic.ToJson().dump());
        Check(HasError(R"({"rules": [{"aiAction": "X", "set": 1}]})", "description"), "an aiAction needs a description");
        Check(HasError(R"({"rules": [{"while": true, "rate": 1, "tell": "hi"}]})", "does not work with"), "tell with while");
        Check(HasError(R"({"rules": [{"skyrimnet": "", "set": 1}]})", "event type"), "skyrimnet needs a type");

        constexpr ActorKey kLydia = 0xA2C94;
        FakeHost host;
        host.flags["IsFollower"][kLydia] = true;
        Engine e(host);
        Define(e, manic);
        e.TakeChanges();
        host.random = 0.0;  // the chance roll passes
        e.Tick(0.2, 10.0, {kPlayer, kLydia});
        e.Tick(0.2, 11.0, {kPlayer, kLydia});  // one game hour: the timer fires
        Check(Get(e, "manic", kLydia) == 1, "manic set by the hourly roll");
        auto notes = e.TakeNotes();
        Check(notes.size() == 1 && notes[0].actor == kLydia && notes[0].text == "{name} feels a manic surge." &&
                  notes[0].value == 1,
              "tell leaves a note for SkyrimNet");
        e.Tick(0.2, 11.4, {kPlayer, kLydia});
        Check(Get(e, "manic", kLydia) == 1, "still manic 24 game minutes in");
        e.Tick(0.2, 11.6, {kPlayer, kLydia});
        Check(Get(e, "manic", kLydia) == 0, "half an hour later it is over");

        // The AI action: offered where "if" holds, fires through its event
        host.flags["Manic"][kLydia] = true;
        Check(e.Eligible("manic", 2, kLydia), "CalmDown is offered to a manic follower");
        Check(!e.Eligible("manic", 2, kPlayer), "...not to the player (outside track)");
        e.Set("manic", kLydia, 1, "test");
        EventInfo act;
        act.actor = kLydia;
        e.OnModEvent("SkyrimNetAction:CalmDown", act);
        Check(Get(e, "manic", kLydia) == 0, "the AI action calmed her");
        host.flags["Manic"][kLydia] = false;
        Check(!e.Eligible("manic", 2, kLydia), "not offered once calm");
    }

    // ── send: rules that send SKSE mod events ───────────────────────────────
    void SendTests() {
        auto alarm = Parse("alarm", R"({
            "range": [0, 10],
            "rules": [
                {"on": "CombatStart", "set": 10},
                {"when": "Combat", "edge": "falling", "send": "MyMod_FightOver", "sendText": "calm", "cooldown": 5},
                {"event": "MyMod_Ping", "send": "MyMod_Pong", "sendNumber": "$hunger"}
            ]
        })");
        Check(alarm.rules[1].action == ActionKind::Send && alarm.rules[1].send == "MyMod_FightOver" &&
                  alarm.rules[1].sendText == "calm" && !alarm.rules[1].value.IsSet(),
              "send: name and text, no number (the dial's value)");
        Check(alarm.rules[2].value.expr == "$hunger", "sendNumber as a value expression");
        const auto again = ParseDial("alarm", alarm.ToJson(), "test");
        Check(again.def && again.def->ToJson() == alarm.ToJson(), "send survives a round trip: " + alarm.ToJson().dump());

        Check(HasError(R"({"rules": [{"while": true, "send": "X"}]})", "every tick"), "send does not work with while");
        Check(HasError(R"({"rules": [{"on": "Hit", "send": "X", "for": 5}]})", "cannot be undone"), "send with for");
        Check(HasError(R"({"rules": [{"on": "Hit", "send": ""}]})", "needs the mod event"), "send needs a name");
        Check(HasError(R"({"rules": [{"on": "Hit", "send": "Dials_Changed"}]})", "cannot send"), "Dials_Changed is reserved");
        Check(HasError(R"({"rules": [{"on": "Hit", "send": "skyrimnet:dialogue"}]})", "cannot send"), "SkyrimNet names are reserved");
        Check(HasError(R"({"rules": [{"on": "Hit", "send": "X", "add": 1}]})", "more than one action"), "send is the one action");
        Check(!ParseDial("d", nlohmann::json::parse(R"({"rules": [{"on": "Hit", "add": 1, "sendText": "x"}]})"), "test").warnings.empty(),
              "sendText without send warns");

        FakeHost host;
        host.values["$hunger"][0] = 42;
        Engine e(host);
        Define(e, alarm);
        e.Tick(0.2, 10.0, {kPlayer});
        e.TakeChanges();

        // The dial's value goes with the event; the dial itself does not change
        host.flags["Combat"][0] = true;
        e.Tick(0.2, 10.0, {kPlayer});
        e.Set("alarm", 0, 7, "test");
        host.flags["Combat"][0] = false;
        e.Tick(0.2, 10.0, {kPlayer});
        auto sends = e.TakeSends();
        Check(sends.size() == 1 && sends[0].name == "MyMod_FightOver" && sends[0].text == "calm" && sends[0].number == 7 &&
                  sends[0].dial == "alarm" && sends[0].actor == kPlayer,
              "the fight ends: MyMod_FightOver with the dial's value 7");
        Check(Get(e, "alarm") == 7, "send does not change the dial");
        bool logged = false;
        for (const auto& l : e.RecentLog()) logged = logged || l.cause.starts_with("sent MyMod_FightOver");
        Check(logged, "the editor's log shows the sent event");

        // sendNumber, and the cooldown applies as for any rule
        EventInfo ping;
        ping.actor = kPlayer;
        e.OnModEvent("MyMod_Ping", ping);
        sends = e.TakeSends();
        Check(sends.size() == 1 && sends[0].name == "MyMod_Pong" && sends[0].number == 42, "sendNumber $hunger = 42");

        // A dial does not hear the events its own rules sent; other dials do
        auto echo = Parse("echo", R"({"rules": [{"event": "MyMod_Pong", "add": 1}]})");
        Define(e, echo);
        EventInfo pong;
        pong.actor = kPlayer;
        pong.fromDial = "echo";
        e.OnModEvent("MyMod_Pong", pong);
        Check(Get(e, "echo") == 0, "a dial does not hear its own sent event");
        pong.fromDial = "alarm";
        e.OnModEvent("MyMod_Pong", pong);
        Check(Get(e, "echo") == 1, "another dial's sent event is heard");

        // At most kMaxSendsPerTick between two ticks; the rest are dropped and logged
        e.Tick(0.2, 10.0, {kPlayer});
        for (int i = 0; i < 40; ++i) e.OnModEvent("MyMod_Ping", ping);
        sends = e.TakeSends();
        Check(sends.size() == kMaxSendsPerTick, "40 pings in one tick send " + std::to_string(sends.size()) + " pongs (the cap)");
        Check(e.DroppedSends() == 40 - kMaxSendsPerTick, "the rest are counted as dropped");
        e.Tick(0.2, 10.0, {kPlayer});
        bool dropLogged = false;
        for (const auto& l : e.RecentLog()) dropLogged = dropLogged || l.cause.starts_with("dropped 8 mod events");
        Check(dropLogged && e.DroppedSends() == 0, "the next tick logs the drop and opens a new window");
        e.OnModEvent("MyMod_Ping", ping);
        Check(e.TakeSends().size() == 1, "after the tick, rules can send again");

        // The editor's Test button sends too
        Check(e.FireRule("alarm", 1, kPlayer) && e.TakeSends().size() == 1, "Test runs a send rule");
    }

    // ── OAR: the values OAR's ConditionEngineDial reads ─────────────────────

    void OarTests() {
        constexpr ActorKey kLydia = 0xA2C94;
        Check(!Parse("plain", "{}").oar, "dials are not shared with OAR by default");
        Check(!Parse("plain", "{}").ToJson().contains("oar"), "oar: false is not written");
        const auto sharedDef = Parse("Alarm", R"({"range": [0, 10], "oar": true, "rules": [{"while": true, "rate": 1}]})");
        Check(sharedDef.oar && sharedDef.ToJson().value("oar", false), "oar: true parses and is written back");
        Check(HasError(R"({"oar": "yes"})", "oar"), "oar must be true or false");

        FakeHost host;
        Engine e(host);
        auto& oar = OarValues::Get();
        oar.Clear();
        const auto mood = Parse("Mood", R"({"kind": "choice", "options": ["Calm", "Tense"], "scope": "actor", "track": "true", "oar": true})");
        const auto hidden = Parse("Hidden", R"({"range": [0, 100], "default": 7})");
        for (const auto* def : {&sharedDef, &mood, &hidden}) {
            Define(e, *def);
            oar.Define(*def, e.Values(def->id));  // as the service does when a dial goes live
        }
        Check(oar.SharedCount() == 2, "two dials shared with OAR");
        e.Set("Mood", kLydia, 1, "test");
        e.Tick(3, 0, {kPlayer, kLydia});
        for (const auto& c : e.TakeChanges()) oar.Update(c.id, c.actor, c.value);  // as Publish does

        auto r = oar.Read("alarm", kLydia, true);
        Check(r.status == OarValues::Status::Ok && Near(r.value, Get(e, "Alarm")) && r.value > 0,
              "a global dial: one value for every actor, any case");
        r = oar.Read("Mood", kLydia, true);
        Check(r.status == OarValues::Status::Ok && Near(r.value, 1) && r.text == "1 (Tense)", "an actor dial: " + r.text);
        r = oar.Read("Mood", kPlayer);
        Check(r.status == OarValues::Status::Ok && Near(r.value, 0), "an actor without a value reads the default");
        r = oar.Read("Hidden", kPlayer, true);
        Check(r.status == OarValues::Status::NotShared && r.text.find("oar") != std::string::npos,
              "a dial without oar: true is not readable, and says why");
        Check(oar.Read("Nobody", kPlayer).status == OarValues::Status::Unknown, "an unknown dial");

        oar.Update("Hidden", 0, 50);
        Check(oar.Read("Hidden", 0).status == OarValues::Status::NotShared, "changes of unshared dials are ignored");
        oar.ClearValues();
        Check(Near(oar.Read("Mood", kLydia).value, 0) && Near(oar.Read("Alarm", 0).value, 0),
              "a new session: values back to the defaults, dials kept");
        oar.Remove("Alarm");
        Check(oar.Read("Alarm", 0).status == OarValues::Status::Unknown && oar.SharedCount() == 1, "a removed dial");
        oar.Clear();
    }

    // ── The shipped examples (and so the editor's templates) are valid ───────
    void ExampleFileTests() {
#ifdef DIALS_SOURCE_DIR
        std::ifstream in(std::string(DIALS_SOURCE_DIR) + "/docs/examples/dials.json");
        Check(in.good(), "docs/examples/dials.json exists");
        if (!in) return;
        const auto file = nlohmann::json::parse(in, nullptr, true, true);
        int n = 0;
        for (const auto& [id, j] : file["dials"].items()) {
            auto r = ParseDial(id, j, "examples");
            ++n;
            for (const auto& e : r.errors) std::cout << "  " << e << "\n";
            for (const auto& w : r.warnings) std::cout << "  " << w << "\n";
            Check(r.def && r.warnings.empty(), "example " + id + " parses cleanly");
            FakeHost host;
            Engine e(host);
            if (r.def) Check(e.Define(*r.def).ok, "example " + id + " compiles");
        }
        Check(n >= 10, "the examples file has the ideas");
#endif
    }
}

void RunDialsTests(void (*check)(bool ok, const std::string& what)) {
    Check = check;
    FormatTests();
    ExampleTests();
    ActionTests();
    TimeTests();
    ActorTests();
    FilterTests();
    PublishTests();
    PersistenceTests();
    SkyrimNetTests();
    SendTests();
    OarTests();
    ExampleFileTests();
}
