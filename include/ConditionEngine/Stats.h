#pragma once

// Stats.h - Evaluation cost counters (issue 088 step 1).
//
// Always counted (a few relaxed atomics per tick):
//   watcher ticks (count, total / max / last duration), conditions evaluated, values
//   sampled, posted / drained events, availability checks (count and time),
//   PapyrusCall dispatches and outcomes.
// Counted only while enabled (SetEnabled / ConditionEngine_SetStatsEnabled), because
// it reads the clock twice per node:
//   per type: evaluations, value reads, total time (inclusive of children) and self
//   time (exclusive). Value nodes (Literal, Math, NamedValue, Truth) have their own rows.
//
// Snapshot() is JSON (see ConditionEngine_GetStats); Reset() zeroes everything.

#include "ConditionEngine/Common.h"

#include <atomic>
#include <chrono>

namespace ConditionEngine::Stats {

    // Fixed rows for nodes that are not registered types
    enum FixedSlot : std::uint32_t {
        kUnregistered = 0,  // conditions built without the registry (host make_unique)
        kLiteral,
        kMath,
        kNamedValue,
        kTruth,
        kFirstTypeSlot
    };

    bool Enabled();
    void SetEnabled(bool enabled);

    // A row for a registered type; returns kUnregistered when the table is full
    std::uint32_t RegisterSlot(std::string_view typeName);

    // Times one node evaluation or value read, inclusive and exclusive of nested nodes
    class Scope {
    public:
        Scope(std::uint32_t slot, bool read);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        std::uint32_t slot_;
        bool read_;
        std::chrono::steady_clock::time_point start_;
    };

    // Watcher and engine counters (always on)
    void RecordTick(std::chrono::steady_clock::duration duration);
    void AddConditionEvaluations(std::uint64_t count);
    void AddValueSamples(std::uint64_t count);
    void CountPostedEvent();
    void CountDrain();
    // Samples the main thread's console mode about once a second (watcher ticks, or an
    // idle check). While it is on, the game's condition functions print "GetInCell >> 0"
    // to the console for every evaluation, anyone's.
    void CountConsoleCheck(bool consoleMode);
    void RecordAvailabilityCheck(std::chrono::steady_clock::duration duration);
    // A named value or rule reference re-bound its definition (the availability revision moved)
    void CountRebind();
    enum class PapyrusOutcome { Dispatched, Completed, Failed, TimedOut };
    void CountPapyrusCall(PapyrusOutcome outcome);

    // {"enabled", "sinceMs", "watcher": {...}, "availability": {...}, "papyrusCalls": {...},
    //  "types": [{name, evaluations, reads, totalUs, selfUs}]}  (types with activity only)
    nlohmann::json Snapshot();
    void Reset();

} // namespace ConditionEngine::Stats
