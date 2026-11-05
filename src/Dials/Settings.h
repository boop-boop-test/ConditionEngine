#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ConditionEngine::Dials {

    // Data/SKSE/Plugins/ConditionEngine.ini, sections [Dials], [Editor], [SkyrimNet]
    constexpr std::uint32_t kDefaultEditorKey = 0x44;  // F10

    struct Settings {
        int tickMs = 200;                 // [Dials] iTickMs: how often rules run (50..2000)
        double maxCatchUpHours = 48.0;    // [Dials] fMaxCatchUpHours: longest game-time gap a game-clock rule makes up
        double trackInterval = 2.0;       // [Dials] fTrackSeconds: how often "track" is re-checked per actor
        // [Editor] iHotkey: the key that opens the editor, a DirectInput scan code (0x44) or a name (F10); 0 = none.
        // The player's choice in the editor (User.json "editor") wins over it.
        std::uint32_t editorKey = kDefaultEditorKey;
        std::uint32_t vrButton = 0;       // [Editor] iVRButton: a Magelight kVRButton* that opens the editor in VR (0 = none)
        std::uint32_t vrModifier = 0;     // [Editor] iVRModifier: held with it (0 = none)
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
