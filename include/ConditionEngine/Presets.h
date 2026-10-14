#pragma once

// Presets.h - Data-only named conditions loaded from JSON files.
//
// Data/SKSE/Plugins/ConditionEngine/*.json:
//   {
//     "conditions": {
//       "IsNight":      "Time(20, 6)",
//       "BadWeather":   "Weather(Rainy) || Weather(Snow)",
//       "ColdOutside":  { "type": "Logical", "operator": "And", "children": [ ... ] },
//       "VeryHungry":   "$vampire_hunger > 80"
//     },
//     "values": {
//       "light":          "Game(GetLightLevel) / 100",
//       "vampire_hunger": { "label": "Vampire hunger", "unit": "%", "range": [0, 100],
//                           "expr": "...", "enabledWhen": "...", "fallback": 0 },
//       "hungry_mood":    { "pushed": { "default": 0, "timeoutSeconds": 600 } }
//     }
//   }
//
// Conditions are expression text or JSON condition trees. Each becomes a named
// condition referenced as @IsNight / Rule("IsNight"). Values become named values
// referenced as $light (see NamedValueDef::Parse for the object form). The file
// name is recorded as the source. Presets may reference each other.

#include "ConditionEngine/Common.h"

#include <filesystem>

namespace ConditionEngine {

    // Data/SKSE/Plugins/ConditionEngine
    std::filesystem::path GetDefaultPresetDirectory();

    // Loads one preset file. Returns the number of conditions and values registered.
    std::size_t LoadPresetFile(const std::filesystem::path& file);

    // Loads every *.json file in the directory (non-recursive, sorted by name so
    // later files deterministically override earlier ones). Missing directory = 0.
    std::size_t LoadPresetDirectory(const std::filesystem::path& directory);

} // namespace ConditionEngine
