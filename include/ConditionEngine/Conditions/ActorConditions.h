#pragma once

// Actor identity and state conditions:
//   Flags:    Sneaking, Sitting, InDialogue, Bleedout, WeaponDrawn, IsFollower, IsPlayer
//   Forms:    IsActor, HasKeyword, HasMagicEffect, HasPerk, HasSpell, WornHasKeyword
//   Values:   Level >= 20, Sex(Female)

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

#include <mutex>

namespace ConditionEngine {

    // A boolean property of the actor compared against an expected state
    class ActorFlagCondition : public Condition {
    public:
        using Predicate = bool (*)(RE::Actor*);

        ActorFlagCondition(std::string type, std::string description, Predicate predicate,
                           EventDependency dependencies, bool expectedState = true);

        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return dependencies_; }

    private:
        std::string type_;
        std::string description_;
        Predicate predicate_;
        EventDependency dependencies_;
        bool expectedState_;
    };

    // True when the actor matches any (or all) of a list of forms. Forms are given
    // as form strings / editor IDs and resolved lazily on first evaluation.
    class ActorFormCondition : public Condition {
    public:
        using Predicate = bool (*)(RE::Actor*, RE::TESForm*);

        ActorFormCondition(std::string type, std::string field, std::string description, Predicate predicate,
                           EventDependency dependencies, std::vector<std::string> forms, bool matchAny = true);

        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return dependencies_; }
        std::vector<std::string> Validate() const override;

    private:
        const std::vector<RE::TESForm*>& Resolve() const;

        std::string type_;
        std::string field_;
        std::string description_;
        Predicate predicate_;
        EventDependency dependencies_;
        std::vector<std::string> forms_;
        bool matchAny_;

        mutable std::once_flag resolveOnce_;
        mutable std::vector<RE::TESForm*> resolved_;
    };

    class LevelCondition : public Condition {
    public:
        LevelCondition(int level, ComparisonOperator op);

        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::ActorValue; }

    private:
        int level_;
        ComparisonOperator op_;
    };

    class SexCondition : public Condition {
    public:
        explicit SexCondition(bool female);

        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override { return female_ ? "Actor is female" : "Actor is male"; }
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override { return std::make_unique<SexCondition>(female_); }
        EventDependency GetEventDependencies() const override { return EventDependency::None; }

    private:
        bool female_;
    };

    // Bitmask of the cheap ActorState flags (sneaking, sitting, in dialogue, bleedout,
    // weapon drawn, follower). Used by the watcher's change detection.
    std::uint32_t ComputeActorStateFlags(RE::Actor* actor);

    // Registers every actor/gear type above with the registry
    class Registry;
    void RegisterActorConditionTypes(Registry& registry);

} // namespace ConditionEngine
