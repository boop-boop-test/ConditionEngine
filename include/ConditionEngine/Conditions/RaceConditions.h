#pragma once

// Race condition (uses ParseFormString for mod-safe formId resolution)

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    // Race condition - checks whether actor is a specific race
    class RaceCondition : public Condition {
    public:
        RaceCondition(const std::string& raceFormId);

        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::None; }
        std::vector<std::string> Validate() const override;

    private:
        std::string raceFormId;
        mutable RE::TESRace* cachedRace = nullptr;
        mutable bool cacheAttempted = false;
    };

} // namespace ConditionEngine
