#include "ConditionEngine/Conditions/EquipmentConditions.h"
#include "ConditionEngine/EvaluationContext.h"

namespace ConditionEngine {

    bool IsBipedSlotWorn(RE::Actor* actor, std::uint32_t slot) {
        if (!actor || slot < 30 || slot > 61) return false;
        auto bipedSlot = static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(1u << (slot - 30));
        return actor->GetWornArmor(bipedSlot) != nullptr;
    }

    HasSlotsEquippedCondition::HasSlotsEquippedCondition(std::vector<std::uint32_t> slots, bool matchAll)
        : slots_(std::move(slots)), matchAll_(matchAll) {}

    bool HasSlotsEquippedCondition::Evaluate(const EvaluationContext& context) const {
        auto* actor = context.GetActor();
        if (!actor) return false;
        if (slots_.empty()) return !matchAll_;  // empty + any = true, empty + all = false

        auto isSlotEquipped = [actor](std::uint32_t slot) { return IsBipedSlotWorn(actor, slot); };

        if (matchAll_) {
            return std::all_of(slots_.begin(), slots_.end(), isSlotEquipped);
        } else {
            return std::any_of(slots_.begin(), slots_.end(), isSlotEquipped);
        }
    }

    std::string HasSlotsEquippedCondition::GetDescription() const {
        std::string desc = matchAll_ ? "All of slots [" : "Any of slots [";
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (i > 0) desc += ", ";
            desc += std::to_string(slots_[i]);
        }
        desc += "] equipped";
        return desc;
    }

    nlohmann::json HasSlotsEquippedCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "HasSlotsEquipped";
        json["slots"] = slots_;
        json["matchMode"] = matchAll_ ? "all" : "any";
        return json;
    }

    std::unique_ptr<Condition> HasSlotsEquippedCondition::Clone() const {
        return std::make_unique<HasSlotsEquippedCondition>(slots_, matchAll_);
    }

} // namespace ConditionEngine
