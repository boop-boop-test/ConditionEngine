#pragma once

// Watcher.h - Change notification for conditions.
//
// A watch re-evaluates its condition for one actor whenever a relevant game
// event fires (location, combat, equipment, quest, cell load) and on a
// periodic poll for state with no event (time, weather, actor values, actor
// states...). The callback runs on the main thread, only when the result
// differs from the previous one.

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

#include <chrono>
#include <mutex>

namespace ConditionEngine {

    using WatchId = std::uint32_t;  // 0 = invalid
    using WatchCallback = std::function<void(WatchId id, RE::Actor* actor, bool result)>;

    class Watcher {
    public:
        static Watcher& Get();

        // `actor` null = the player. With fireImmediately the callback also receives
        // the initial result (requires the game world to be loaded).
        WatchId Watch(std::shared_ptr<const Condition> condition, RE::Actor* actor, WatchCallback callback,
                      bool fireImmediately = false);
        void Unwatch(WatchId id);
        std::size_t GetWatchCount() const;

        // Re-evaluates watches whose condition depends on any of `events`.
        // `actor` null = every watched actor. Must run on the main thread.
        void NotifyEvent(EventDependency events, RE::Actor* actor = nullptr);

        // Re-evaluates every watch depending on poll-only state. Main thread.
        void Poll();

        // Registers game event sinks and starts the poll timer. Idempotent.
        // Call once the game data is loaded (SKSE kDataLoaded).
        void Install(std::chrono::milliseconds pollInterval = std::chrono::milliseconds(1000));

    private:
        Watcher() = default;

        struct Entry {
            std::shared_ptr<const Condition> condition;
            RE::FormID actorId = 0;  // 0 = player
            WatchCallback callback;
            EventDependency dependencies = EventDependency::None;
            std::optional<bool> lastResult;
        };

        void Reevaluate(EventDependency events, RE::FormID onlyActor);

        mutable std::mutex mutex_;
        std::unordered_map<WatchId, Entry> entries_;
        WatchId nextId_ = 1;
        bool installed_ = false;
    };

} // namespace ConditionEngine
