#pragma once

// Actor value condition

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    // Actor value condition (health, magicka, stamina, skills, etc.)
    class ActorValueCondition : public Condition {
    public:
        ActorValueCondition(RE::ActorValue av, float value, ComparisonOperator op, bool usePercent = false);
        ActorValueCondition(const std::string& avName, float value, ComparisonOperator op, bool usePercent = false);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::ActorValue; }

    private:
        RE::ActorValue actorValue;
        std::string actorValueName;
        float targetValue;
        ComparisonOperator operation;
        bool usePercentage;
    };

} // namespace ConditionEngine
