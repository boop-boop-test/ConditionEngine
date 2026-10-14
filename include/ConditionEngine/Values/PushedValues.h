#pragma once

// PushedValues.h - Values set from outside (Papyrus SetValue, the C API, SkyrimNet
// actions) and read back through $name.
//
// - Per actor or global. A global push also answers for actors without their own.
// - Optional timeout (real seconds); afterwards the value reverts to the named
//   value's expression, its pushed default, or Unavailable.
// - Never saved: every game load starts clean (ClearAll on load), actions push again.
// - Pushing a name nobody declared registers an implicit pushed value, so a script
//   needs no preset file.
// - Each push fires EventDependency::Value for that actor (all actors for a global
//   push) so watches react immediately.

#include "ConditionEngine/Common.h"
#include "ConditionEngine/ValueResult.h"

#include <atomic>
#include <mutex>

namespace ConditionEngine {

    struct NamedValueDef;

    class PushedValues {
    public:
        static PushedValues& Get();

        // `actor` null = the player. timeoutSeconds <= 0 uses the named value's
        // default timeout (none when it has no default). Returns false for an invalid name.
        bool Set(RE::Actor* actor, const std::string& name, double value, double timeoutSeconds = 0.0);
        bool SetGlobal(const std::string& name, double value, double timeoutSeconds = 0.0);
        bool Clear(RE::Actor* actor, const std::string& name);
        bool ClearGlobal(const std::string& name);
        void ClearAll();

        // Drops expired pushes and fires EventDependency::Value for their actors.
        // Called from the watcher tick.
        void ExpireDue();

        // The active push for this value and actor (`actor` null = the player), or the
        // pushed default of a pushed-only value. nullopt = nothing pushed, read the
        // value's expression instead.
        std::optional<ValueResult> Read(const NamedValueDef& def, RE::Actor* actor) const;

        // Why Read found nothing for this actor: expired, cleared, pushed only for other
        // actors, or never pushed
        std::string WhyMissing(const NamedValueDef& def, RE::Actor* actor) const;

        // Test hook: key used for an actor (0 = global, 0x14 = player)
        static RE::FormID ActorKey(RE::Actor* actor);

    private:
        PushedValues() = default;

        struct Entry {
            double value = 0.0;
            ValueResult::Clock::time_point setAt;
            std::optional<ValueResult::Clock::time_point> expiresAt;
        };

        bool Store(RE::FormID key, const std::string& name, double value, double timeoutSeconds);
        bool Remove(RE::FormID key, const std::string& name);

        enum class Ended { Expired, Cleared };

        mutable std::mutex mutex_;
        // (actor key, lowercase name)
        std::map<std::pair<RE::FormID, std::string>, Entry> entries_;
        std::atomic<std::size_t> count_{0};  // entries_.size(), read without the lock
        // Pushes that ended, for WhyMissing; forgotten when pushed again or on ClearAll
        std::map<std::pair<RE::FormID, std::string>, Ended> ended_;
    };

} // namespace ConditionEngine
