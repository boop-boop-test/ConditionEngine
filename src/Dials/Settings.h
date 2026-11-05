#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ConditionEngine::Dials {

    // Data/SKSE/Plugins/ConditionEngine.ini, sections [Dials], [Editor], [SkyrimNet]
    struct Settings {
        int tickMs = 200;                 // [Dials] iTickMs: how often rules run (50..2000)
        double maxCatchUpHours = 48.0;    // [Dials] fMaxCatchUpHours: longest game-time gap a game-clock rule makes up
        double trackInterval = 2.0;       // [Dials] fTrackSeconds: how often "track" is re-checked per actor
        std::uint32_t editorKey = 0;      // [Editor] iHotkey: DirectInput scan code that opens the editor (0 = none)
        bool pauseGame = true;            // [Editor] bPauseGame
        bool verbose = false;             // [Dials] bVerbose: log every notified change
        bool skyrimNet = true;            // [SkyrimNet] bEnable: decorators, events, world knowledge, actions
        double skyrimNetRefresh = 1.5;    // [SkyrimNet] fRefreshSeconds: how fresh condition_engine() answers are
    };

    Settings LoadSettings();
    const Settings& GetSettings();

    std::filesystem::path DataDirectory();      // Data/SKSE/Plugins/ConditionEngine (preset files: their "dials")
    std::filesystem::path UserFile();           // Data/SKSE/Plugins/ConditionEngine/User/User.json (the editor's)

} // namespace ConditionEngine::Dials
