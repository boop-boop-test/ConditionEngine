#pragma once

// Engine.h - Runs dials: keeps their values per actor, applies rules on events, timers
// and conditions, integrates rates, undoes "for" effects, saves and loads.
//
// No game code: the game layer (or a test) implements Host for expressions, the game
// clock and form checks, feeds events in and publishes the changes that come out.
// Thread-safe: every public member takes the engine's lock. Host calls happen under it,
// so a Host must not call back into the engine.

#include "Definition.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>

namespace ConditionEngine::Dials {

    using Handle = std::uint32_t;  // a compiled expression; 0 = none

    class Host {
    public:
        virtual ~Host() = default;

        // Compile a condition / a value expression. 0 and `error` set on failure.
        virtual Handle CompileCondition(const std::string& source, std::string& error) = 0;
        virtual Handle CompileValue(const std::string& source, std::string& error) = 0;
        virtual void ReleaseCondition(Handle handle) = 0;
        virtual void ReleaseValue(Handle handle) = 0;

        // nullopt: unavailable or pending (rules then do nothing). `actor` 0 = the player.
        virtual std::optional<bool> Evaluate(Handle condition, ActorKey actor) = 0;
        virtual std::optional<double> EvaluateValue(Handle value, ActorKey actor) = 0;

        // The event's form matches a form filter ("0x12EB7|Skyrim.esm", an editor ID) /
        // has a keyword (editor ID or form string)
        virtual bool FormMatches(const std::string& filter, std::uint32_t formID) = 0;
        virtual bool HasKeyword(const std::string& keyword, std::uint32_t formID) = 0;

        virtual double Random01() = 0;

        // The number DialsEvent reads while a rule's expressions run
        virtual void SetEventNumber(double number) = 0;
    };

    // What happened, for game events and mod events
    struct EventInfo {
        ActorKey actor = 0;    // the event's actor (0: none / not an actor)
        ActorKey other = 0;    // its other actor
        std::uint32_t form = 0;
        std::string text;
        double number = 1.0;
        ActorKey crosshair = 0;
        std::string fromDial;  // mod events: the dial whose rule sent it (it does not hear its own events)
    };

    // A rule with "send" fired: the game layer sends this SKSE mod event on its next tick
    struct Send {
        std::string dial;   // the dial whose rule sent it
        std::string name;   // eventName
        std::string text;   // strArg
        double number = 0;  // numArg
        ActorKey actor = 0; // sender: the actor the rule acted on
    };
    // Most mod events dials send between two ticks; more are dropped (and logged), so a mistake cannot flood scripts
    constexpr std::size_t kMaxSendsPerTick = 32;

    // A published value changed
    struct Change {
        std::string id;
        ActorKey actor = 0;   // 0 for global dials
        double value = 0;     // exposed (rounded for integer dials)
        double previous = 0;
        std::string cause;
        bool notify = false;  // send Dials_Changed (discrete change, threshold crossed, bound reached)
    };

    // A rule with "tell" fired: SkyrimNet hears about it
    struct Note {
        std::string id;
        ActorKey actor = 0;  // who it is about (the player for global dials fired by keys and timers)
        std::string text;    // as written: {name}, {value}, {label} are filled in by the game layer
        bool narration = false;
        double value = 0;
        std::string label;   // the dial's label, or a choice dial's option
    };

    struct LogEntry {
        double time = 0;  // real seconds since start
        std::string id;
        ActorKey actor = 0;
        double from = 0;
        double to = 0;
        std::string cause;
    };

    struct DefineResult {
        bool ok = false;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    // Maps a saved form ID to this session's (SKSE ResolveFormID); nullopt = gone
    using FormResolver = std::function<std::optional<std::uint32_t>(std::uint32_t)>;

    class Engine {
    public:
        explicit Engine(Host& host);
        ~Engine();
        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;

        // ── Definitions ──────────────────────────────────────────────────────
        // Compiles every expression. Replacing a dial keeps its values (clamped to the new
        // range); values saved for a dial nobody defined yet are adopted.
        DefineResult Define(DialDef def);
        bool Remove(const std::string& id);
        std::size_t RemoveSource(const std::string& source);
        std::vector<DialDef> Definitions() const;
        std::optional<DialDef> Find(const std::string& id) const;
        bool Exists(const std::string& id) const;

        // ── Values ───────────────────────────────────────────────────────────
        // `actor` 0 = the player (actor dials) / the value (global dials). Exposed values.
        std::optional<double> Get(const std::string& id, ActorKey actor) const;
        bool Set(const std::string& id, ActorKey actor, double value, const std::string& cause);
        bool Add(const std::string& id, ActorKey actor, double delta, const std::string& cause);
        bool Reset(const std::string& id, ActorKey actor, const std::string& cause);
        bool Toggle(const std::string& id, ActorKey actor, const std::string& cause);
        bool Cycle(const std::string& id, ActorKey actor, int step, const std::string& cause);
        // Actors with their own value of an actor dial
        std::vector<std::pair<ActorKey, double>> Values(const std::string& id) const;
        // Runs one rule now as if it fired (the editor's Test button). False: no such rule.
        bool FireRule(const std::string& id, std::size_t rule, ActorKey actor);
        // Could this rule fire for the actor now: it has the dial, "if" holds, no cooldown (main thread;
        // chance is not rolled). SkyrimNet's AI actions offer themselves where this is true.
        bool Eligible(const std::string& id, std::size_t rule, ActorKey actor);

        // ── Triggers ─────────────────────────────────────────────────────────
        // `realDt` real seconds since the last tick (0 while paused), `gameHours` the game
        // clock, `loaded` the loaded actors worth considering (the player first).
        void Tick(double realDt, double gameHours, const std::vector<ActorKey>& loaded);
        void OnGameEvent(GameEvent event, const EventInfo& info);
        void OnModEvent(const std::string& name, const EventInfo& info);
        void OnKey(std::uint32_t scanCode, ActorKey crosshair);

        // ── Game sessions ────────────────────────────────────────────────────
        void ClearValues();        // before a load / new game: every dial back to its default
        void OnSessionStart();     // after a load / new game: publish every value, run Load rules
        std::vector<std::uint8_t> Save() const;
        bool Load(std::span<const std::uint8_t> data, const FormResolver& resolve);

        // ── Outputs ──────────────────────────────────────────────────────────
        std::vector<Change> TakeChanges();
        std::vector<Note> TakeNotes();
        std::vector<Send> TakeSends();
        std::size_t DroppedSends() const;  // sends refused since the last Tick (over kMaxSendsPerTick)
        bool HasChanges() const;
        std::vector<LogEntry> RecentLog(std::size_t max = 50) const;
        double Now() const;

        // The actors an actor dial currently runs rules for (tracked and loaded)
        std::vector<ActorKey> TrackedActors(const std::string& id) const;

        // How often the tracked set is re-checked (real seconds)
        void SetTrackInterval(double seconds) { trackInterval_ = seconds; }
        // Longest game-time gap a game-clock rule catches up on (sleep, wait, a long absence)
        void SetMaxCatchUpHours(double hours) { maxCatchUpHours_ = hours; }

    private:
        struct Cell {
            double raw = 0;            // unrounded
            double published = 0;      // last exposed value published
            bool publishedOnce = false;
            double lastGameHours = -1;  // for game-clock catch-up; < 0 = not ticked yet
        };

        struct RuleState {
            std::optional<bool> last;  // when: the condition's last result
            double lastFiredReal = -1e300;
            double lastFiredGame = -1e300;
        };

        struct CompiledRule {
            Handle condition = 0;  // when / while
            Handle onlyIf = 0;
            Handle otherIf = 0;
            Handle value = 0;  // the amount, when an expression
            double nextEvery = -1;
            std::unordered_map<ActorKey, RuleState> state;
        };

        struct Dial {
            DialDef def;
            Handle track = 0;
            std::vector<CompiledRule> rules;
            std::unordered_map<ActorKey, Cell> cells;  // global dials: key 0
            std::unordered_map<ActorKey, std::pair<bool, double>> trackCache;  // (tracked, checked at)
            std::vector<ActorKey> tracked;  // after the last tick
        };

        struct Pending {
            std::string id;
            ActorKey actor = 0;
            std::size_t rule = 0;
            bool restore = true;  // restore `amount` (set, toggle, cycle, reset) or subtract it (add)
            double amount = 0;
            Clock clock = Clock::Real;
            double due = 0;
        };

        void ReleaseHandles(Dial& dial);
        bool Compile(Dial& dial, std::vector<std::string>& errors);
        Dial* FindDial(const std::string& id);
        const Dial* FindDial(const std::string& id) const;

        ActorKey CellKey(const Dial& dial, ActorKey actor) const;
        Cell& CellFor(Dial& dial, ActorKey key);
        double RawValue(const Dial& dial, ActorKey key) const;

        // Sets a cell's raw value and records the change. `discrete`: an action, not a rate.
        void Write(Dial& dial, ActorKey key, double raw, const std::string& cause, bool discrete);

        bool IsTracked(Dial& dial, ActorKey actor);
        ActorKey ActorFor(const Rule& rule, const EventInfo& info) const;
        bool Allowed(Dial& dial, std::size_t index, ActorKey actor, const EventInfo* info, bool rollChance = true);
        void Apply(Dial& dial, std::size_t index, ActorKey key, const EventInfo* info);
        std::optional<double> AmountOf(const CompiledRule& compiled, const Rule& rule, ActorKey key);
        void RunEvent(Dial& dial, std::size_t index, const EventInfo& info);
        void ExpirePending();
        double ClockNow(Clock clock) const { return clock == Clock::Game ? gameHours_ : now_; }
        std::string CauseOf(const Dial& dial, std::size_t index) const;

        Host& host_;
        mutable std::mutex mutex_;
        std::vector<std::unique_ptr<Dial>> dials_;
        std::unordered_map<std::string, Dial*> byId_;  // lower-case id
        std::vector<Pending> pending_;
        // Saved values of dials nobody defined (yet): kept and saved again
        std::unordered_map<std::string, std::unordered_map<ActorKey, Cell>> orphans_;
        std::vector<Change> changes_;
        std::vector<Note> notes_;
        std::vector<Send> sends_;
        std::size_t sendsSinceTick_ = 0;    // sends queued since the last Tick, against kMaxSendsPerTick
        std::size_t droppedSinceTick_ = 0;  // sends refused since the last Tick (logged once per tick)
        std::deque<LogEntry> log_;
        double now_ = 0;        // real seconds the game has run (not paused)
        double gameHours_ = 0;  // the game clock at the last tick
        bool gameClockKnown_ = false;
        double trackInterval_ = 2.0;
        double maxCatchUpHours_ = 48.0;
        std::vector<ActorKey> loaded_;
    };

} // namespace ConditionEngine::Dials
