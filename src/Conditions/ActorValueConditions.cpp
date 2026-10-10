#include "ConditionEngine/Conditions/ActorValueConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"

namespace ConditionEngine {

    static std::string ComparisonOpToSymbol(ComparisonOperator op) {
        switch (op) {
            case ComparisonOperator::Equal:        return "==";
            case ComparisonOperator::NotEqual:     return "!=";
            case ComparisonOperator::GreaterThan:  return ">";
            case ComparisonOperator::LessThan:     return "<";
            case ComparisonOperator::GreaterEqual: return ">=";
            case ComparisonOperator::LessEqual:    return "<=";
        }
        return "==";
    }

    static std::string ComparisonOpToName(ComparisonOperator op) {
        switch (op) {
            case ComparisonOperator::Equal:        return "Equal";
            case ComparisonOperator::NotEqual:     return "NotEqual";
            case ComparisonOperator::GreaterThan:  return "Greater";
            case ComparisonOperator::LessThan:     return "Less";
            case ComparisonOperator::GreaterEqual: return "GreaterOrEqual";
            case ComparisonOperator::LessEqual:    return "LessOrEqual";
        }
        return "Equal";
    }

    static bool CompareFloatLocal(float lhs, float rhs, ComparisonOperator op) {
        switch (op) {
            case ComparisonOperator::Equal:        return lhs == rhs;
            case ComparisonOperator::NotEqual:     return lhs != rhs;
            case ComparisonOperator::GreaterThan:  return lhs > rhs;
            case ComparisonOperator::LessThan:     return lhs < rhs;
            case ComparisonOperator::GreaterEqual: return lhs >= rhs;
            case ComparisonOperator::LessEqual:    return lhs <= rhs;
        }
        return false;
    }

    ActorValueCondition::ActorValueCondition(RE::ActorValue av, float value, ComparisonOperator op, bool usePercent)
        : actorValue(av), targetValue(value), operation(op), usePercentage(usePercent) {}

    ActorValueCondition::ActorValueCondition(const std::string& avName, float value, ComparisonOperator op, bool usePercent)
        : actorValue(RE::ActorValue::kNone), actorValueName(avName), targetValue(value), operation(op), usePercentage(usePercent) {}

    bool ActorValueCondition::Evaluate(const EvaluationContext& context) const {
        auto actor = context.GetActor();
        if (!actor) return false;

        RE::ActorValue av = actorValue;
        if (av == RE::ActorValue::kNone && !actorValueName.empty()) {
            auto avList = RE::ActorValueList::GetSingleton();
            if (avList) {
                av = avList->LookupActorValueByName(actorValueName.c_str());
            }
        }
        if (av == RE::ActorValue::kNone) return false;

        auto* avOwner = actor->AsActorValueOwner();
        float currentValue = avOwner->GetActorValue(av);

        if (usePercentage) {
            float maxValue = avOwner->GetPermanentActorValue(av);
            if (maxValue > 0.0f) {
                currentValue = (currentValue / maxValue) * 100.0f;
            } else {
                currentValue = 0.0f;
            }
        }

        return CompareFloatLocal(currentValue, targetValue, operation);
    }

    std::string ActorValueCondition::GetDescription() const {
        std::string name = !actorValueName.empty() ? actorValueName : std::to_string(static_cast<int>(actorValue));
        std::string desc = "Actor Value " + name + " " + ComparisonOpToSymbol(operation) + " " + std::to_string(targetValue);
        if (usePercentage) desc += "%";
        return desc;
    }

    nlohmann::json ActorValueCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "ActorValue";
        json["actorValue"] = actorValueName;
        json["value"] = targetValue;
        json["operator"] = ComparisonOpToName(operation);
        json["usePercentage"] = usePercentage;
        return json;
    }

    std::unique_ptr<Condition> ActorValueCondition::Clone() const {
        if (!actorValueName.empty()) {
            return std::make_unique<ActorValueCondition>(actorValueName, targetValue, operation, usePercentage);
        }
        return std::make_unique<ActorValueCondition>(actorValue, targetValue, operation, usePercentage);
    }

} // namespace ConditionEngine
