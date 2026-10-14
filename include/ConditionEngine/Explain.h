#pragma once

// Explain.h - "Why is this true / false right now?" for one actor.
//
// Evaluates a condition or value the way the engine does (same cheapest-first order,
// same short-circuits) and returns the tree with each part's outcome:
//
//   {"expression": "$hunger > 80", "state": "false", "children": [
//       {"expression": "$hunger", "state": "ok", "value": 42, "from": "severactions_hunger", ...},
//       {"expression": "80", "state": "ok", "value": 80}]}
//
// Condition states: true, false, pending, unavailable, skipped (not evaluated: a cheaper
// sibling decided the group, or it repeats one). Value states: ok, pending, unavailable,
// error. Nodes also carry "reason" (why not true / not ok), "value" (numbers, including the
// number a numeric condition compared), "from" (the provider a combined value read) and
// "note" (folded at compile time, duplicate, ...).
//
// Main thread: it reads game state like evaluation does. Only runs when asked.

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Value.h"

namespace ConditionEngine::Explain {

    nlohmann::json ExplainCondition(const Condition& condition, const EvaluationContext& context);
    nlohmann::json ExplainValue(const Value& value, const EvaluationContext& context);

    // Indented plain text for the console and logs:
    //   [F] $hunger > 80
    //     [ok] $hunger = 42  (from severactions_hunger)
    std::string ToText(const nlohmann::json& explanation);

    // Whole numbers as is, others to 2 decimals without trailing zeros: 8.29, 0.5, 42
    std::string FormatNumber(double value);

} // namespace ConditionEngine::Explain
