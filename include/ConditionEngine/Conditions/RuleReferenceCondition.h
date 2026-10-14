#pragma once

// Rule reference condition - evaluates a named condition (a "rule") from the registry.
// The key is the rule UUID when set, otherwise the rule name. Named conditions come
// from the engine's own store (presets, C API, Papyrus) or from host resolvers.

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    class RuleReferenceCondition : public Condition {
    public:
        RuleReferenceCondition(const std::string& ruleName, bool inverted = false, const std::string& ruleUUID = "");

        bool Evaluate(const EvaluationContext& context) const override;
        TriResult EvaluateTri(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override;
        std::vector<std::string> Validate() const override;

        const std::string& GetRuleName() const { return ruleName; }
        void SetRuleName(const std::string& name) { ruleName = name; }

        const std::string& GetRuleUUID() const { return ruleUUID; }
        void SetRuleUUID(const std::string& uuid) { ruleUUID = uuid; }

        bool IsInverted() const { return inverted; }

        // Key used for registry lookup and memoization
        const std::string& GetKey() const { return ruleUUID.empty() ? ruleName : ruleUUID; }

    private:
        std::string ruleName;
        std::string ruleUUID;
        bool inverted;
    };

} // namespace ConditionEngine
