#include "ConditionEngine/Conditions/CompareCondition.h"
#include "ConditionEngine/Expression.h"

namespace ConditionEngine {

    CompareCondition::CompareCondition(std::shared_ptr<const Value> left, ComparisonOperator op,
                                       std::shared_ptr<const Value> right, nlohmann::json leftJson,
                                       nlohmann::json rightJson)
        : left_(std::move(left)), op_(op), right_(std::move(right)), leftJson_(std::move(leftJson)),
          rightJson_(std::move(rightJson)) {}

    bool CompareCondition::Evaluate(const EvaluationContext& context) const {
        return EvaluateTri(context).state == TriState::True;
    }

    TriResult CompareCondition::EvaluateTri(const EvaluationContext& context) const {
        if (!left_ || !right_) return TriResult::Of(false);
        const auto lhs = left_->ReadCounted(context);
        const auto rhs = right_->ReadCounted(context);
        if (lhs.status == ValueStatus::Pending) return TriResult::Pending(lhs.reason);
        if (rhs.status == ValueStatus::Pending) return TriResult::Pending(rhs.reason);
        if (!lhs.Ok()) return TriResult::Unavailable(lhs.reason);
        if (!rhs.Ok()) return TriResult::Unavailable(rhs.reason);
        return TriResult::Of(CompareValues(lhs.value, op_, rhs.value));
    }

    std::optional<bool> CompareCondition::ConstantResult() const {
        auto lhs = left_ ? left_->ConstantValue() : std::nullopt;
        auto rhs = right_ ? right_->ConstantValue() : std::nullopt;
        if (!lhs || !rhs) return std::nullopt;
        return CompareValues(*lhs, op_, *rhs);
    }

    bool CompareValues(double lhs, ComparisonOperator op, double rhs) {
        switch (op) {
            case ComparisonOperator::Equal: return lhs == rhs;
            case ComparisonOperator::NotEqual: return lhs != rhs;
            case ComparisonOperator::GreaterThan: return lhs > rhs;
            case ComparisonOperator::LessThan: return lhs < rhs;
            case ComparisonOperator::GreaterEqual: return lhs >= rhs;
            case ComparisonOperator::LessEqual: return lhs <= rhs;
        }
        return false;
    }

    std::string CompareCondition::GetDescription() const { return Expression::ToExpression(*this); }

    nlohmann::json CompareCondition::ToJson() const {
        return {{"type", "Compare"},
                {"left", left_ ? left_->ToJson() : leftJson_},
                {"operator", ComparisonOpToName(op_)},
                {"right", right_ ? right_->ToJson() : rightJson_}};
    }

    std::unique_ptr<Condition> CompareCondition::Clone() const {
        // Values are immutable, so the clone shares them
        return std::make_unique<CompareCondition>(left_, op_, right_, leftJson_, rightJson_);
    }

    EventDependency CompareCondition::GetEventDependencies() const {
        EventDependency deps = EventDependency::None;
        if (left_) deps |= left_->GetEventDependencies();
        if (right_) deps |= right_->GetEventDependencies();
        return deps;
    }

    std::vector<std::string> CompareCondition::Validate() const {
        std::vector<std::string> errors;
        for (const auto& [value, json, side] : {std::tuple{left_.get(), &leftJson_, "left"},
                                                std::tuple{right_.get(), &rightJson_, "right"}}) {
            if (!value) {
                std::string error;
                Value::FromJson(*json, &error);
                errors.push_back(std::string("Compare: ") + side + " side is not a value" +
                                 (error.empty() ? "" : " (" + error + ")"));
                continue;
            }
            for (auto& e : value->Validate()) errors.push_back(std::move(e));
        }
        return errors;
    }

} // namespace ConditionEngine
