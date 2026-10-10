#pragma once

// Settings.h - ConditionEngine.ini
//
//   [Watcher]
//   fPollIntervalSeconds = 10      ; full re-check of watches (state with no change signal)
//   fDetectIntervalSeconds = 1     ; cheap change detection (cell, hour, weather, actor flags)
//
//   [Temperature]
//   sGlobal =                      ; editor ID or form string of a global holding the temperature
//   fScale = 1.0                   ; temperature = global * fScale + fOffset
//   fOffset = 0.0
//
// Without sGlobal, Temperature conditions see a fixed 20 unless a host or plugin
// sets a provider (Registry::SetTemperatureProvider).

#include "ConditionEngine/Common.h"

#include <chrono>
#include <filesystem>

namespace ConditionEngine {

    struct Settings {
        std::chrono::milliseconds pollInterval{10000};
        std::chrono::milliseconds detectInterval{1000};
        std::string temperatureGlobal;
        float temperatureScale = 1.0f;
        float temperatureOffset = 0.0f;
    };

    // Data/SKSE/Plugins/ConditionEngine.ini
    std::filesystem::path GetDefaultSettingsPath();

    // Missing file or keys keep their defaults
    Settings LoadSettings(const std::filesystem::path& path);

    // Installs the temperature provider described by the settings (no-op without sGlobal)
    void ApplyTemperatureSettings(const Settings& settings);

} // namespace ConditionEngine
