#pragma once

// RefreshOn.h - Re-sample named values when an SKSE mod event fires (issue 089 part 2).
//
//   "severactions_hunger": { "expr": "...", "refreshOn": ["SeverActions_Tick_Survival",
//                                                         "SeverActionsNative_FoodConsumed"] }
//
// When a listed event fires (SKSE ModCallbackEvent; names ignore case):
// - every PapyrusCall the value reads (directly, through other named values or its
//   enabledWhen) is re-dispatched at once, ignoring its interval;
// - watches and samplers that read values are re-sampled, for the sender actor when
//   the sender is an Actor, otherwise for every watched actor.
// Polling intervals then only act as a fallback.

#include "ConditionEngine/Common.h"

namespace ConditionEngine::RefreshOn {

    // Registers the ModCallbackEvent sink. Idempotent; called by Watcher::Install.
    void Install();

    // Handles one mod event (the sink calls this; hosts and tests may too).
    // `sender` null or not an actor = every watched actor. Returns the number of
    // named values the event refreshes.
    std::size_t OnModEvent(std::string_view eventName, RE::TESForm* sender);

} // namespace ConditionEngine::RefreshOn
