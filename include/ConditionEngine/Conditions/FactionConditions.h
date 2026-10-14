#pragma once

// Faction membership condition (uses ParseFormString for mod-safe formId resolution)

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    // Faction condition - checks actor faction membership and rank
    class FactionCondition : public Condition {
    public:
        FactionCondition(const std::string& factionName, int rank = 0, ComparisonOperator op = ComparisonOperator::GreaterEqual);
        
        bool Evaluate(const EvaluationContext& context) const override;
        ValueResult ReadValue(const EvaluationContext& context) const override;
        RE::TESFaction* ResolveFaction() const;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Faction; }
        std::vector<std::string> Validate() const override;

    private:
        std::string factionName;
        int rank;
        ComparisonOperator operation;
        mutable RE::TESFaction* cachedFaction = nullptr;
        mutable bool cacheAttempted = false;
        mutable std::uint64_t attemptRevision = 0;  // availability revision of the last attempt
    };

} // namespace ConditionEngine
