#pragma once

// Watcher.h - Change notification for conditions.
//
// Two tiers keep watches responsive without re-evaluating everything constantly:
//
//  1. Change detection (every detect interval, default 1 s): compares a cheap
//     snapshot - game minute/day, weather, and per watched actor its cell,
//     location, worldspace and state flags (combat, swimming, sneaking...).
//     Only watches depending on what actually changed are re-evaluated.
//     Game events (location change, combat, equip, inventory, magic effects,
//     cell load, sleep/wait/fast travel, menu close, load game) do the same.
//  2. Full poll (default every 10 s, per-watch override): re-evaluates each
//     watch regardless of dependencies, catching state with no change signal
//     (actor values, globals, script properties, vanilla Game(...) functions).
//
// Callbacks run on the main thread, only when the result differs from the
// previous one.
//
// Value watches (API v5) sample a numeric value (Value.h) at their own interval,
// down to one sample per frame, and on matching events. Watches of the same value
// definition and actor share one sample. ReadValueWatch returns the latest sample
// from any thread without touching game state.

#include "ConditionEngine/Availability.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"
#include "ConditionEngine/FirstMatchList.h"
#include "ConditionEngine/Value.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>

namespace ConditionEngine {

    using WatchId = std::uint32_t;  // 0 = invalid
    using WatchCallback = std::function<void(WatchId id, RE::Actor* actor, bool result)>;

    struct WatchOptions {
        // Full-poll interval for this watch. Zero = the global default; kNever = only
        // re-evaluate on change detection/events.
        static constexpr std::chrono::milliseconds kNever = std::chrono::milliseconds::max();
        std::chrono::milliseconds pollInterval{0};

        // Extra triggers on top of the condition's own dependencies, e.g. Location
        // to always re-check on cell transitions.
        EventDependency extraDependencies = EventDependency::None;

        // Also deliver the initial result (requires the game world to be loaded)
        bool fireImmediately = false;
    };

    struct ValueWatchOptions {
        // Sampling interval. Zero = the global poll interval; WatchOptions::kNever = only
        // on events. Intervals below a frame sample once per frame.
        std::chrono::milliseconds pollInterval{0};
        EventDependency extraDependencies = EventDependency::None;
        bool fireImmediately = false;
        // Callback when |value - last fired value| >= minStep. With thresholds and no
        // step, only crossings (and status changes) fire; with neither, every change does.
        double minStep = 0.0;
        std::vector<double> thresholds;
        // A crossed threshold re-arms only after the value moves this far back past it
        double hysteresis = 0.0;
    };

    struct ValueSample {
        using Clock = std::chrono::steady_clock;

        double value = 0.0;
        ValueStatus status = ValueStatus::Pending;
        std::string reason;
        std::string from;  // combined values: the provider that answered
        std::optional<Clock::time_point> sampledAt;  // nullopt = never sampled
        std::uint64_t availabilityRevision = 0;

        // Milliseconds since the sample was read, computed now; UINT32_MAX = never
        std::uint32_t AgeMs() const;
    };

    // List watches (issue 090): winner = index of the first true rule, -1 none
    using ListWatchCallback = std::function<void(WatchId id, RE::Actor* actor, int winner)>;

    // crossedIndex: index of the threshold crossed, or -1 for a step or status change
    using ValueWatchCallback =
        std::function<void(WatchId id, RE::Actor* actor, const ValueSample& sample, int crossedIndex)>;

    class Watcher {
    public:
        static Watcher& Get();

        // PostEvent actor argument meaning every watched actor
        static constexpr RE::FormID kAllActors = static_cast<RE::FormID>(-1);

        // `actor` null = the player.
        WatchId Watch(std::shared_ptr<const Condition> condition, RE::Actor* actor, WatchCallback callback,
                      const WatchOptions& options = {});
        WatchId Watch(std::shared_ptr<const Condition> condition, RE::Actor* actor, WatchCallback callback,
                      bool fireImmediately) {
            WatchOptions options;
            options.fireImmediately = fireImmediately;
            return Watch(std::move(condition), actor, std::move(callback), options);
        }
        void Unwatch(WatchId id);
        std::size_t GetWatchCount() const;
        bool IsWatchingActor(RE::Actor* actor) const;

        // Re-evaluates one watch now. Returns its current result, or nullopt when
        // the watch is unknown or the world is not loaded. Fires the callback on change.
        std::optional<bool> Refresh(WatchId id);

        // Re-evaluates watches whose condition depends on any of `events`.
        // `actor` null = every watched actor. Must run on the main thread.
        void NotifyEvent(EventDependency events, RE::Actor* actor = nullptr);

        // NotifyEvent for a keyed value change: wakes only what reads one of `keys`
        // ("$name", "call:<key>") among Value-dependent watches.
        void NotifyEvent(EventDependency events, RE::Actor* actor, const std::set<std::string>* keys);

        // Thread-safe NotifyEvent: queued for the main thread, and posts for the same
        // actor before the queue drains merge into one re-evaluation. `actorFormId`
        // is a real FormID (0x14 = player) or kAllActors.
        void PostEvent(EventDependency events, RE::FormID actorFormId);
        // Thread-safe keyed value change ("$hunger", "call:<key>") for an actor or kAllActors
        void PostValueChange(std::string key, RE::FormID actorFormId);

        // ── List watches ─────────────────────────────────────────────────────
        // Ordered rules for one actor; the callback fires when the first true rule (its
        // index, -1 none) changes. See FirstMatchList.h. `callback` may be null.
        WatchId WatchList(std::vector<std::shared_ptr<const Condition>> rules, RE::Actor* actor,
                          ListWatchCallback callback, const WatchOptions& options = {});
        // Any thread: the current winner, -1 none, -2 unknown watch
        int ReadListWatch(WatchId id) const;
        // Main thread: re-evaluates fully; the winner (-1 none), or -2 unknown / world not loaded
        int RefreshListWatch(WatchId id);
        // Any thread: {"winner", "rules": [{"state": "true|false|pending|unavailable|unread", "reason"}]}
        nlohmann::json ReadListWatchState(WatchId id) const;

        // ── Value watches ────────────────────────────────────────────────────
        // `actor` null = the player. `callback` may be null: the watch then only keeps
        // the sample fresh for ReadValueWatch. Unwatch() removes it.
        WatchId WatchValue(std::shared_ptr<const Value> value, RE::Actor* actor, ValueWatchCallback callback,
                           const ValueWatchOptions& options = {});
        // Latest sample. Any thread, never touches game state. False for unknown ids.
        bool ReadValueWatch(WatchId id, ValueSample& out) const;
        // Samples now (main thread). nullopt when the watch is unknown or the world is not loaded.
        std::optional<ValueSample> RefreshValue(WatchId id);
        std::size_t GetValueWatchCount() const;

        // Default full-poll interval (ConditionEngine.ini [Watcher] fPollIntervalSeconds)
        void SetPollInterval(std::chrono::milliseconds interval);
        std::chrono::milliseconds GetPollInterval() const;

        // One change-detection pass plus any due full polls and value samples. Main thread.
        void Tick();

        // Call before a game loads (SKSE kPreLoadGame / kNewGame). Watches on created
        // references (0xFF...) cannot follow their actor into another save - the same
        // FormID may name a different actor there - so they stop: condition watches are
        // no longer evaluated and value watches read Unavailable ("actor from a previous
        // game"). Unwatch and watch again after loading.
        void OnGameLoad();

        // Registers game event sinks and starts the detection timer. Idempotent.
        // Call once the game data is loaded (SKSE kDataLoaded).
        void Install(std::chrono::milliseconds detectInterval = std::chrono::milliseconds(1000));

    private:
        Watcher() = default;

        using Clock = std::chrono::steady_clock;

        struct Entry {
            std::shared_ptr<const Condition> condition;
            RE::FormID actorId = 0;  // 0 = player
            WatchCallback callback;
            EventDependency dependencies = EventDependency::None;
            EventDependency extraDependencies = EventDependency::None;  // the watch's own, kept on refresh
            std::set<std::string> keys;  // value keys the condition reads
            std::chrono::milliseconds pollInterval{0};
            Clock::time_point lastEvaluated{};
            std::optional<bool> lastResult;
            bool orphaned = false;  // created actor from before a game load: never evaluated again
        };

        struct ActorSnapshot {
            RE::TESObjectCELL* cell = nullptr;
            RE::BGSLocation* location = nullptr;
            RE::TESWorldSpace* worldspace = nullptr;
            std::uint32_t flags = 0;  // bit per tracked state, see Watcher.cpp
        };

        struct WorldSnapshot {
            std::int64_t gameMinute = -1;
            std::int64_t gameDay = -1;
            RE::TESWeather* weather = nullptr;
        };

        // Evaluates the entries `select` picks and fires callbacks for changed results
        void Evaluate(const std::function<bool(WatchId, const Entry&)>& select);

        // One value read shared by every watch of the same definition and actor
        struct Sampler {
            std::shared_ptr<const Value> value;
            std::string key;              // definition JSON + actor
            RE::FormID actorId = 0;       // 0 = player (global values always use 0)
            EventDependency dependencies = EventDependency::None;
            EventDependency extraDependencies = EventDependency::None;  // union of its watches' extras
            std::set<std::string> keys;  // value keys the value reads
            std::chrono::milliseconds interval{WatchOptions::kNever};  // shortest of its watches
            Clock::time_point lastSampled{};
            std::uint64_t checkedRevision = ~0ull;  // availability cache
            AvailabilityState availability = AvailabilityState::Available;
            std::string availabilityReason;
            std::size_t users = 0;

            mutable std::mutex sampleMutex;  // guards `sample` for cross-thread reads
            ValueSample sample;
        };

        struct ValueEntry {
            std::shared_ptr<Sampler> sampler;
            ValueWatchCallback callback;
            ValueWatchOptions options;
            std::chrono::milliseconds pollInterval{0};
            bool delivered = false;               // fireImmediately / first sample handled
            std::optional<double> lastFiredValue;
            ValueStatus lastStatus = ValueStatus::Pending;
            std::vector<bool> above;              // per threshold
        };

        struct ListEntry {
            std::unique_ptr<FirstMatchList> list;  // main thread only
            RE::FormID actorId = 0;
            ListWatchCallback callback;
            EventDependency extraDependencies = EventDependency::None;
            std::chrono::milliseconds pollInterval{0};
            Clock::time_point lastPolled{};
            bool orphaned = false;
            bool evaluated = false;
            std::atomic<int> winner{-1};  // published for ReadListWatch
            mutable std::mutex stateMutex;  // guards stateJson
            nlohmann::json stateJson;
        };
        // Evaluates the lists `select` picks and fires callbacks. select returns the mode (0 skip,
        // 1 incremental, 2 full = EvaluateAll) and the change bits for that list.
        void EvaluateLists(const std::function<std::pair<int, EventDependency>(const ListEntry&)>& select,
                           const std::set<std::string>* keys);

        // Samples the samplers `select` picks, then fires value callbacks
        void SampleValues(const std::function<bool(const Sampler&)>& select);
        // Shortest active value-watch interval (for the timer thread)
        std::chrono::milliseconds ShortestValueInterval() const;
        void DrainPosted();
        // Recomputes dependency masks when the availability revision moved (named values or
        // rules redefined), so existing watches follow what their definitions now read
        void RefreshDependencies();
        std::atomic<std::uint64_t> dependenciesRevision_{~0ull};

        mutable std::mutex mutex_;
        std::unordered_map<WatchId, Entry> entries_;
        WatchId nextId_ = 1;
        bool installed_ = false;
        std::chrono::milliseconds pollInterval_{10000};

        // Touched only on the main thread (Tick)
        WorldSnapshot world_;
        std::unordered_map<RE::FormID, ActorSnapshot> actors_;
        Clock::time_point lastDetect_{};
        std::chrono::milliseconds detectInterval_{1000};

        // Value watches: lookups from any thread take valueMutex_ shared
        mutable std::shared_mutex valueMutex_;
        std::unordered_map<WatchId, ValueEntry> valueEntries_;
        std::unordered_map<std::string, std::shared_ptr<Sampler>> samplers_;
        std::atomic<std::int64_t> shortestValueIntervalMs_{0};  // 0 = no value watches

        // List watches: lookups from any thread take listMutex_ shared
        mutable std::shared_mutex listMutex_;
        std::unordered_map<WatchId, std::shared_ptr<ListEntry>> lists_;

        // PostEvent queue
        struct Posted {
            EventDependency events = EventDependency::None;  // unkeyed bits (an unkeyed Value wakes all readers)
            std::set<std::string> valueKeys;                  // keyed value changes
        };
        std::mutex postMutex_;
        std::unordered_map<RE::FormID, Posted> posted_;
        bool postQueued_ = false;
        std::atomic<bool> tickQueued_{false};
    };

} // namespace ConditionEngine
