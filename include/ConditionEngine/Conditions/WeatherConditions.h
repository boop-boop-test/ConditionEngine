#pragma once

// Weather condition

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    // Weather-based condition
    class WeatherCondition : public Condition {
    public:
        WeatherCondition(std::uint32_t weatherFormId, ComparisonOperator op = ComparisonOperator::Equal);
        WeatherCondition(const std::string& weatherName, ComparisonOperator op = ComparisonOperator::Equal);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Weather; }

    private:
        std::uint32_t weatherFormId;
        std::string weatherName;
        ComparisonOperator operation;
    };

} // namespace ConditionEngine
