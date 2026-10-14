#include "ConditionEngine/Conditions/RuleReferenceCondition.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Registry.h"

namespace ConditionEngine {

    RuleReferenceCondition::RuleReferenceCondition(const std::string& name, bool inv, const std::string& uuid)
        : ruleName(name), ruleUUID(uuid), inverted(inv) {}

    bool RuleReferenceCondition::Evaluate(const EvaluationContext& context) const {
        const std::string& key = GetKey();

        // Memoized result from earlier in this evaluation
        auto memoIt = context.ruleMemo.find(key);
        if (memoIt != context.ruleMemo.end()) {
            return inverted ? !memoIt->second : memoIt->second;
        }

        if (context.rulesInProgress.count(key)) {
            SKSE::log::warn("Circular rule reference detected: '{}' (key: {}) - returning false", ruleName, key);
            return false;
        }

        auto named = Registry::Get().FindNamed(key);
        if (!named && !ruleUUID.empty() && !ruleName.empty()) {
            // UUID unknown to every resolver - fall back to the display name
            named = Registry::Get().FindNamed(ruleName);
        }
        if (!named) {
            SKSE::log::error("Rule not found for evaluation: '{}' (key: {})", ruleName, key);
            return false;
        }

        context.rulesInProgress.insert(key);
        bool result = named->Evaluate(context);
        context.rulesInProgress.erase(key);
        context.ruleMemo[key] = result;

        CE_TRACE(info, "Rule reference '{}' evaluated to {}", ruleName.empty() ? key : ruleName, result);
        return inverted ? !result : result;
    }

    TriResult RuleReferenceCondition::EvaluateTri(const EvaluationContext& context) const {
        const std::string& key = GetKey();
        if (context.rulesInProgress.count(key)) return TriResult::Of(false);
        auto named = Registry::Get().FindNamed(key);
        if (!named && !ruleUUID.empty() && !ruleName.empty()) named = Registry::Get().FindNamed(ruleName);
        if (!named) return TriResult::Of(false);

        context.rulesInProgress.insert(key);
        auto r = named->EvaluateTri(context);
        context.rulesInProgress.erase(key);
        if (!r.Decided() || !inverted) return r;
        return TriResult::Of(r.state == TriState::False);
    }

    std::string RuleReferenceCondition::GetDescription() const {
        std::string desc = inverted ? "NOT " : "";
        desc += "Rule: " + ruleName;
        return desc;
    }

    nlohmann::json RuleReferenceCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "Rule";
        json["ruleName"] = ruleName;
        if (!ruleUUID.empty()) {
            json["ruleUUID"] = ruleUUID;
        }
        json["inverted"] = inverted;
        return json;
    }

    std::unique_ptr<Condition> RuleReferenceCondition::Clone() const {
        return std::make_unique<RuleReferenceCondition>(ruleName, inverted, ruleUUID);
    }

    std::vector<std::string> RuleReferenceCondition::Validate() const {
        std::vector<std::string> errors;
        if (ruleUUID.empty() && ruleName.empty()) {
            errors.push_back("Rule Reference condition: no rule specified");
        }
        return errors;
    }

    EventDependency RuleReferenceCondition::GetEventDependencies() const {
        auto named = Registry::Get().FindNamed(GetKey());
        if (named) {
            return named->GetEffectiveEventDependencies();
        }
        // If rule not found, assume all dependencies to be safe
        return EventDependency::Time | EventDependency::Location | EventDependency::Weather |
               EventDependency::Combat | EventDependency::ActorValue;
    }

} // namespace ConditionEngine
