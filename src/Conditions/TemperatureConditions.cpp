#include "ConditionEngine/Conditions/TemperatureConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"

namespace ConditionEngine {

    TemperatureCondition::TemperatureCondition(float temp, ComparisonOperator op)
        : targetTemperature(temp), operation(op) {}

    ValueResult TemperatureCondition::ReadValue(const EvaluationContext& context) const {
        return ValueResult::Of(context.GetTemperature());
    }

    bool TemperatureCondition::Evaluate(const EvaluationContext& context) const {
        float currentTemp = context.GetTemperature();
        return CompareFloat(currentTemp, targetTemperature, operation);
    }

    std::string TemperatureCondition::GetDescription() const {
        return "Temperature " + ComparisonOpToString(operation) + " " + std::to_string(targetTemperature);
    }

    nlohmann::json TemperatureCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "Temperature";
        json["targetTemperature"] = targetTemperature;
        json["operator"] = ComparisonOpToName(operation);
        std::string range = "Comfortable";
        if (targetTemperature < 0.1f) range = "Freezing";
        else if (targetTemperature < 40.1f) range = "Cold";
        else if (targetTemperature < 70.0f) range = "Comfortable";
        else if (targetTemperature < 90.0f) range = "Warm";
        else range = "Hot";
        json["temperatureRange"] = range;
        return json;
    }

    std::unique_ptr<Condition> TemperatureCondition::Clone() const {
        return std::make_unique<TemperatureCondition>(targetTemperature, operation);
    }

} // namespace ConditionEngine
