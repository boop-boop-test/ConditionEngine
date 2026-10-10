#pragma once

// Time and date conditions: TimeCondition, DayOfWeekCondition, MonthCondition, DayOfMonthCondition

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    // Time-based condition (hour of day)
    class TimeCondition : public Condition {
    public:
        TimeCondition(float startHour, float endHour, ComparisonOperator op = ComparisonOperator::Equal);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Time; }

    private:
        float startHour;
        float endHour;
        ComparisonOperator operation;
        bool isRange;
    };

    // Day-of-week condition (bitmask of allowed days)
    // Skyrim days: 0=Sundas, 1=Morndas, 2=Tirdas, 3=Middas, 4=Turdas, 5=Fredas, 6=Loredas
    class DayOfWeekCondition : public Condition {
    public:
        explicit DayOfWeekCondition(std::uint8_t allowedDaysMask = 0x7F); // default: all days
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Date; }

        static constexpr const char* DayNames[] = {
            "Sundas", "Morndas", "Tirdas", "Middas", "Turdas", "Fredas", "Loredas"
        };

    private:
        std::uint8_t allowedDays; // bitmask: bit 0=Sundas ... bit 6=Loredas
    };

    // Month condition (bitmask of allowed months)
    // Skyrim months: 0=Morning Star ... 11=Evening Star
    class MonthCondition : public Condition {
    public:
        explicit MonthCondition(std::uint16_t allowedMonthsMask = 0x0FFF); // default: all months
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Date; }

        static constexpr const char* MonthNames[] = {
            "Morning Star", "Sun's Dawn", "First Seed", "Rain's Hand",
            "Second Seed", "Midyear", "Sun's Height", "Last Seed",
            "Hearthfire", "Frostfall", "Sun's Dusk", "Evening Star"
        };

    private:
        std::uint16_t allowedMonths; // bitmask: bit 0=Morning Star ... bit 11=Evening Star
    };

    // Day-of-month condition (bitmask of allowed days 1-31)
    class DayOfMonthCondition : public Condition {
    public:
        // bit 0 = day 1, bit 30 = day 31. Default: all 31 days enabled.
        explicit DayOfMonthCondition(std::uint32_t allowedDaysMask = 0x7FFFFFFF);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Date; }

    private:
        std::uint32_t allowedDays; // bitmask: bit 0 = day 1 ... bit 30 = day 31
    };

} // namespace ConditionEngine
