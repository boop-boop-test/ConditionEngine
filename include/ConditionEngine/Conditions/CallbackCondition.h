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

} // namespace ConditionEngine
