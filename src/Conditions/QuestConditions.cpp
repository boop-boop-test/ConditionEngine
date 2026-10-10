#include "ConditionEngine/Conditions/QuestConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"

namespace ConditionEngine {

    QuestStageCondition::QuestStageCondition(std::uint32_t formId, int stage, ComparisonOperator op)
        : questFormId(formId), targetStage(stage), operation(op) {}

    QuestStageCondition::QuestStageCondition(const std::string& name, int stage, ComparisonOperator op)
        : questFormId(0), questName(name), targetStage(stage), operation(op) {}

    bool QuestStageCondition::Evaluate(const EvaluationContext& context) const {
        RE::TESQuest* quest = nullptr;
        if (questFormId != 0) {
            quest = RE::TESForm::LookupByID<RE::TESQuest>(questFormId);
        } else if (!questName.empty()) {
            auto* form = RE::TESForm::LookupByEditorID(questName);
            if (form) {
                quest = form->As<RE::TESQuest>();
            }
        }
        if (!quest) return false;

        int currentStage = quest->GetCurrentStageID();
        return CompareInt(currentStage, targetStage, operation);
    }

    std::string QuestStageCondition::GetDescription() const {
        std::string name = !questName.empty() ? questName : "ID " + std::to_string(questFormId);
        return "Quest " + name + " stage " + ComparisonOpToString(operation) + " " + std::to_string(targetStage);
    }

    nlohmann::json QuestStageCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "QuestStage";
        json["questId"] = questName;  // FromJson reads "questId" first, "questName" as legacy fallback
        json["stage"] = targetStage;
        json["operation"] = static_cast<int>(operation);
        return json;
    }

    std::unique_ptr<Condition> QuestStageCondition::Clone() const {
        if (!questName.empty()) {
            return std::make_unique<QuestStageCondition>(questName, targetStage, operation);
        }
        return std::make_unique<QuestStageCondition>(questFormId, targetStage, operation);
    }

    std::vector<std::string> QuestStageCondition::Validate() const {
        std::vector<std::string> errors;
        if (questName.empty() && questFormId == 0) {
            errors.push_back("Quest Stage condition: no quest specified");
        }
        return errors;
    }

} // namespace ConditionEngine
