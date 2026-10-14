#include "ConditionEngine/Conditions/ScriptValueConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Forms.h"

namespace ConditionEngine {

    ScriptValueCondition::ScriptValueCondition(const std::string& script, const std::string& varName,
                                               const std::string& valType, const std::string& value, ComparisonOperator op,
                                               std::string formKey)
        : scriptName(script), variableName(varName), valueType(valType), targetValue(value), operation(op),
          form(std::move(formKey)) {}

    const RE::BSScript::Variable* ScriptValueCondition::FindProperty(const EvaluationContext& context,
                                                                     RE::BSTSmartPointer<RE::BSScript::Object>& scriptObject,
                                                                     std::string& error) const {
        if (scriptName.empty() || variableName.empty()) {
            error = "no script or property name";
            return nullptr;
        }

        // The object the script is bound to: the actor, or the configured form (e.g. a quest)
        RE::TESForm* owner = nullptr;
        if (!form.empty()) {
            owner = Forms::LookupAny(form);
            if (!owner) {
                error = "form not found: " + form;
                return nullptr;
            }
        } else {
            owner = context.GetActor();
            if (!owner) {
                error = "no actor";
                return nullptr;
            }
        }

        // Access the Papyrus VM to read script property values
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) {
            error = "Papyrus VM not available";
            return nullptr;
        }

        auto* handlePolicy = vm->GetObjectHandlePolicy();
        if (!handlePolicy) {
            error = "Papyrus handle policy not available";
            return nullptr;
        }

        RE::VMHandle handle = handlePolicy->GetHandleForObject(static_cast<RE::VMTypeID>(owner->GetFormType()), owner);
        if (!handle) {
            error = "no Papyrus handle for the form";
            return nullptr;
        }

        // Find the bound script object
        if (!vm->FindBoundObject(handle, scriptName.c_str(), scriptObject) || !scriptObject) {
            error = "script '" + scriptName + "' is not attached";
            SKSE::log::trace("ScriptValueCondition: Script '{}' not found on form 0x{:X}", scriptName, owner->GetFormID());
            return nullptr;
        }

        auto* variable = scriptObject->GetProperty(RE::BSFixedString(variableName));
        if (!variable) {
            error = "property '" + variableName + "' not found on script '" + scriptName + "'";
            SKSE::log::trace("ScriptValueCondition: Property '{}' not found on script '{}'", variableName, scriptName);
            return nullptr;
        }
        return variable;
    }

    ValueResult ScriptValueCondition::ReadValue(const EvaluationContext& context) const {
        RE::BSTSmartPointer<RE::BSScript::Object> scriptObject;
        std::string error;
        const auto* variable = FindProperty(context, scriptObject, error);
        if (!variable) return ValueResult::Fail(ValueStatus::Unavailable, error);
        if (variable->IsInt()) return ValueResult::Of(variable->GetSInt());
        if (variable->IsFloat()) return ValueResult::Of(variable->GetFloat());
        if (variable->IsBool()) return ValueResult::Of(variable->GetBool() ? 1.0 : 0.0);
        return ValueResult::Fail(ValueStatus::Error, scriptName + "." + variableName + " is not a number or bool");
    }

    bool ScriptValueCondition::Evaluate(const EvaluationContext& context) const {
        RE::BSTSmartPointer<RE::BSScript::Object> scriptObject;
        std::string error;
        const auto* variable = FindProperty(context, scriptObject, error);
        if (!variable) return false;

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
        if (!form.empty()) json["form"] = form;
        return json;
    }

    std::unique_ptr<Condition> ScriptValueCondition::Clone() const {
        return std::make_unique<ScriptValueCondition>(scriptName, variableName, valueType, targetValue, operation, form);
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
