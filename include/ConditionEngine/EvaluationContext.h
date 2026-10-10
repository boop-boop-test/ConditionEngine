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

        // Re-snapshot game state for the current actor
        void UpdateFromGameState();

        RE::Actor* GetActor() const { return actor; }
        RE::BGSLocation* GetCurrentLocation() const { return currentLocation; }
        RE::TESWeather* GetCurrentWeather() const { return currentWeather; }
        float GetCurrentTime() const { return currentTime; }
        int GetCurrentDay() const { return currentDay; }
        int GetCurrentMonth() const { return currentMonth; }
        float GetTemperature() const { return temperature; }

        // Poll-based actor state getters
        bool IsSwimming() const { return isSwimming; }
        bool IsSleeping() const { return isSleeping; }
        bool IsMounted() const { return isMounted; }
        bool IsInWater() const { return isInWater; }
        bool IsInLoveScene() const { return isInLoveScene; }
        bool IsInCombat() const { return isInCombat; }

        // Cache for expensive lookups
        mutable std::unordered_map<std::string, std::variant<bool, int, float, std::string>> cache;

        // Named-condition (Rule reference) memoization and cycle detection
        mutable std::unordered_map<std::string, bool> ruleMemo;
        mutable std::unordered_set<std::string> rulesInProgress;

        // Opaque pointer forwarded to C-API custom condition callbacks.
        void* userData = nullptr;

    private:
        RE::Actor* actor = nullptr;
        RE::BGSLocation* currentLocation = nullptr;
        RE::TESWeather* currentWeather = nullptr;
        float currentTime = 0.0f;
        int currentDay = 0;
        int currentMonth = 0;
        float temperature = 20.0f;

        bool isSwimming = false;
        bool isSleeping = false;
        bool isMounted = false;
        bool isInWater = false;
        bool isInLoveScene = false;
        bool isInCombat = false;
    };

    // Returns true when the actor is in a sit/sleep state between "sleeping" and "want to wake"
    bool IsActorSleeping(RE::Actor* actor);

} // namespace ConditionEngine
