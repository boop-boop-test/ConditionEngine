#pragma once

// Script value condition (Papyrus VM property access)

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    // Script value condition - reads a Papyrus script property value at runtime
    // Accesses the Papyrus VM to read bound object properties. The script is the one
    // bound to the actor, or to `form` when set (a quest such as "0xD62|SeverActions.esp").
    class ScriptValueCondition : public Condition {
    public:
        ScriptValueCondition(const std::string& scriptName, const std::string& varName,
                           const std::string& valueType, const std::string& value, ComparisonOperator op,
                           std::string form = {});
        
        bool Evaluate(const EvaluationContext& context) const override;
        // Int, float or bool (0 / 1) property as a number
        ValueResult ReadValue(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Script; }
        std::vector<std::string> Validate() const override;

    private:
        // The property variable, or null with `error` set
        const RE::BSScript::Variable* FindProperty(const EvaluationContext& context,
                                                   RE::BSTSmartPointer<RE::BSScript::Object>& holder,
                                                   std::string& error) const;

        std::string scriptName;
        std::string variableName;
        std::string valueType; // "int", "float", "bool", "string"
        std::string targetValue;
        ComparisonOperator operation;
        std::string form;      // empty = the actor
    };

} // namespace ConditionEngine
