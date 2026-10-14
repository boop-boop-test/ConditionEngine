#include "ConditionEngine/Conditions/ScriptValueConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"

namespace ConditionEngine {

    ScriptValueCondition::ScriptValueCondition(const std::string& script, const std::string& varName,
                                               const std::string& valType, const std::string& value, ComparisonOperator op)
        : scriptName(script), variableName(varName), valueType(valType), targetValue(value), operation(op) {}

    bool ScriptValueCondition::Evaluate(const EvaluationContext& context) const {
        if (scriptName.empty() || variableName.empty()) return false;

        auto actor = context.GetActor();
        if (!actor) return false;

        // Access the Papyrus VM to read script property values
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            SKSE::log::trace("ScriptValueCondition: Papyrus VM not available");
            return false;
        }

        // Get the object handle for the actor
        auto* handlePolicy = vm->GetObjectHandlePolicy();
        if (!handlePolicy) return false;

        RE::VMHandle handle = handlePolicy->GetHandleForObject(
            static_cast<RE::VMTypeID>(actor->GetFormType()), actor);
        if (!handle) return false;

        // Find the bound script object on the actor
        RE::BSTSmartPointer<RE::BSScript::Object> scriptObject;
        if (!vm->FindBoundObject(handle, scriptName.c_str(), scriptObject) || !scriptObject) {
            SKSE::log::trace("ScriptValueCondition: Script '{}' not found on actor 0x{:X}",
                scriptName, actor->GetFormID());
            return false;
        }

        // Get the property variable
        auto* variable = scriptObject->GetProperty(RE::BSFixedString(variableName));
        if (!variable) {
            SKSE::log::trace("ScriptValueCondition: Property '{}' not found on script '{}'",
                variableName, scriptName);
            return false;
        }

        // Compare based on value type
        if (valueType == "int") {
            int current = static_cast<int>(variable->GetSInt());
            int target = 0;
            try { target = std::stoi(targetValue); } catch (...) { return false; }
            return CompareInt(current, target, operation);
        }
        else if (valueType == "float") {
            float current = variable->GetFloat();
            float target = 0.0f;
            try { target = std::stof(targetValue); } catch (...) { return false; }
            return CompareFloat(current, target, operation);
        }
        else if (valueType == "bool") {
            bool current = variable->GetBool();
            bool target = (targetValue == "true" || targetValue == "1" || targetValue == "True");
            // For bool, Equal means same value, NotEqual means different
            if (operation == ComparisonOperator::Equal) return current == target;
            if (operation == ComparisonOperator::NotEqual) return current != target;
            return false;
        }
        else if (valueType == "string") {
            RE::BSFixedString current = variable->GetString();
            if (operation == ComparisonOperator::Equal) return current == RE::BSFixedString(targetValue);
            if (operation == ComparisonOperator::NotEqual) return current != RE::BSFixedString(targetValue);
            return false;
        }

        SKSE::log::warn("ScriptValueCondition: Unknown value type '{}'", valueType);
        return false;
    }

    std::string ScriptValueCondition::GetDescription() const {
        return "Script " + scriptName + "." + variableName + " " + ComparisonOpToString(operation) + " " + targetValue;
    }

    nlohmann::json ScriptValueCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "ScriptValue";
        json["scriptName"] = scriptName;
        json["variableName"] = variableName;
        json["valueType"] = valueType;
        json["targetValue"] = targetValue;
        json["operator"] = ComparisonOpToName(operation);
        return json;
    }

    std::unique_ptr<Condition> ScriptValueCondition::Clone() const {
        return std::make_unique<ScriptValueCondition>(scriptName, variableName, valueType, targetValue, operation);
    }

    std::vector<std::string> ScriptValueCondition::Validate() const {
        std::vector<std::string> errors;
        if (scriptName.empty()) {
            errors.push_back("Script Value condition: no script name specified");
        }
        if (variableName.empty()) {
            errors.push_back("Script Value condition: no variable name specified");
        }
        return errors;
    }

} // namespace ConditionEngine
