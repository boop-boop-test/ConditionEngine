#pragma once

// CompareCondition - compares two values (see Value.h):
//   $vampire_hunger > 80
//   ActorValue(Health) * 2 < ActorValue(Magicka)
//   {"type": "Compare", "left": <value>, "operator": "Greater", "right": <value>}
//
// The expression parser only emits Compare when the plain `Type(...) op literal`
// form does not fit, so existing condition JSON is unchanged. A side that does not
// read Ok (pending, unavailable, error) makes Evaluate false; EvaluateTri reports it
// as Pending or Unavailable with the side's reason.

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Value.h"

namespace ConditionEngine {

    // lhs op rhs on doubles
    bool CompareValues(double lhs, ComparisonOperator op, double rhs);

    class CompareCondition : public Condition {
    public:
        CompareCondition(std::shared_ptr<const Value> left, ComparisonOperator op, std::shared_ptr<const Value> right,
                         nlohmann::json leftJson = nullptr, nlohmann::json rightJson = nullptr);

        bool Evaluate(const EvaluationContext& context) const override;
        TriResult EvaluateTri(const EvaluationContext& context) const override;
        std::optional<bool> ConstantResult() const override;
        int Cost() const override { return (left_ ? left_->Cost() : 0) + (right_ ? right_->Cost() : 0); }
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override;
        std::vector<std::string> Validate() const override;

        const Value* Left() const { return left_.get(); }
        const Value* Right() const { return right_.get(); }

    private:
        std::shared_ptr<const Value> left_;
        ComparisonOperator op_;
        std::shared_ptr<const Value> right_;
        // Original JSON of a side that failed to build, kept so saving does not lose it
        nlohmann::json leftJson_;
        nlohmann::json rightJson_;
    };

} // namespace ConditionEngine
