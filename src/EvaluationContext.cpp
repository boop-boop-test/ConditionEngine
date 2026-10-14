#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Registry.h"

namespace ConditionEngine {

    EvaluationContext::EvaluationContext(RE::Actor* actor)
        : actor(actor) {}

    void EvaluationContext::UpdateFromGameState() {
        snapshotted = false;
        cache.clear();
    }

    void EvaluationContext::TakeSnapshot() const {
        snapshotted = true;
        if (!actor) return;

        // Time information (doesn't require player)
        auto calendar = RE::Calendar::GetSingleton();
        if (calendar) {
            currentTime = calendar->GetHour();
            currentDay = calendar->GetDay();
            currentMonth = calendar->GetMonth();
        }

        // Location - use the actor's location, fall back to the cell's location.
        // Some cells (especially interiors) may not set the actor's location directly,
        // but the cell itself has a location assigned.
        currentLocation = actor->GetCurrentLocation();
        if (!currentLocation) {
            if (auto* cell = actor->GetParentCell()) {
                currentLocation = cell->GetLocation();
            }
        }

        auto sky = RE::Sky::GetSingleton();
        if (sky && sky->currentWeather) {
            currentWeather = sky->currentWeather;
        }

        auto& registry = Registry::Get();
        temperature = registry.QueryTemperature(actor);

        // Poll-based actor states
        if (auto actorState = actor->AsActorState()) {
            isSwimming = actorState->IsSwimming();
        } else {
            isSwimming = false;
        }
        isSleeping = IsActorSleeping(actor);
        isMounted = actor->IsOnMount();
        // In water is different from swimming - an actor can be in water but not swimming
        isInWater = actor->IsInWater();
        isInCombat = actor->IsInCombat();
        isInLoveScene = registry.QueryLoveScene(actor);
    }

    bool IsActorSleeping(RE::Actor* actor) {
        if (!actor) return false;
        auto* actorState = actor->AsActorState();
        if (!actorState) return false;
        auto sleepState = actorState->GetSitSleepState();
        return sleepState >= RE::SIT_SLEEP_STATE::kIsSleeping && sleepState <= RE::SIT_SLEEP_STATE::kWantToWake;
    }

} // namespace ConditionEngine
