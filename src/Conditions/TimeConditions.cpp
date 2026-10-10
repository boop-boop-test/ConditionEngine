#include "ConditionEngine/Conditions/TimeConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"

namespace ConditionEngine {

    // ============================================================================
    // TimeCondition implementation
    // ============================================================================

    TimeCondition::TimeCondition(float startHour, float endHour, ComparisonOperator op)
        : startHour(startHour), endHour(endHour), operation(op), isRange(startHour != endHour) {}

    bool TimeCondition::Evaluate(const EvaluationContext& context) const {
        float currentTime = context.GetCurrentTime();
        
        if (isRange) {
            if (startHour <= endHour) {
                return currentTime >= startHour && currentTime <= endHour;
            } else {
                // Overnight range (e.g., 22:00 to 06:00)
                return currentTime >= startHour || currentTime <= endHour;
            }
        } else {
            switch (operation) {
                case ComparisonOperator::Equal: return currentTime == startHour;
                case ComparisonOperator::NotEqual: return currentTime != startHour;
                case ComparisonOperator::GreaterThan: return currentTime > startHour;
                case ComparisonOperator::LessThan: return currentTime < startHour;
                case ComparisonOperator::GreaterEqual: return currentTime >= startHour;
                case ComparisonOperator::LessEqual: return currentTime <= startHour;
            }
        }
        return false;
    }

    std::string TimeCondition::GetDescription() const {
        if (isRange) {
            return "Time between " + std::to_string(startHour) + " and " + std::to_string(endHour);
        } else {
            return "Time " + std::to_string(startHour);
        }
    }

    nlohmann::json TimeCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "Time";
        json["startHour"] = startHour;
        json["endHour"] = endHour;
        json["operation"] = ComparisonOpToString(operation);
        json["isRange"] = isRange;
        return json;
    }

    std::unique_ptr<Condition> TimeCondition::Clone() const {
        return std::make_unique<TimeCondition>(startHour, endHour, operation);
    }

    // ============================================================================
    // DayOfWeekCondition implementation
    // ============================================================================

    DayOfWeekCondition::DayOfWeekCondition(std::uint8_t allowedDaysMask)
        : allowedDays(allowedDaysMask) {}

    bool DayOfWeekCondition::Evaluate(const EvaluationContext& context) const {
        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar) return false;

        // GetDayOfWeek() returns 0-6
        int dayOfWeek = calendar->GetDayOfWeek();
        if (dayOfWeek < 0 || dayOfWeek > 6) return false;

        return (allowedDays & (1 << dayOfWeek)) != 0;
    }

    std::string DayOfWeekCondition::GetDescription() const {
        if (allowedDays == 0x7F) return "Any day of the week";
        if (allowedDays == 0) return "No days";

        std::string desc = "Days: ";
        bool first = true;
        for (int i = 0; i < 7; ++i) {
            if (allowedDays & (1 << i)) {
                if (!first) desc += ", ";
                desc += DayNames[i];
                first = false;
            }
        }
        return desc;
    }

    nlohmann::json DayOfWeekCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "DayOfWeek";
        json["allowedDays"] = allowedDays;
        // Also include a human-readable array for the UI
        nlohmann::json daysArray = nlohmann::json::array();
        for (int i = 0; i < 7; ++i) {
            if (allowedDays & (1 << i)) {
                daysArray.push_back(DayNames[i]);
            }
        }
        json["dayNames"] = daysArray;
        return json;
    }

    std::unique_ptr<Condition> DayOfWeekCondition::Clone() const {
        return std::make_unique<DayOfWeekCondition>(allowedDays);
    }

    // ============================================================================
    // MonthCondition implementation
    // ============================================================================

    MonthCondition::MonthCondition(std::uint16_t allowedMonthsMask)
        : allowedMonths(allowedMonthsMask) {}

    bool MonthCondition::Evaluate(const EvaluationContext& context) const {
        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar) return false;

        // GetMonth() returns 0-11
        auto month = calendar->GetMonth();
        int monthIndex = static_cast<int>(month);
        if (monthIndex < 0 || monthIndex > 11) return false;

        return (allowedMonths & (1 << monthIndex)) != 0;
    }

    std::string MonthCondition::GetDescription() const {
        if (allowedMonths == 0x0FFF) return "Any month";
        if (allowedMonths == 0) return "No months";

        std::string desc = "Months: ";
        bool first = true;
        for (int i = 0; i < 12; ++i) {
            if (allowedMonths & (1 << i)) {
                if (!first) desc += ", ";
                desc += MonthNames[i];
                first = false;
            }
        }
        return desc;
    }

    nlohmann::json MonthCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "Month";
        json["allowedMonths"] = allowedMonths;
        nlohmann::json monthsArray = nlohmann::json::array();
        for (int i = 0; i < 12; ++i) {
            if (allowedMonths & (1 << i)) {
                monthsArray.push_back(MonthNames[i]);
            }
        }
        json["monthNames"] = monthsArray;
        return json;
    }

    std::unique_ptr<Condition> MonthCondition::Clone() const {
        return std::make_unique<MonthCondition>(allowedMonths);
    }

    // ============================================================================
    // DayOfMonthCondition implementation
    // ============================================================================

    DayOfMonthCondition::DayOfMonthCondition(std::uint32_t allowedDaysMask)
        : allowedDays(allowedDaysMask) {}

    bool DayOfMonthCondition::Evaluate(const EvaluationContext& context) const {
        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar) return false;

        // GetDay() returns 1-31; map to bit index 0-30
        int currentDay = static_cast<int>(calendar->GetDay());
        if (currentDay < 1 || currentDay > 31) return false;

        return (allowedDays & (1u << (currentDay - 1))) != 0;
    }

    std::string DayOfMonthCondition::GetDescription() const {
        if (allowedDays == 0x7FFFFFFF) return "Any day of the month";
        if (allowedDays == 0) return "No days";

        std::string desc = "Days: ";
        bool first = true;
        for (int i = 0; i < 31; ++i) {
            if (allowedDays & (1u << i)) {
                if (!first) desc += ", ";
                desc += std::to_string(i + 1);
                first = false;
            }
        }
        return desc;
    }

    nlohmann::json DayOfMonthCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "DayOfMonth";
        json["allowedDays"] = allowedDays;
        // Human-readable array for the UI
        nlohmann::json daysArray = nlohmann::json::array();
        for (int i = 0; i < 31; ++i) {
            if (allowedDays & (1u << i)) {
                daysArray.push_back(i + 1);
            }
        }
        json["dayNumbers"] = daysArray;
        return json;
    }

    std::unique_ptr<Condition> DayOfMonthCondition::Clone() const {
        return std::make_unique<DayOfMonthCondition>(allowedDays);
    }

} // namespace ConditionEngine
