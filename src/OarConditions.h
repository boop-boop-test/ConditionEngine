#pragma once

// OarConditions.h - Open Animation Replacer conditions backed by ConditionEngine (DLL build only).
//
//   ConditionEngine            text = a named condition (preset, script, plugin or host rule)
//   ConditionEngineExpression  text = an expression, compiled once per distinct text
//
// OAR evaluates conditions inside the actor's animation graph update, which the engine runs on
// worker threads for most actors. ConditionEngine evaluation reads game state and must run on
// the main thread, so off the main thread a condition reads the latest result of a watch kept
// per (text, actor) on the main thread; the first read for a pair is false until that watch has
// evaluated. On the main thread the condition evaluates directly.
//
// src/OAR holds Open Animation Replacer's condition API files (github.com/ersh1/OpenAnimationReplacer),
// copied into the project as the API asks; only the module lookup uses CommonLib's REX::W32.

namespace ConditionEngine::OarConditions {

    // SKSE kPostLoad, main thread: OAR locks its condition factories after it. Registers the
    // conditions when OpenAnimationReplacer.dll is loaded, otherwise logs one line.
    void Register();

    // SKSE kPreLoadGame / kNewGame, main thread: drops every watch (FormIDs of created
    // references may name other actors in the next save).
    void OnGameLoad();

}  // namespace ConditionEngine::OarConditions
