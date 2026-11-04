#pragma once

#include "ConditionEngine/Condition.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_set>

namespace ConditionEngine {
    struct TypeInfo;
    class Value;

    enum class AvailabilityState : std::int32_t { Pending = 0, Available = 1, Unavailable = 2 };

    struct AvailabilityStatus {
        AvailabilityState state = AvailabilityState::Available;
        std::string reason;
        bool Ready() const { return state == AvailabilityState::Available; }
        nlohmann::json ToJson() const;
        bool operator==(const AvailabilityStatus&) const = default;
    };

    namespace detail {
        // Per-object availability cache, owned by Condition / Value (issue 088 step 2)
        struct AvailabilityMemo {
            std::mutex mutex;
            std::uint64_t revision = ~0ull;  // never matches until computed
            AvailabilityStatus status;
        };

        // The memo in `slot` (created on first use), or `compute()` when its revision is stale
        AvailabilityStatus CachedCheck(std::atomic<AvailabilityMemo*>& slot,
                                       const std::function<AvailabilityStatus()>& compute);
    }

    // Snapshot reads never touch game state. Publishers notify subscribers outside the lock,
    // on the publishing thread; callbacks must be thread-safe and may query/re-enter this API.
    class Availability {
    public:
        static Availability& Get();
        AvailabilityStatus Check(const Condition& condition) const;
        // Value expressions: numeric types, $name references (their required plugins,
        // enabledWhen and definition) and conditions read as 0 / 1
        AvailabilityStatus CheckValue(const Value& value) const;
        AvailabilityStatus CheckType(const TypeInfo& type) const;
        // Required plugins (loaded, full or light) and DLLs (loaded modules)
        AvailabilityStatus CheckRequirements(const std::vector<std::string>& plugins,
                                             const std::vector<std::string>& dlls) const;
        bool SetTypeStatus(const std::string& type, AvailabilityStatus status);
        void SetLoadedPlugins(const std::vector<std::string>& plugins);
        // Main thread, after game data loads. Includes full and light plugins.
        void RefreshLoadedPlugins();
        // Host resolvers can invalidate readiness when their named definitions change.
        void Invalidate() { Publish(); }
        std::uint64_t Revision() const;
        using ChangedFn = std::function<void(std::uint64_t revision)>;
        std::uint32_t Subscribe(ChangedFn fn);
        void Unsubscribe(std::uint32_t subscription);
    private:
        AvailabilityStatus PluginStatus(const std::string& plugin) const;
        AvailabilityStatus CheckNode(const nlohmann::json& node, std::unordered_set<std::string>& names,
                                     std::size_t depth) const;
        AvailabilityStatus CheckValueNode(const nlohmann::json& node, std::unordered_set<std::string>& names,
                                          std::size_t depth) const;
        void Publish();
        mutable std::mutex mutex_;
        bool pluginsReady_ = false;
        std::unordered_set<std::string> plugins_;
        std::unordered_map<std::string, AvailabilityStatus> types_;
        std::unordered_map<std::uint32_t, ChangedFn> subscribers_;
        std::uint32_t nextSubscription_ = 1;
        std::atomic<std::uint64_t> revision_{0};
    };
}
