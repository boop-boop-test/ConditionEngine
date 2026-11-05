#pragma once

// Weather conditions: Weather (classification, kind or editor ID) and WeatherKind (a 0-100 level)

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"
#include "ConditionEngine/Weather/WeatherProfile.h"

namespace ConditionEngine {

    // Weather(Rainy): the classification flags (Clear / Cloudy / Rainy / Snowy), a kind from the weather
    // profile (Stormy, Thunderstorm, Blizzard, Foggy, Windy, Ashfall, Sandstorm, Aurora), or an editor ID
    // (glob allowed: "TS_*_Heavy"), or a form ID.
    class WeatherCondition : public Condition {
    public:
        WeatherCondition(std::uint32_t weatherFormId, ComparisonOperator op = ComparisonOperator::Equal);
        WeatherCondition(const std::string& weatherName, ComparisonOperator op = ComparisonOperator::Equal);

        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Weather; }

        // The profile kind and minimum level a Weather(...) name stands for ("Foggy" -> Fog, 50)
        static std::optional<std::pair<Weather::Kind, int>> ProfileTest(std::string_view name);

    private:
        std::uint32_t weatherFormId;
        std::string weatherName;
        ComparisonOperator operation;
    };

    // WeatherKind(Rain) reads the current weather's level of one kind, 0-100 (25 / 50 / 75 = light /
    // moderate / heavy). As a condition it's true from minLevel (default >= 25): WeatherKind(Rain) >= 75.
    class WeatherKindCondition : public Condition {
    public:
        WeatherKindCondition(Weather::Kind kind, float minLevel = 25.0f,
                             ComparisonOperator op = ComparisonOperator::GreaterEqual);

        bool Evaluate(const EvaluationContext& context) const override;
        ValueResult ReadValue(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Weather; }

    private:
        Weather::Kind kind;
        float minLevel;
        ComparisonOperator operation;
    };

} // namespace ConditionEngine
