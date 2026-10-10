#include "ConditionEngine/Conditions/GlobalVariableConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"

namespace ConditionEngine {

    GlobalVariableCondition::GlobalVariableCondition(const std::string& name, float value, ComparisonOperator op)
        : globalName(name), targetValue(value), operation(op) {}

    bool GlobalVariableCondition::Evaluate(const EvaluationContext& context) const {
        if (globalName.empty()) return false;

        // Lazy-cache the global pointer on first eval (requires po3_Tweaks for editor IDs)
        if (!cacheAttempted) {
            cacheAttempted = true;
            auto* form = RE::TESForm::LookupByEditorID(globalName);
            if (form) {
                cachedGlobal = form->As<RE::TESGlobal>();
            }
            if (!cachedGlobal) {
                SKSE::log::warn("GlobalVariableCondition: Could not find global '{}' - requires po3_Tweaks", globalName);
            }
        }

        if (!cachedGlobal) return false;

        return CompareFloat(cachedGlobal->value, targetValue, operation);
    }

    std::string GlobalVariableCondition::GetDescription() const {
        return "Global " + globalName + " " + ComparisonOpToString(operation) + " " + std::to_string(targetValue);
    }

    nlohmann::json GlobalVariableCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "GlobalVariable";
        json["globalName"] = globalName;
        json["value"] = targetValue;
        json["operation"] = static_cast<int>(operation);
        return json;
    }

    std::unique_ptr<Condition> GlobalVariableCondition::Clone() const {
        return std::make_unique<GlobalVariableCondition>(globalName, targetValue, operation);
    }

    std::vector<std::string> GlobalVariableCondition::Validate() const {
        std::vector<std::string> errors;
        if (globalName.empty()) {
            errors.push_back("Global Variable condition: no global variable specified");
        }
        return errors;
    }

} // namespace ConditionEngine
