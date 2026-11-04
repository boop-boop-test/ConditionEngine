#include "ConditionEngine/Conditions/RuleReferenceCondition.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Availability.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Stats.h"

namespace ConditionEngine {

    RuleReferenceCondition::RuleReferenceCondition(const std::string& name, bool inv, const std::string& uuid)
        : ruleName(name), ruleUUID(uuid), inverted(inv) {}

    std::shared_ptr<const Condition> RuleReferenceCondition::Resolve() const {
        const auto revision = Availability::Get().Revision();
        {
            std::lock_guard lock(bindMutex_);
            if (bound_ && boundRevision_ == revision) return bound_;
        }
        auto& registry = Registry::Get();
        const std::string* matched = &GetKey();
        auto named = registry.FindNamed(*matched);
        if (!named && !ruleUUID.empty() && !ruleName.empty()) {
            // UUID unknown to every resolver - fall back to the display name
            matched = &ruleName;
            named = registry.FindNamed(ruleName);
        }
        if (named && registry.IsEngineNamed(*matched)) {
            std::lock_guard lock(bindMutex_);
            bound_ = named;
            boundRevision_ = revision;
            Stats::CountRebind();
        }
        return named;
    }

    int RuleReferenceCondition::Cost() const {
        // Named conditions can nest; a depth cap keeps cycles finite
        thread_local int depth = 0;
        if (depth > 16) return 3;
        ++depth;
        auto named = Resolve();
        const int cost = named ? named->Cost() : 3;  // host rules: an ordinary read
        --depth;
        return cost;
    }

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

        auto named = Resolve();
        if (!named) {
            SKSE::log::error("Rule not found for evaluation: '{}' (key: {})", ruleName, key);
            return false;
        }

        context.rulesInProgress.insert(key);
        bool result = named->EvaluateCounted(context);
        context.rulesInProgress.erase(key);
        context.ruleMemo[key] = result;

        CE_TRACE(info, "Rule reference '{}' evaluated to {}", ruleName.empty() ? key : ruleName, result);
        return inverted ? !result : result;
    }

    TriResult RuleReferenceCondition::EvaluateTri(const EvaluationContext& context) const {
        const std::string& key = GetKey();
        if (context.rulesInProgress.count(key)) return TriResult::Of(false);
        auto named = Resolve();
        if (!named) return TriResult::Of(false);

        context.rulesInProgress.insert(key);
        auto r = named->EvaluateTriCounted(context);
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
