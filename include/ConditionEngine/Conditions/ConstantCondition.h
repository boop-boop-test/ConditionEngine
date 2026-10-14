#pragma once

// Constant condition - always true or always false ("true"/"false" in expressions)

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    class ConstantCondition : public Condition {
    public:
        explicit ConstantCondition(bool value) : value_(value) {}

        bool Evaluate(const EvaluationContext&) const override { return value_; }
        std::string GetDescription() const override { return value_ ? "Always" : "Never"; }
        nlohmann::json ToJson() const override { return {{"type", "Constant"}, {"value", value_}}; }
        std::unique_ptr<Condition> Clone() const override { return std::make_unique<ConstantCondition>(value_); }
        EventDependency GetEventDependencies() const override { return EventDependency::None; }
        std::optional<bool> ConstantResult() const override { return value_; }
        int Cost() const override { return 0; }

        bool GetValue() const { return value_; }

    private:
        bool value_;
    };

} // namespace ConditionEngine
