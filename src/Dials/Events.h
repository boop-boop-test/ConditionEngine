#pragma once

// Events.h - Game events into the engine: hits, combat, deaths, equipment, spells,
// effects, activation, items, locations, sleep, menus, the player's animation events,
// skill increases, mod events and keys. Each is handed to the main thread, then to the
// rules that listen for it.

namespace ConditionEngine::Dials::Events {

    void Install();          // kDataLoaded
    void OnSessionStart();   // re-attaches the player's animation graph sink
    RE::FormID Crosshair();  // the actor under the crosshair (0 = none)

} // namespace ConditionEngine::Dials::Events
