#pragma once

// EvaluationContext.h - Snapshot of game state that conditions evaluate against.
//
// Hosts may derive from this class to carry extra per-evaluation state for
// their own condition types (e.g. a host's pending equipment state); such
// conditions dynamic_cast the context and fall back gracefully when it is a
// plain EvaluationContext.

#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    class EvaluationContext {
    public:
        explicit EvaluationContext(RE::Actor* actor = nullptr);
        virtual ~EvaluationContext() = default;

        EvaluationContext(const EvaluationContext&) = default;
        EvaluationContext& operator=(const EvaluationContext&) = default;

        // Re-snapshot game state for the current actor. The snapshot is taken lazily and in
        // groups (time, location, weather, temperature, actor state, love scene), each on its
        // first getter call, so a context that only reads the hour never queries the rest.
        void UpdateFromGameState();

        RE::Actor* GetActor() const { return actor; }
        RE::BGSLocation* GetCurrentLocation() const { Snapshot(kLocation); return currentLocation; }
        RE::TESWeather* GetCurrentWeather() const { Snapshot(kWeather); return currentWeather; }
        float GetCurrentTime() const { Snapshot(kTime); return currentTime; }
        int GetCurrentDay() const { Snapshot(kTime); return currentDay; }
        int GetCurrentMonth() const { Snapshot(kTime); return currentMonth; }
        float GetTemperature() const { Snapshot(kTemperature); return temperature; }

        // Poll-based actor state getters
        bool IsSwimming() const { Snapshot(kActorState); return isSwimming; }
        bool IsSleeping() const { Snapshot(kActorState); return isSleeping; }
        bool IsMounted() const { Snapshot(kActorState); return isMounted; }
        bool IsInWater() const { Snapshot(kActorState); return isInWater; }
        bool IsInLoveScene() const { Snapshot(kLoveScene); return isInLoveScene; }
        bool IsInCombat() const { Snapshot(kActorState); return isInCombat; }

        // Named-condition (Rule reference) memoization and cycle detection
        mutable std::unordered_map<std::string, bool> ruleMemo;
        mutable std::unordered_set<std::string> rulesInProgress;

        // Named-value ($name) cycle detection: definitions being read
        mutable std::vector<const void*> valuesInProgress;

        // Opaque pointer forwarded to C-API custom condition callbacks.
        void* userData = nullptr;

    private:
        enum SnapshotGroup : std::uint8_t {
            kTime = 1 << 0,
            kLocation = 1 << 1,
            kWeather = 1 << 2,
            kTemperature = 1 << 3,
            kActorState = 1 << 4,
            kLoveScene = 1 << 5,
        };
        void Snapshot(SnapshotGroup group) const {
            if (!(taken & group)) TakeSnapshot(group);
        }
        void TakeSnapshot(SnapshotGroup group) const;

        RE::Actor* actor = nullptr;
        mutable std::uint8_t taken = 0;  // SnapshotGroup bits already read
        mutable RE::BGSLocation* currentLocation = nullptr;
        mutable RE::TESWeather* currentWeather = nullptr;
        mutable float currentTime = 0.0f;
        mutable int currentDay = 0;
        mutable int currentMonth = 0;
        mutable float temperature = 20.0f;

        mutable bool isSwimming = false;
        mutable bool isSleeping = false;
        mutable bool isMounted = false;
        mutable bool isInWater = false;
        mutable bool isInLoveScene = false;
        mutable bool isInCombat = false;
    };

    // Returns true when the actor is in a sit/sleep state between "sleeping" and "want to wake"
    bool IsActorSleeping(RE::Actor* actor);

} // namespace ConditionEngine
