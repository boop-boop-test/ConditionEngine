#include "ConditionEngine/Conditions/WeatherConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"

namespace ConditionEngine {

    WeatherCondition::WeatherCondition(std::uint32_t weatherFormId, ComparisonOperator op)
        : weatherFormId(weatherFormId), operation(op) {}

    WeatherCondition::WeatherCondition(const std::string& weatherName, ComparisonOperator op)
        : weatherFormId(0), weatherName(weatherName), operation(op) {}

    bool WeatherCondition::Evaluate(const EvaluationContext& context) const {
        auto currentWeather = context.GetCurrentWeather();
        if (!currentWeather) return false;

        bool matches = false;
        if (weatherFormId != 0) {
            matches = (currentWeather->GetFormID() == weatherFormId);
        } else if (!weatherName.empty()) {
            // Match weather category strings against TESWeather classification flags
            auto flags = currentWeather->data.flags;
            if (weatherName == "Clear" || weatherName == "Pleasant")
                matches = flags.any(RE::TESWeather::WeatherDataFlag::kPleasant);
            else if (weatherName == "Cloudy")
                matches = flags.any(RE::TESWeather::WeatherDataFlag::kCloudy);
            else if (weatherName == "Rainy" || weatherName == "Rain")
                matches = flags.any(RE::TESWeather::WeatherDataFlag::kRainy);
            else if (weatherName == "Snowy" || weatherName == "Snow")
                matches = flags.any(RE::TESWeather::WeatherDataFlag::kSnow);
            else
                matches = (currentWeather->GetName() && std::string(currentWeather->GetName()) == weatherName);
        }

        switch (operation) {
            case ComparisonOperator::Equal: return matches;
            case ComparisonOperator::NotEqual: return !matches;
            default: return matches;
        }
    }

    std::string WeatherCondition::GetDescription() const {
        if (!weatherName.empty()) {
            return "Weather: " + weatherName;
        } else {
            return "Weather ID: " + std::to_string(weatherFormId);
        }
    }

    nlohmann::json WeatherCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "Weather";
        json["weatherType"] = weatherName;  // FromJson reads "weatherType" first, "weatherName" as legacy fallback
        json["operator"] = ComparisonOpToName(operation);
        return json;
    }

    std::unique_ptr<Condition> WeatherCondition::Clone() const {
        if (!weatherName.empty()) {
            return std::make_unique<WeatherCondition>(weatherName, operation);
        } else {
            return std::make_unique<WeatherCondition>(weatherFormId, operation);
        }
    }

} // namespace ConditionEngine
