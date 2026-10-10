#pragma once

// World and mod-detection conditions:
//   Worldspace(Tamriel), Hold(Whiterun), InLocation("Dragonsreach"), InCell(WhiterunBanneredMare)
//   IsPluginLoaded("Frostfall.esp"), Holiday(Saturalia) / Holiday(Any)
//
// Places match by form string, editor ID (where the game keeps one) or display
// name, case-insensitively.

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

#include <mutex>

namespace ConditionEngine {

    class PlaceCondition : public Condition {
    public:
        enum class Kind { Worldspace, Hold, Location, Cell };

        PlaceCondition(Kind kind, std::vector<std::string> places, bool includeParents = true);

        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Location; }
        std::vector<std::string> Validate() const override;

    private:
        struct Target {
            std::string text;
            RE::TESForm* form = nullptr;
        };

        const std::vector<Target>& Resolve() const;
        bool Matches(RE::TESForm* form) const;

        Kind kind_;
        std::vector<std::string> places_;
        bool includeParents_;
        mutable std::once_flag resolveOnce_;
        mutable std::vector<Target> targets_;
    };

    class PluginLoadedCondition : public Condition {
    public:
        explicit PluginLoadedCondition(std::string plugin);

        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override { return "Plugin loaded: " + plugin_; }
        nlohmann::json ToJson() const override { return {{"type", "IsPluginLoaded"}, {"plugin", plugin_}}; }
        std::unique_ptr<Condition> Clone() const override { return std::make_unique<PluginLoadedCondition>(plugin_); }
        EventDependency GetEventDependencies() const override { return EventDependency::None; }

    private:
        std::string plugin_;
    };

    class HolidayCondition : public Condition {
    public:
        explicit HolidayCondition(std::vector<std::string> holidays);

        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override { return std::make_unique<HolidayCondition>(holidays_); }
        EventDependency GetEventDependencies() const override { return EventDependency::Date; }
        std::vector<std::string> Validate() const override;

        // Name of the holiday on the given date (month 0 = Morning Star, day 1-31), or empty
        static std::string_view HolidayOn(int month, int day);

    private:
        std::vector<std::string> holidays_;
    };

    class Registry;
    void RegisterWorldConditionTypes(Registry& registry);

} // namespace ConditionEngine
