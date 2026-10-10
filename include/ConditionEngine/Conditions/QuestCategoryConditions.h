#pragma once

// Quest category condition — checks if any quest in a named questline/category is tracked (player "set active")

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    // Known quest categories for Skyrim (matches QUEST_DATA::Type)
    enum class QuestCategory {
        MainQuest,          // Main quest (Dragonborn prophecy)
        Companions,         // Companions guild line
        CollegeOfWinterhold,// Mages guild line
        ThievesGuild,       // Thieves guild line
        DarkBrotherhood,    // Dark Brotherhood guild line
        CivilWar,           // Civil War questline
        Daedric,            // Daedric quests
        SideQuest,          // Side quests
        Miscellaneous,      // Miscellaneous quests
        Dawnguard,          // DLC1 main quest
        Dragonborn,         // DLC2 main quest
        Unknown
    };

    QuestCategory QuestCategoryFromString(const std::string& str);
    std::string QuestCategoryToString(QuestCategory cat);

    // Quest category condition — true if any quest in ANY of the given categories is player-tracked (has displayed objectives)
    class QuestCategoryCondition : public Condition {
    public:
        QuestCategoryCondition(const std::string& category);
        QuestCategoryCondition(const std::vector<std::string>& categories);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Quest; }

    private:
        std::vector<std::string> categoryNames;
        std::vector<QuestCategory> categories;
    };

} // namespace ConditionEngine
