#pragma once

// Rule reference condition - evaluates a named condition (a "rule") from the registry.
// The key is the rule UUID when set, otherwise the rule name. Named conditions come
// from the engine's own store (presets, C API, Papyrus) or from host resolvers.

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

#include <mutex>

namespace ConditionEngine {

    class RuleReferenceCondition : public Condition {
    public:
        RuleReferenceCondition(const std::string& ruleName, bool inverted = false, const std::string& ruleUUID = "");

        bool Evaluate(const EvaluationContext& context) const override;
        TriResult EvaluateTri(const EvaluationContext& context) const override;
        int Cost() const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override;
        std::vector<std::string> Validate() const override;

        const std::string& GetRuleName() const { return ruleName; }
        void SetRuleName(const std::string& name) {
            ruleName = name;
            Unbind();
        }

        const std::string& GetRuleUUID() const { return ruleUUID; }
        void SetRuleUUID(const std::string& uuid) {
            ruleUUID = uuid;
            Unbind();
        }

        bool IsInverted() const { return inverted; }

        // Key used for registry lookup and memoization
        const std::string& GetKey() const { return ruleUUID.empty() ? ruleName : ruleUUID; }

    private:
        // The named condition, looked up by key (then display name). Engine-owned ones are
        // kept until the availability revision moves; host-resolved ones (DressCode rules)
        // are looked up every time, since hosts change them without moving it.
        std::shared_ptr<const Condition> Resolve() const;
        void Unbind() {
            std::lock_guard lock(bindMutex_);
            boundRevision_ = ~0ull;
            bound_.reset();
        }

        std::string ruleName;
        std::string ruleUUID;
        bool inverted;

        mutable std::mutex bindMutex_;
        mutable std::uint64_t boundRevision_ = ~0ull;
        mutable std::shared_ptr<const Condition> bound_;
    };

} // namespace ConditionEngine
