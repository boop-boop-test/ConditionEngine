#pragma once

// Temperature condition (survival mods)

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    // Temperature condition - for survival mod integration
    class TemperatureCondition : public Condition {
    public:
        TemperatureCondition(float temp, ComparisonOperator op);
        
        bool Evaluate(const EvaluationContext& context) const override;
        ValueResult ReadValue(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Temperature; }

    private:
        float targetTemperature;
        ComparisonOperator operation;
    };

} // namespace ConditionEngine
