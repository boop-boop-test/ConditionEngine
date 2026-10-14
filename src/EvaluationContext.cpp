#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Registry.h"

namespace ConditionEngine {

    EvaluationContext::EvaluationContext(RE::Actor* actor)
        : actor(actor) {}

    void EvaluationContext::UpdateFromGameState() {
        taken = 0;
    }

    void EvaluationContext::TakeSnapshot(SnapshotGroup group) const {
        taken |= group;
        if (!actor) return;  // an actor-less context reads nothing (as before)
        switch (group) {
            case kTime:
                // Time information (doesn't require the actor)
                if (auto* calendar = RE::Calendar::GetSingleton()) {
                    currentTime = calendar->GetHour();
                    currentDay = calendar->GetDay();
                    currentMonth = calendar->GetMonth();
                }
                break;
            case kLocation:
                // The actor's location, falling back to the cell's: some cells (especially
                // interiors) do not set the actor's location directly
                if (!actor) break;
                currentLocation = actor->GetCurrentLocation();
                if (!currentLocation) {
                    if (auto* cell = actor->GetParentCell()) currentLocation = cell->GetLocation();
                }
                break;
            case kWeather:
                if (auto* sky = RE::Sky::GetSingleton(); sky && sky->currentWeather) currentWeather = sky->currentWeather;
                break;
            case kTemperature:
                if (actor) temperature = Registry::Get().QueryTemperature(actor);
                break;
            case kActorState:
                if (!actor) break;
                if (auto* actorState = actor->AsActorState()) {
                    isSwimming = actorState->IsSwimming();
                } else {
                    isSwimming = false;
                }
                isSleeping = IsActorSleeping(actor);
                isMounted = actor->IsOnMount();
                // In water is different from swimming - an actor can be in water but not swimming
                isInWater = actor->IsInWater();
                isInCombat = actor->IsInCombat();
                break;
            case kLoveScene:
                if (actor) isInLoveScene = Registry::Get().QueryLoveScene(actor);
                break;
        }
    }

    bool IsActorSleeping(RE::Actor* actor) {
        if (!actor) return false;
        auto* actorState = actor->AsActorState();
        if (!actorState) return false;
        auto sleepState = actorState->GetSitSleepState();
        return sleepState >= RE::SIT_SLEEP_STATE::kIsSleeping && sleepState <= RE::SIT_SLEEP_STATE::kWantToWake;
    }

} // namespace ConditionEngine
