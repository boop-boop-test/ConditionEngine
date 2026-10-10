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

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

#include <chrono>
#include <mutex>

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

    class Watcher {
    public:
        static Watcher& Get();

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

        // Default full-poll interval (ConditionEngine.ini [Watcher] fPollIntervalSeconds)
        void SetPollInterval(std::chrono::milliseconds interval);
        std::chrono::milliseconds GetPollInterval() const;

        // One change-detection pass plus any due full polls. Main thread.
        void Tick();

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
            std::chrono::milliseconds pollInterval{0};
            Clock::time_point lastEvaluated{};
            std::optional<bool> lastResult;
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

        mutable std::mutex mutex_;
        std::unordered_map<WatchId, Entry> entries_;
        WatchId nextId_ = 1;
        bool installed_ = false;
        std::chrono::milliseconds pollInterval_{10000};

        // Touched only on the main thread (Tick)
        WorldSnapshot world_;
        std::unordered_map<RE::FormID, ActorSnapshot> actors_;
    };

} // namespace ConditionEngine
