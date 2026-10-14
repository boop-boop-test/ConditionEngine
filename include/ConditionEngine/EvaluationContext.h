#pragma once

// EvaluationContext.h - Snapshot of game state that conditions evaluate against.
//
// Hosts may derive from this class to carry extra per-evaluation state for
// their own condition types (e.g. DressCode's pending equipment state); such
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

        // Re-snapshot game state for the current actor. The snapshot is taken lazily, on
        // the first getter call, so contexts that only read values stay cheap.
        void UpdateFromGameState();

        RE::Actor* GetActor() const { return actor; }
        RE::BGSLocation* GetCurrentLocation() const { Snapshot(); return currentLocation; }
        RE::TESWeather* GetCurrentWeather() const { Snapshot(); return currentWeather; }
        float GetCurrentTime() const { Snapshot(); return currentTime; }
        int GetCurrentDay() const { Snapshot(); return currentDay; }
        int GetCurrentMonth() const { Snapshot(); return currentMonth; }
        float GetTemperature() const { Snapshot(); return temperature; }

        // Poll-based actor state getters
        bool IsSwimming() const { Snapshot(); return isSwimming; }
        bool IsSleeping() const { Snapshot(); return isSleeping; }
        bool IsMounted() const { Snapshot(); return isMounted; }
        bool IsInWater() const { Snapshot(); return isInWater; }
        bool IsInLoveScene() const { Snapshot(); return isInLoveScene; }
        bool IsInCombat() const { Snapshot(); return isInCombat; }

        // Cache for expensive lookups
        mutable std::unordered_map<std::string, std::variant<bool, int, float, std::string>> cache;

        // Named-condition (Rule reference) memoization and cycle detection
        mutable std::unordered_map<std::string, bool> ruleMemo;
        mutable std::unordered_set<std::string> rulesInProgress;

        // Named-value ($name) cycle detection, lowercase names
        mutable std::unordered_set<std::string> valuesInProgress;

        // Opaque pointer forwarded to C-API custom condition callbacks.
        void* userData = nullptr;

    private:
        void Snapshot() const {
            if (!snapshotted) TakeSnapshot();
        }
        void TakeSnapshot() const;

        RE::Actor* actor = nullptr;
        mutable bool snapshotted = false;
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
