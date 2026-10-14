#pragma once

// FirstMatchList.h - First-match evaluation of an ordered rule list (issue 090).
//
// Rules are in priority order; the winner is the first rule that is true (-1: none).
// After a change, only what can move the result is evaluated:
//   1. rules above the winner whose dependencies (or value keys) the change touches, in
//      order - the first true one becomes the winner;
//   2. otherwise the winner, if the change touches it;
//   3. only if the winner turned false, the rules below it, top down.
// A pending rule above the winner holds the current winner, so the result does not flip
// while something loads; an unavailable rule counts as not matching. Polled rules count
// as changed on a full poll (EvaluateAll).
//
// Not thread-safe: the Watcher evaluates lists on the main thread.

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"

#include <set>

namespace ConditionEngine {

    class FirstMatchList {
    public:
        explicit FirstMatchList(std::vector<std::shared_ptr<const Condition>> rules);

        // Evaluates top down from scratch. Returns the winner.
        int EvaluateAll(const EvaluationContext& context);

        // Re-evaluates what a change can affect. `keys` (may be null) are the value keys the
        // change carries ("$hunger", "call:..."); `changed` its dependency bits.
        int EvaluateChanged(const EvaluationContext& context, EventDependency changed,
                            const std::set<std::string>* keys = nullptr);

        int Winner() const { return winner_; }
        std::size_t Size() const { return rules_.size(); }
        // Last known state of each rule (rules never evaluated read False)
        const std::vector<TriResult>& States() const { return states_; }
        const std::vector<bool>& Evaluated() const { return evaluated_; }

        // What the list listens to: rules 0..winner (all when there is none)
        EventDependency ListenedDependencies() const;
        bool ListensToKeys(const std::set<std::string>& keys) const;

        // Recomputes per-rule dependencies and value keys (after redefinitions)
        void RefreshDependencies();

        // Rule evaluations so far (for tests and stats)
        std::uint64_t EvaluationCount() const { return evaluations_; }

    private:
        TriResult Evaluate(std::size_t index, const EvaluationContext& context);
        bool Touched(std::size_t index, EventDependency changed, const std::set<std::string>* keys) const;
        int ScanFrom(std::size_t first, const EvaluationContext& context);

        std::vector<std::shared_ptr<const Condition>> rules_;
        std::vector<EventDependency> dependencies_;
        std::vector<std::set<std::string>> keys_;
        std::vector<TriResult> states_;
        std::vector<bool> evaluated_;
        int winner_ = -1;
        std::uint64_t evaluations_ = 0;
    };

    // Whether a change (bits + optional value keys) touches something that depends on `deps`
    // and reads `readKeys`. A keyed Value change touches only readers of those keys, plus
    // anything that depends on Value without readable keys (host types).
    bool ChangeTouches(EventDependency deps, const std::set<std::string>& readKeys, EventDependency changed,
                       const std::set<std::string>* changedKeys);

    // Value keys a condition or value tree reads: "$name" for named values (following
    // their definitions, enabledWhen and providers) and "call:<key>" for PapyrusCalls,
    // through engine-owned rule references. Lowercase.
    std::set<std::string> CollectValueKeys(const nlohmann::json& tree);

} // namespace ConditionEngine
