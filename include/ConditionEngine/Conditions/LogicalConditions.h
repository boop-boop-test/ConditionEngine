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
        TriResult EvaluateTri(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override;
        std::vector<std::string> Validate() const override;

        void AddChild(std::unique_ptr<Condition> condition);
        void RemoveChild(std::size_t index);
        std::size_t GetChildCount() const { return children.size(); }

        LogicalOperator GetOperator() const { return operation; }
        void SetOperator(LogicalOperator op) {
            operation = op;
            Plan();
        }

        std::optional<bool> ConstantResult() const override { return constant_; }

        // Read-only views for tools (Explain): children in written order, and the indices
        // evaluated, in evaluation order
        const std::vector<std::unique_ptr<Condition>>& GetChildren() const { return children; }
        const std::vector<std::size_t>& GetEvaluationOrder() const { return order_; }
        int Cost() const override { return cost_; }

    private:
        // Compiles the evaluation plan (issue 088 steps 4-5): a decisive constant child
        // decides the group; neutral constants and duplicate children are skipped; the rest
        // are evaluated cheapest first. The written order is kept for ToJson / display.
        void Plan();

        LogicalOperator operation;
        std::vector<std::unique_ptr<Condition>> children;
        std::vector<std::size_t> order_;   // children to evaluate, cheapest first
        std::optional<bool> constant_;     // set when the group cannot depend on game state
        int cost_ = 0;
    };

} // namespace ConditionEngine
