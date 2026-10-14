#pragma once

// Global variable condition (requires po3_Tweaks for editor ID resolution)

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    // Global variable condition - compares a TESGlobal's value
    class GlobalVariableCondition : public Condition {
    public:
        GlobalVariableCondition(const std::string& globalName, float value, ComparisonOperator op);
        
        bool Evaluate(const EvaluationContext& context) const override;
        ValueResult ReadValue(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::GlobalVariable; }
        std::vector<std::string> Validate() const override;

    private:
        std::string globalName;
        float targetValue;
        ComparisonOperator operation;
        mutable RE::TESGlobal* cachedGlobal = nullptr;
        mutable bool cacheAttempted = false;
    };

} // namespace ConditionEngine
