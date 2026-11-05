#pragma once

// WeatherProfile.h - What a weather is like, on one 0-100 scale per kind, from its record alone.
//
// Weather mods mostly edit the vanilla weather records (Obsidian, Cathedral, Azurite) or add hundreds of
// their own (Vivid), and their numbers don't agree: one mod's rain particle density is 1, another's 9.
// So a profile combines three signals that every weather mod keeps:
//   - the record's classification flags (Pleasant / Cloudy / Rainy / Snow), which the engine itself uses;
//   - its data: precipitation particles (type and texture: rain, snow, ash, sand), wind speed, lightning
//     frequency, fog distance, the aurora model;
//   - words in its editor ID ("SkyrimStormSnow", "TS_SolstheimDustStorm", "SkyrimOvercast_LowRain_43").
// Levels follow the survival values: 25 / 50 / 75 = light / moderate / heavy, 100 = extreme, 0 = none.
// Wind is the record's wind speed scaled to 0-100.
//
// Nothing here touches game state; WeatherProfiles.h reads records from loaded weather forms.

#include <array>
#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ConditionEngine::Weather {

    enum class Kind : std::uint8_t {
        Clear,          // fair sky: 100 clear, 50 partly cloudy
        Clouds,         // cloud cover: 50 cloudy, 75 overcast, 100 storm clouds
        Rain,           // 25 light / sun shower, 50 rain, 75 heavy, 100 extreme
        Snow,           // 25 flurries, 50 snow, 75 heavy
        Precipitation,  // rain or snow, the larger
        Thunder,        // lightning: 25 rare, 50 occasional, 75 frequent
        Storm,          // any rough weather: thunderstorm, gale-driven rain or snow, blizzard, ash or dust storm
        Blizzard,       // wind-driven snow
        Fog,            // 25 mist / haze, 50 fog, 75 thick fog
        Wind,           // wind speed, 0-100
        Ash,            // 25 ash in the air, 50 ashfall, 75 ash storm
        Sand,           // sand or dust: 25 dusty, 50 blowing, 75 dust storm
        Aurora,         // 100 when the weather shows an aurora (at night)
        kTotal
    };
    inline constexpr std::size_t kKindCount = static_cast<std::size_t>(Kind::kTotal);

    // "Rain", "Snow", ... (the names WeatherKind(...) takes)
    std::string_view KindName(Kind kind);
    // Case-insensitive, with aliases: Rainy, Raining, Snowy, Cloudy, Overcast, Stormy, Thunderstorm,
    // Lightning, Foggy, Mist, Windy, Ashfall, Dust, Sandstorm, ...
    std::optional<Kind> ParseKind(std::string_view text);

    // The fields of a weather record (WTHR) the profile reads
    struct Record {
        enum Flag : std::uint8_t { kPleasant = 1, kCloudy = 2, kRainy = 4, kSnow = 8 };
        std::uint8_t flags = 0;                // DATA classification bits
        std::uint8_t windSpeed = 0;            // 0-255
        std::uint8_t thunderFrequency = 255;   // lower = more frequent, 255 = never
        int precipitationType = -1;            // -1 none, 0 rain particles, 1 snow particles
        float precipitationDensity = 0.0f;
        std::string precipitationTexture;      // "Effects\\FXRaindrops.dds"
        float fogDayFar = 0.0f;
        float fogNightFar = 0.0f;
        std::string auroraModel;               // "Sky\\SkyrimAuroraGB.nif"; other sky models also live here
        std::string editorId;                  // "" when unknown
    };

    struct Profile {
        std::array<std::uint8_t, kKindCount> level{};
        std::array<std::string, kKindCount> why{};  // short reason per kind with a level, for explanations

        std::uint8_t Level(Kind kind) const { return level[static_cast<std::size_t>(kind)]; }
        const std::string& Why(Kind kind) const { return why[static_cast<std::size_t>(kind)]; }
        void Set(Kind kind, int value, std::string reason = {});

        // "rain 75 (classed rainy, editor ID says storm), storm 75, clouds 100, wind 20"
        std::string Describe() const;
    };

    Profile Classify(const Record& record);

    // The lowercase words of an editor ID, split at case changes, digits and underscores, plus each
    // adjacent pair joined ("Dark"+"Sky" -> "darksky"). A word that follows "No" is dropped ("NoFog").
    std::vector<std::string> EditorIdWords(std::string_view editorId);

    // Case-insensitive glob: * any run, ? one character
    bool MatchesGlob(std::string_view pattern, std::string_view text);

    // Reads the editor IDs of the weather records (WTHR group) in a plugin file. formID is the
    // record's own form ID: its top byte indexes the plugin's master list, or the plugin itself past
    // the masters. Compressed records are skipped (weather records are not compressed in practice).
    struct PluginWeatherId {
        std::uint32_t formID = 0;
        std::string editorId;
    };
    std::vector<PluginWeatherId> ReadPluginWeatherEditorIds(std::istream& in);

} // namespace ConditionEngine::Weather
