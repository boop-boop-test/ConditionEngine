#pragma once

// PapyrusCallCondition - reads a number returned by a Papyrus function, asynchronously.
//
//   PapyrusCall(SeverActionsNative, Native_Survival_GetHunger)               static call
//   PapyrusCall(SeverActions_Survival, GetHunger, form="0xD62|SeverActions.esp")   method on a form's script
//   PapyrusCall(MyScript, GetLevel, passActor=false, interval=5) > 3
//
// The actor is passed as the only argument unless passActor=false. Reading never
// blocks: it returns the latest result and, when that is older than `interval`
// seconds, dispatches one call (at most one in flight per function and actor).
// Results are shared by every reader of the same function and actor. Before the
// first result the value is Pending; a failed call reads Unavailable (keeping the
// last number). The VM does not run while the game is paused, so a call only times
// out after 10 s of unpaused waiting. Int, float and bool (0 / 1) results are accepted. Each result fires
// EventDependency::Value for its actor. Results are dropped on game load.

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

#include <mutex>

namespace ConditionEngine {

    class PapyrusCallCondition : public Condition {
    public:
        struct Call {
            std::string script;
            std::string function;
            std::string form;  // empty = static call
            bool passActor = true;
            double intervalSeconds = 1.0;
            std::string key;  // lowercase "form|script.function"; filled by the condition (bind at compile)

            // The key for script / function / form, lowercase
            static std::string MakeKey(const Call& call);
        };

        PapyrusCallCondition(Call call, ComparisonOperator op, double value);

        bool Evaluate(const EvaluationContext& context) const override;
        TriResult EvaluateTri(const EvaluationContext& context) const override;
        ValueResult ReadValue(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Value | EventDependency::Script; }
        std::vector<std::string> Validate() const override;

    private:
        Call call_;
        ComparisonOperator op_;
        double value_;
    };

    // Shared result cache and dispatcher behind PapyrusCall
    class PapyrusCalls {
    public:
        static PapyrusCalls& Get();

        // Main thread. Latest result for the call and actor; dispatches a refresh when due.
        ValueResult Read(const PapyrusCallCondition::Call& call, RE::Actor* actor);

        // Drops every result (game load); late results from before are ignored
        void ClearAll();

        // The next read of this call re-dispatches it whatever its interval (refreshOn).
        // `actorId` 0 = every actor's copy of the call.
        void Expire(const PapyrusCallCondition::Call& call, RE::FormID actorId);

    private:
        PapyrusCalls() = default;

        using Clock = std::chrono::steady_clock;

        struct Sample {
            std::optional<double> value;
            Clock::time_point sampledAt{};
            Clock::time_point requestedAt{};
            Clock::time_point lastObserved{};
            Clock::duration pendingFor{};  // unpaused time the current call has been waiting
            std::uint64_t request = 0;  // 0 = never requested
            bool pending = false;
            bool failed = false;
            bool expired = false;  // re-dispatch on the next read (refreshOn)
            std::string reason;
        };

        void Complete(const std::string& callKey, RE::FormID slot, std::uint64_t generation, std::uint64_t request,
                      RE::FormID actorId,
                      std::optional<double> value, std::string reason);

        std::mutex mutex_;
        // call key -> actor (0 when the call takes no actor) -> latest result
        std::unordered_map<std::string, std::unordered_map<RE::FormID, Sample>> samples_;
        std::uint64_t generation_ = 1;
        std::uint64_t nextRequest_ = 0;
    };

} // namespace ConditionEngine
