#pragma once

#include "ConditionEngine/Condition.h"
#include <functional>
#include <mutex>
#include <unordered_set>

namespace ConditionEngine {
    struct TypeInfo;

    enum class AvailabilityState : std::int32_t { Pending = 0, Available = 1, Unavailable = 2 };

    struct AvailabilityStatus {
        AvailabilityState state = AvailabilityState::Available;
        std::string reason;
        bool Ready() const { return state == AvailabilityState::Available; }
        nlohmann::json ToJson() const;
        bool operator==(const AvailabilityStatus&) const = default;
    };

    // Snapshot reads never touch game state. Publishers notify subscribers outside the lock,
    // on the publishing thread; callbacks must be thread-safe and may query/re-enter this API.
    class Availability {
    public:
        static Availability& Get();
        AvailabilityStatus Check(const Condition& condition) const;
        AvailabilityStatus CheckType(const TypeInfo& type) const;
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
        void Publish();
        mutable std::mutex mutex_;
        bool pluginsReady_ = false;
        std::unordered_set<std::string> plugins_;
        std::unordered_map<std::string, AvailabilityStatus> types_;
        std::unordered_map<std::uint32_t, ChangedFn> subscribers_;
        std::uint32_t nextSubscription_ = 1;
        std::uint64_t revision_ = 0;
    };
}
