#pragma once

// Logical condition (AND, OR, NOT combinator)

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    // Logical condition that combines other conditions
    class LogicalCondition : public Condition {
    public:
        LogicalCondition(LogicalOperator op);
        LogicalCondition(LogicalOperator op, std::vector<std::unique_ptr<Condition>> children);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override;
        std::vector<std::string> Validate() const override;
        
        void AddChild(std::unique_ptr<Condition> condition);
        void RemoveChild(std::size_t index);
        std::size_t GetChildCount() const { return children.size(); }
        
        LogicalOperator GetOperator() const { return operation; }
        void SetOperator(LogicalOperator op) { operation = op; }

    private:
        LogicalOperator operation;
        std::vector<std::unique_ptr<Condition>> children;
    };

} // namespace ConditionEngine
