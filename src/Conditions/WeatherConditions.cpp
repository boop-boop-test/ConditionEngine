#include "ConditionEngine/Conditions/WeatherConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Weather/WeatherProfiles.h"

namespace ConditionEngine {

    namespace {
        bool IEquals(std::string_view a, std::string_view b) {
            return a.size() == b.size() && _strnicmp(a.data(), b.data(), a.size()) == 0;
        }
    }

    // ============================================================================
    // WeatherCondition
    // ============================================================================

    WeatherCondition::WeatherCondition(std::uint32_t weatherFormId, ComparisonOperator op)
        : weatherFormId(weatherFormId), operation(op) {}

    WeatherCondition::WeatherCondition(const std::string& weatherName, ComparisonOperator op)
        : weatherFormId(0), weatherName(weatherName), operation(op) {}

    std::optional<std::pair<Weather::Kind, int>> WeatherCondition::ProfileTest(std::string_view name) {
        using Weather::Kind;
        static const std::pair<std::string_view, std::pair<Kind, int>> kTests[] = {
            {"Stormy", {Kind::Storm, 25}},       {"Storm", {Kind::Storm, 25}},
            {"Thunderstorm", {Kind::Thunder, 25}}, {"Thunder", {Kind::Thunder, 25}},
            {"Blizzard", {Kind::Blizzard, 25}},
            {"Foggy", {Kind::Fog, 50}},          {"Fog", {Kind::Fog, 50}},
            {"Misty", {Kind::Fog, 25}},
            {"Windy", {Kind::Wind, 40}},
            {"Ashfall", {Kind::Ash, 25}},        {"Ash", {Kind::Ash, 25}},
            {"Sandstorm", {Kind::Sand, 25}},     {"Dusty", {Kind::Sand, 25}},
            {"Aurora", {Kind::Aurora, 25}},
            {"Precipitation", {Kind::Precipitation, 25}},
            {"Overcast", {Kind::Clouds, 75}},
        };
        for (const auto& [n, test] : kTests) {
            if (IEquals(n, name)) return test;
        }
        return std::nullopt;
    }

    bool WeatherCondition::Evaluate(const EvaluationContext& context) const {
        auto currentWeather = context.GetCurrentWeather();
        if (!currentWeather) return false;

        bool matches = false;
        if (weatherFormId != 0) {
            matches = (currentWeather->GetFormID() == weatherFormId);
        } else if (!weatherName.empty()) {
            // The classification flags, as the game itself reads them
            auto flags = currentWeather->data.flags;
            if (weatherName == "Clear" || weatherName == "Pleasant")
                matches = flags.any(RE::TESWeather::WeatherDataFlag::kPleasant);
            else if (weatherName == "Cloudy")
                matches = flags.any(RE::TESWeather::WeatherDataFlag::kCloudy);
            else if (weatherName == "Rainy" || weatherName == "Rain")
                matches = flags.any(RE::TESWeather::WeatherDataFlag::kRainy);
            else if (weatherName == "Snowy" || weatherName == "Snow")
                matches = flags.any(RE::TESWeather::WeatherDataFlag::kSnow);
            else if (auto test = ProfileTest(weatherName)) {
                auto profile = Weather::ProfileOf(currentWeather);
                matches = profile && profile->Level(test->first) >= test->second;
            } else {
                // An editor ID, or a glob over editor IDs
                matches = Weather::MatchesGlob(weatherName, Weather::EditorIdOf(currentWeather));
            }
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

    // ============================================================================
    // WeatherKindCondition
    // ============================================================================

    WeatherKindCondition::WeatherKindCondition(Weather::Kind kind, float minLevel, ComparisonOperator op)
        : kind(kind), minLevel(minLevel), operation(op) {}

    ValueResult WeatherKindCondition::ReadValue(const EvaluationContext& context) const {
        auto* weather = context.GetCurrentWeather();
        if (!weather) return ValueResult::Fail(ValueStatus::Unavailable, "no current weather");
        auto profile = Weather::ProfileOf(weather);
        if (!profile) return ValueResult::Fail(ValueStatus::Unavailable, "no current weather");
        return ValueResult::Of(profile->Level(kind));
    }

    bool WeatherKindCondition::Evaluate(const EvaluationContext& context) const {
        auto result = ReadValue(context);
        return result.Ok() && CompareFloat(static_cast<float>(result.value), minLevel, operation);
    }

    std::string WeatherKindCondition::GetDescription() const {
        return "Weather " + std::string(Weather::KindName(kind)) + " " + ComparisonOpToString(operation) + " " +
               std::to_string(static_cast<int>(minLevel));
    }

    nlohmann::json WeatherKindCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "WeatherKind";
        json["kind"] = std::string(Weather::KindName(kind));
        json["operator"] = ComparisonOpToName(operation);
        json["minLevel"] = minLevel;
        return json;
    }

    std::unique_ptr<Condition> WeatherKindCondition::Clone() const {
        return std::make_unique<WeatherKindCondition>(kind, minLevel, operation);
    }

} // namespace ConditionEngine
