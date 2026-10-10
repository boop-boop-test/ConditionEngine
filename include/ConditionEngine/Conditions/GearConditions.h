#pragma once

// Gear and inventory conditions:
//   ItemCount(item) >= 3, HasItem(item)       - item form, or a keyword matching any item
//   EquippedWeaponType(Bow), EquippedWeaponType([Sword, Dagger], hand=Left)
// (WornHasKeyword lives with the actor form conditions.)

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

#include <mutex>

namespace ConditionEngine {

    class ItemCountCondition : public Condition {
    public:
        ItemCountCondition(std::string item, int count, ComparisonOperator op);

        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Inventory; }
        std::vector<std::string> Validate() const override;

    private:
        RE::TESForm* Resolve() const;

        std::string item_;
        int count_;
        ComparisonOperator op_;
        mutable std::once_flag resolveOnce_;
        mutable RE::TESForm* resolved_ = nullptr;
    };

    class EquippedWeaponTypeCondition : public Condition {
    public:
        enum class Hand { Either, Right, Left, Both };

        EquippedWeaponTypeCondition(std::vector<std::string> types, Hand hand);

        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Equipment; }
        std::vector<std::string> Validate() const override;

        static Hand ParseHand(const std::string& text);

        // Classifies what is held in one hand: Unarmed, Sword, Dagger, WarAxe, Mace,
        // Greatsword, Battleaxe, Warhammer, Bow, Crossbow, Staff, Spell, Shield, Torch
        static std::string Classify(RE::Actor* actor, bool leftHand);

    private:
        bool HandMatches(RE::Actor* actor, bool leftHand) const;

        std::vector<std::string> types_;
        Hand hand_;
    };

    class Registry;
    void RegisterGearConditionTypes(Registry& registry);

} // namespace ConditionEngine
