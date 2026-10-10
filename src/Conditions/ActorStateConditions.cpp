#include "ConditionEngine/Conditions/ActorStateConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"

namespace ConditionEngine {

    // ============================================================================
    // SwimmingCondition
    // ============================================================================

    SwimmingCondition::SwimmingCondition(bool shouldBeSwimming) : expectedState(shouldBeSwimming) {}

    bool SwimmingCondition::Evaluate(const EvaluationContext& context) const {
        return context.IsSwimming() == expectedState;
    }

    std::string SwimmingCondition::GetDescription() const {
        return expectedState ? "Actor is swimming" : "Actor is not swimming";
    }

    nlohmann::json SwimmingCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "Swimming";
        json["expectedState"] = expectedState;
        return json;
    }

    std::unique_ptr<Condition> SwimmingCondition::Clone() const {
        return std::make_unique<SwimmingCondition>(expectedState);
    }

    // ============================================================================
    // SleepingCondition
    // ============================================================================

    SleepingCondition::SleepingCondition(bool shouldBeSleeping) : expectedState(shouldBeSleeping) {}

    bool SleepingCondition::Evaluate(const EvaluationContext& context) const {
        return context.IsSleeping() == expectedState;
    }

    std::string SleepingCondition::GetDescription() const {
        return expectedState ? "Actor is sleeping" : "Actor is not sleeping";
    }

    nlohmann::json SleepingCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "Sleeping";
        json["expectedState"] = expectedState;
        return json;
    }

    std::unique_ptr<Condition> SleepingCondition::Clone() const {
        return std::make_unique<SleepingCondition>(expectedState);
    }

    // ============================================================================
    // MountingCondition
    // ============================================================================

    MountingCondition::MountingCondition(bool shouldBeMounted) : expectedState(shouldBeMounted) {}

    bool MountingCondition::Evaluate(const EvaluationContext& context) const {
        return context.IsMounted() == expectedState;
    }

    std::string MountingCondition::GetDescription() const {
        return expectedState ? "Actor is mounted" : "Actor is not mounted";
    }

    nlohmann::json MountingCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "Mounting";
        json["expectedState"] = expectedState;
        return json;
    }

    std::unique_ptr<Condition> MountingCondition::Clone() const {
        return std::make_unique<MountingCondition>(expectedState);
    }

    // ============================================================================
    // InWaterCondition
    // ============================================================================

    InWaterCondition::InWaterCondition(bool shouldBeInWater) : expectedState(shouldBeInWater) {}

    bool InWaterCondition::Evaluate(const EvaluationContext& context) const {
        return context.IsInWater() == expectedState;
    }

    std::string InWaterCondition::GetDescription() const {
        return expectedState ? "Actor is in water" : "Actor is not in water";
    }

    nlohmann::json InWaterCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "InWater";
        json["expectedState"] = expectedState;
        return json;
    }

    std::unique_ptr<Condition> InWaterCondition::Clone() const {
        return std::make_unique<InWaterCondition>(expectedState);
    }

    // ============================================================================
    // LoveSceneCondition
    // ============================================================================

    LoveSceneCondition::LoveSceneCondition(bool shouldBeInLoveScene) : expectedState(shouldBeInLoveScene) {}

    bool LoveSceneCondition::Evaluate(const EvaluationContext& context) const {
        return context.IsInLoveScene() == expectedState;
    }

    std::string LoveSceneCondition::GetDescription() const {
        return expectedState ? "Actor is in love scene" : "Actor is not in love scene";
    }

    nlohmann::json LoveSceneCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "LoveScene";
        json["expectedState"] = expectedState;
        return json;
    }

    std::unique_ptr<Condition> LoveSceneCondition::Clone() const {
        return std::make_unique<LoveSceneCondition>(expectedState);
    }

    // ============================================================================
    // CombatCondition
    // ============================================================================

    CombatCondition::CombatCondition(bool shouldBeInCombat) : expectedState(shouldBeInCombat) {}

    bool CombatCondition::Evaluate(const EvaluationContext& context) const {
        return context.IsInCombat() == expectedState;
    }

    std::string CombatCondition::GetDescription() const {
        return expectedState ? "Actor is in combat" : "Actor is not in combat";
    }

    nlohmann::json CombatCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "Combat";
        json["expectedState"] = expectedState;
        return json;
    }

    std::unique_ptr<Condition> CombatCondition::Clone() const {
        return std::make_unique<CombatCondition>(expectedState);
    }

} // namespace ConditionEngine
