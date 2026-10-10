#pragma once

// Script value condition (Papyrus VM property access)

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    // Script value condition - reads a Papyrus script property value at runtime
    // Accesses the Papyrus VM to read bound object properties
    class ScriptValueCondition : public Condition {
    public:
        ScriptValueCondition(const std::string& scriptName, const std::string& varName,
                           const std::string& valueType, const std::string& value, ComparisonOperator op);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Script; }
        std::vector<std::string> Validate() const override;

    private:
        std::string scriptName;
        std::string variableName;
        std::string valueType; // "int", "float", "bool", "string"
        std::string targetValue;
        ComparisonOperator operation;
    };

} // namespace ConditionEngine
