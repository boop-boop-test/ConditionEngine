#pragma once

// Quest stage condition

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    // Quest stage condition - checks quest progress
    class QuestStageCondition : public Condition {
    public:
        QuestStageCondition(std::uint32_t questFormId, int stage, ComparisonOperator op);
        QuestStageCondition(const std::string& questName, int stage, ComparisonOperator op);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Quest; }
        std::vector<std::string> Validate() const override;

    private:
        std::uint32_t questFormId;
        std::string questName;
        int targetStage;
        ComparisonOperator operation;
    };

} // namespace ConditionEngine
