#pragma once

// Poll-based actor state conditions:
// Swimming, Sleeping, Mounting, InWater, LoveScene, Combat

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    // Swimming condition
    class SwimmingCondition : public Condition {
    public:
        explicit SwimmingCondition(bool shouldBeSwimming = true);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Swimming; }

    private:
        bool expectedState;
    };

    // Sleeping condition
    class SleepingCondition : public Condition {
    public:
        explicit SleepingCondition(bool shouldBeSleeping = true);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Sleeping; }

    private:
        bool expectedState;
    };

    // Mounting condition
    class MountingCondition : public Condition {
    public:
        explicit MountingCondition(bool shouldBeMounted = true);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Mounting; }

    private:
        bool expectedState;
    };

    // InWater condition
    class InWaterCondition : public Condition {
    public:
        explicit InWaterCondition(bool shouldBeInWater = true);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::InWater; }

    private:
        bool expectedState;
    };

    // LoveScene condition (OStim/SexLab)
    class LoveSceneCondition : public Condition {
    public:
        explicit LoveSceneCondition(bool shouldBeInLoveScene = true);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::LoveScene; }

    private:
        bool expectedState;
    };

    // Combat condition
    class CombatCondition : public Condition {
    public:
        explicit CombatCondition(bool shouldBeInCombat = true);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Combat; }

    private:
        bool expectedState;
    };

} // namespace ConditionEngine
