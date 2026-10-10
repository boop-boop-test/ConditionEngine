#pragma once

// Equipment-related conditions:
// HasSlotsEquipped - checks whether specific biped slots are occupied

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    // Returns true when the actor wears armor in biped slot 30-61
    bool IsBipedSlotWorn(RE::Actor* actor, std::uint32_t slot);

    // Checks whether specific equipment slots are currently occupied on the actor.
    // Supports "any" (at least one slot filled) or "all" (all specified slots filled).
    class HasSlotsEquippedCondition : public Condition {
    public:
        HasSlotsEquippedCondition(std::vector<std::uint32_t> slots, bool matchAll = false);

        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Equipment; }

    private:
        std::vector<std::uint32_t> slots_;  // Slot numbers (30-61)
        bool matchAll_;                      // true = all slots must be filled, false = any one
    };

} // namespace ConditionEngine
