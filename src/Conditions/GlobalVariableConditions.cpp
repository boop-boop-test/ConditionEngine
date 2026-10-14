#include "ConditionEngine/Conditions/GlobalVariableConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Forms.h"

namespace ConditionEngine {

    GlobalVariableCondition::GlobalVariableCondition(const std::string& name, float value, ComparisonOperator op)
        : globalName(name), targetValue(value), operation(op) {}

    ValueResult GlobalVariableCondition::ReadValue(const EvaluationContext&) const {
        if (globalName.empty()) return ValueResult::Fail(ValueStatus::Error, "no global variable specified");

        // Lazy-cache the global pointer on first use (form string, or editor ID - globals keep theirs)
        if (!cacheAttempted) {
            cacheAttempted = true;
            cachedGlobal = Forms::LookupForm<RE::TESGlobal>(globalName);
            if (!cachedGlobal) {
                SKSE::log::warn("GlobalVariableCondition: Could not find global '{}'", globalName);
            }
        }

        if (!cachedGlobal) return ValueResult::Fail(ValueStatus::Unavailable, "global not found: " + globalName);
        return ValueResult::Of(cachedGlobal->value);
    }

    bool GlobalVariableCondition::Evaluate(const EvaluationContext& context) const {
        const auto current = ReadValue(context);
        return current.Ok() && CompareFloat(static_cast<float>(current.value), targetValue, operation);
    }

    std::string GlobalVariableCondition::GetDescription() const {
        return "Global " + globalName + " " + ComparisonOpToString(operation) + " " + std::to_string(targetValue);
    }

    nlohmann::json GlobalVariableCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "GlobalVariable";
        json["globalId"] = globalName;  // FromJson reads "globalId" first
        json["value"] = targetValue;
        json["operator"] = ComparisonOpToName(operation);
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
