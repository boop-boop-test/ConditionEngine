#pragma once

// Bridge.h - ConditionEngine and dials in SkyrimNet (SkyrimNet public API v5+, loaded at run time;
// without SkyrimNet every call here is a no-op):
//
//   Decorators   get_dials(actorUUID)         every dial's value for that actor (an object)
//                condition_engine(actorUUID)  {"conditions": {Name: bool}, "values": {name: number}, "ready"}
//   Events       dial rules {"skyrimnet": "dialogue"} fire on SkyrimNet events about the actor
//   Memory       dial rules with "tell" register an event ("{name} had a manic spell.")
//   Knowledge    conditions with "knowledge" become world knowledge, injected while true
//   Actions      dial rules {"aiAction": "CalmDown", "description": ...} become AI actions
//
// SkyrimNet calls decorators and action eligibility on its own threads, where game state is not
// safe: they answer from a cache this bridge refreshes on the main thread for the actors SkyrimNet
// has asked about recently. A first question about an actor waits (briefly) for the main thread.

#include <nlohmann/json.hpp>

namespace ConditionEngine::SkyrimNet {

    void Install();         // main thread, after the dials started (once)
    void OnSessionStart();  // world knowledge is synced again once SkyrimNet's memory is ready
    void Tick();            // main thread, from the dials tick
    bool Available();
    nlohmann::json Status();  // for the editor: version, what is registered, last problems

} // namespace ConditionEngine::SkyrimNet
