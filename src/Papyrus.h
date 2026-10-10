#pragma once

// Papyrus.h - Papyrus native registration for ConditionEngine.dll

namespace ConditionEngine::Papyrus {

    // Registers the ConditionEngine script's native functions
    void Register();

    // Drops script-created watches and named conditions. Scripts re-register them
    // on load (OnPlayerLoadGame), so they must not leak across saves.
    void ClearScriptState();

} // namespace ConditionEngine::Papyrus
