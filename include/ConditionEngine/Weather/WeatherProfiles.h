#pragma once

// WeatherProfiles.h - Profiles of the loaded weather forms (see WeatherProfile.h), cached per form.
//
// Editor IDs: the game drops weather editor IDs after loading. When another plugin keeps them
// (powerofthree's Tweaks) they're used as they are; otherwise the WTHR records of every loaded plugin
// are read once from Data/, and a weather takes the editor ID from the last plugin that edits it.
//
// Preset files can correct a weather's profile ("weathers" section, docs/weather.md):
//   "weathers": {
//     "TS_*_Heavy":                 {"fog": 75},           editor ID glob
//     "0x10A241|Skyrim.esm":        {"rain": 50, "storm": 0}
//   }
// Later files win; a listed kind replaces the computed level, unlisted kinds stay.

#include "ConditionEngine/Common.h"
#include "ConditionEngine/Weather/WeatherProfile.h"

namespace ConditionEngine::Weather {

    // The record fields of a loaded weather
    Record ReadRecord(const RE::TESWeather* weather);

    // The weather's editor ID, or "" when it can't be found
    std::string EditorIdOf(const RE::TESWeather* weather);

    // Reads the plugins' weather editor IDs now, on a worker thread, so the first weather read doesn't
    // wait for it (kDataLoaded). Reads made before it finishes wait for it. Safe to call more than once.
    void PrimeEditorIds();

    // The weather's profile with preset overrides applied, cached. Null for a null weather. Any thread.
    std::shared_ptr<const Profile> ProfileOf(const RE::TESWeather* weather);

    // "SkyrimStormSnow (0x000C8221, Skyrim.esm)"
    std::string NameOf(const RE::TESWeather* weather);

    // Reads a preset's "weathers" section. Returns the number of entries kept; problems go to `warnings`.
    std::size_t AddOverrides(const nlohmann::json& section, const std::string& source,
                             std::vector<std::string>* warnings = nullptr);

    // Applies the overrides that match this editor ID or form ID (what ProfileOf does after Classify)
    void ApplyOverrides(Profile& profile, std::string_view editorId, std::uint32_t formID);

    // Removes every override (tests; a host reloading its presets)
    void ClearOverrides();

    // Drops cached profiles (after overrides change or a plugin edits weather records at runtime)
    void ClearCache();

} // namespace ConditionEngine::Weather
