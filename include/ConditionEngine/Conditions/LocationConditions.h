#pragma once

// Location-based conditions: InteriorExterior, LocationType, LocationKeyword, LocationCondition (legacy)

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    // Interior/Exterior condition - simple cell-based check
    class InteriorExteriorCondition : public Condition {
    public:
        explicit InteriorExteriorCondition(bool shouldBeInterior = true);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Location; }

    private:
        bool expectInterior;
    };

    // Location type condition - checks for specific location categories
    class LocationTypeCondition : public Condition {
    public:
        enum class LocationCategory {
            Town,
            City,
            Dungeon,
            Inn,
            Store,
            Temple,
            Guild,
            Dwelling,
            Wilderness,
            Military,
            Farm,
            Jail,
            PlayerHome,
            Castle,
        };
        
        explicit LocationTypeCondition(LocationCategory category);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Location; }
        
        LocationCategory GetCategory() const { return category; }

    private:
        LocationCategory category;
        
        static bool HasLocationKeyword(RE::BGSLocation* location, const std::string& keywordName);
        static bool HasAnySignificantKeyword(RE::BGSLocation* location);
    };

    // Location keyword condition - flexible keyword-based check
    class LocationKeywordCondition : public Condition {
    public:
        explicit LocationKeywordCondition(const std::string& keyword);
        LocationKeywordCondition(const std::vector<std::string>& keywords, bool matchAny = true);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Location; }
        std::vector<std::string> Validate() const override;
        
        const std::vector<std::string>& GetKeywords() const { return keywords; }
        bool IsMatchAny() const { return matchAny; }

    private:
        std::vector<std::string> keywords;
        bool matchAny;
        
        static bool HasLocationKeyword(RE::BGSLocation* location, const std::string& keywordName);
    };

    // Legacy LocationCondition - kept for backward compatibility with existing saves
    class LocationCondition : public Condition {
    public:
        enum class LocationType {
            Any,
            Interior,
            Exterior,
            Town,
            City,
            Dungeon,
            Wilderness,
            Custom
        };
        
        LocationCondition(LocationType type, const std::string& customLocation = "");
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Location; }

    private:
        LocationType locationType;
        std::string customLocationName;
        
        bool HasLocationKeyword(RE::BGSLocation* location, const std::string& keywordName) const;
        bool HasAnyLocationKeywords(RE::BGSLocation* location) const;
    };

} // namespace ConditionEngine
