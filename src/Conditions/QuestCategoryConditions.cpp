#include "ConditionEngine/Conditions/QuestCategoryConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"
#include <unordered_set>

namespace ConditionEngine {

    QuestCategory QuestCategoryFromString(const std::string& str) {
        if (str == "MainQuest")           return QuestCategory::MainQuest;
        if (str == "Companions")          return QuestCategory::Companions;
        if (str == "CollegeOfWinterhold") return QuestCategory::CollegeOfWinterhold;
        if (str == "ThievesGuild")        return QuestCategory::ThievesGuild;
        if (str == "DarkBrotherhood")     return QuestCategory::DarkBrotherhood;
        if (str == "CivilWar")            return QuestCategory::CivilWar;
        if (str == "Daedric")             return QuestCategory::Daedric;
        if (str == "Dawnguard")           return QuestCategory::Dawnguard;
        if (str == "Dragonborn")          return QuestCategory::Dragonborn;
        if (str == "SideQuest")           return QuestCategory::SideQuest;
        if (str == "Miscellaneous")       return QuestCategory::Miscellaneous;
        return QuestCategory::Unknown;
    }

    std::string QuestCategoryToString(QuestCategory cat) {
        switch (cat) {
            case QuestCategory::MainQuest:           return "MainQuest";
            case QuestCategory::Companions:          return "Companions";
            case QuestCategory::CollegeOfWinterhold: return "CollegeOfWinterhold";
            case QuestCategory::ThievesGuild:        return "ThievesGuild";
            case QuestCategory::DarkBrotherhood:     return "DarkBrotherhood";
            case QuestCategory::CivilWar:            return "CivilWar";
            case QuestCategory::Daedric:             return "Daedric";
            case QuestCategory::Dawnguard:           return "Dawnguard";
            case QuestCategory::Dragonborn:          return "Dragonborn";
            case QuestCategory::SideQuest:           return "SideQuest";
            case QuestCategory::Miscellaneous:       return "Miscellaneous";
            default:                                 return "Unknown";
        }
    }

    // Map our category enum to Skyrim's QUEST_DATA::Type
    static RE::QUEST_DATA::Type CategoryToQuestType(QuestCategory cat) {
        switch (cat) {
            case QuestCategory::MainQuest:           return RE::QUEST_DATA::Type::kMainQuest;
            case QuestCategory::Companions:          return RE::QUEST_DATA::Type::kCompanionsQuest;
            case QuestCategory::CollegeOfWinterhold: return RE::QUEST_DATA::Type::kMagesGuild;
            case QuestCategory::ThievesGuild:        return RE::QUEST_DATA::Type::kThievesGuild;
            case QuestCategory::DarkBrotherhood:     return RE::QUEST_DATA::Type::kDarkBrotherhood;
            case QuestCategory::CivilWar:            return RE::QUEST_DATA::Type::kCivilWar;
            case QuestCategory::Daedric:             return RE::QUEST_DATA::Type::kDaedric;
            case QuestCategory::Dawnguard:           return RE::QUEST_DATA::Type::kDLC01_Vampire;
            case QuestCategory::Dragonborn:          return RE::QUEST_DATA::Type::kDLC02_Dragonborn;
            case QuestCategory::SideQuest:           return RE::QUEST_DATA::Type::kSideQuest;
            case QuestCategory::Miscellaneous:       return RE::QUEST_DATA::Type::kMiscellaneous;
            default:                                 return RE::QUEST_DATA::Type::kNone;
        }
    }

    QuestCategoryCondition::QuestCategoryCondition(const std::string& category) {
        categoryNames.push_back(category);
        categories.push_back(QuestCategoryFromString(category));
    }

    QuestCategoryCondition::QuestCategoryCondition(const std::vector<std::string>& cats) {
        for (const auto& cat : cats) {
            categoryNames.push_back(cat);
            categories.push_back(QuestCategoryFromString(cat));
        }
    }

    // Check if a quest has any objectives currently displayed (player "set active" / tracked)
    static bool HasDisplayedObjectives(const RE::TESQuest* quest) {
        if (!quest) return false;
        // BSSimpleList doesn't support const iteration in CommonLibVR-ng
        auto* mutableQuest = const_cast<RE::TESQuest*>(quest);
        for (auto& objItem : mutableQuest->objectives) {
            if (!objItem) continue;
            auto state = objItem->state.get();
            if (state == RE::QUEST_OBJECTIVE_STATE::kDisplayed ||
                state == RE::QUEST_OBJECTIVE_STATE::kCompletedDisplayed) {
                return true;
            }
        }
        return false;
    }

    bool QuestCategoryCondition::Evaluate(const EvaluationContext& /*context*/) const {
        if (categories.empty()) return false;

        // Build a set of quest types to match
        std::unordered_set<int> targetTypes;
        for (auto cat : categories) {
            if (cat == QuestCategory::Unknown) continue;
            auto qt = CategoryToQuestType(cat);
            if (qt != RE::QUEST_DATA::Type::kNone) {
                targetTypes.insert(static_cast<int>(qt));
            }
        }
        if (targetTypes.empty()) return false;

        // True if any quest of matching type is player-tracked (has displayed objectives)
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return false;

        for (auto* quest : dataHandler->GetFormArray<RE::TESQuest>()) {
            if (!quest) continue;
            if (!quest->IsActive()) continue;
            if (!targetTypes.contains(static_cast<int>(quest->GetType()))) continue;
            if (HasDisplayedObjectives(quest)) {
                return true;
            }
        }
        return false;
    }

    std::string QuestCategoryCondition::GetDescription() const {
        std::string desc = "Quest Category: ";
        for (size_t i = 0; i < categoryNames.size(); ++i) {
            if (i > 0) desc += ", ";
            desc += categoryNames[i];
        }
        desc += " is tracked";
        return desc;
    }

    nlohmann::json QuestCategoryCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "QuestCategory";
        // Store as array for multi-select, but keep backward compat
        json["categories"] = nlohmann::json::array();
        for (const auto& name : categoryNames) {
            json["categories"].push_back(name);
        }
        return json;
    }

    std::unique_ptr<Condition> QuestCategoryCondition::Clone() const {
        return std::make_unique<QuestCategoryCondition>(categoryNames);
    }

} // namespace ConditionEngine
