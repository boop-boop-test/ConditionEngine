#pragma once

// CallbackCondition - a condition backed by a host function.
// Used to adapt C-API custom types, callback-registered named conditions and
// cross-module resolvers into ordinary Condition objects.

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    class CallbackCondition : public Condition {
    public:
        using EvalFn = std::function<bool(const EvaluationContext&)>;

        // `json` is what ToJson returns - the original definition, so the condition
        // round-trips through FromJson back into the same registered type.
        CallbackCondition(std::string description, EvalFn eval, EventDependency dependencies, nlohmann::json json)
            : description_(std::move(description)), eval_(std::move(eval)),
              dependencies_(dependencies), json_(std::move(json)) {}

        bool Evaluate(const EvaluationContext& context) const override { return eval_ ? eval_(context) : false; }
        std::string GetDescription() const override { return description_; }
        nlohmann::json ToJson() const override { return json_; }
        std::unique_ptr<Condition> Clone() const override {
            return std::make_unique<CallbackCondition>(description_, eval_, dependencies_, json_);
        }
        EventDependency GetEventDependencies() const override { return dependencies_; }

    private:
        std::string description_;
        EvalFn eval_;
        EventDependency dependencies_;
        nlohmann::json json_;
    };

    // A numeric type backed by a host function (C-API RegisterValueTypeEx). Reads the
    // number through the callback; as a condition it compares that number with the
    // JSON's compare field and operator, or tests it against 0 when there is none.
    class CallbackValueCondition : public Condition {
    public:
        using ReadFn = std::function<ValueResult(const EvaluationContext&)>;

        CallbackValueCondition(std::string description, ReadFn read, EventDependency dependencies, nlohmann::json json,
                               std::string compareField)
            : description_(std::move(description)), read_(std::move(read)), dependencies_(dependencies),
              json_(std::move(json)), compareField_(std::move(compareField)) {}

        bool Evaluate(const EvaluationContext& context) const override {
            return EvaluateTri(context).state == TriState::True;
        }
        TriResult EvaluateTri(const EvaluationContext& context) const override {
            const auto current = ReadValue(context);
            if (current.status == ValueStatus::Pending) return TriResult::Pending(current.reason);
            if (!current.Ok()) return TriResult::Unavailable(current.reason);
            if (compareField_.empty() || !json_.contains(compareField_) || !json_[compareField_].is_number()) {
                return TriResult::Of(current.value != 0.0);
            }
            const double target = json_[compareField_].get<double>();
            double lhs = current.value;
            bool result = false;
            switch (ParseComparisonOperator(json_, "operator")) {
                case ComparisonOperator::Equal: result = lhs == target; break;
                case ComparisonOperator::NotEqual: result = lhs != target; break;
                case ComparisonOperator::GreaterThan: result = lhs > target; break;
                case ComparisonOperator::LessThan: result = lhs < target; break;
                case ComparisonOperator::GreaterEqual: result = lhs >= target; break;
                case ComparisonOperator::LessEqual: result = lhs <= target; break;
            }
            return TriResult::Of(result);
        }
        ValueResult ReadValue(const EvaluationContext& context) const override {
            return read_ ? read_(context) : ValueResult::Fail(ValueStatus::Error, "no callback");
        }
        std::string GetDescription() const override { return description_; }
        nlohmann::json ToJson() const override { return json_; }
        std::unique_ptr<Condition> Clone() const override {
            return std::make_unique<CallbackValueCondition>(description_, read_, dependencies_, json_, compareField_);
        }
        EventDependency GetEventDependencies() const override { return dependencies_; }

    private:
        std::string description_;
        ReadFn read_;
        EventDependency dependencies_;
        nlohmann::json json_;
        std::string compareField_;
    };

} // namespace ConditionEngine
