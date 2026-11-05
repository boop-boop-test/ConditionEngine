// WeatherTests.cpp - Out-of-game tests for weather profiles (docs/weather.md): the classifier over real
// weather records from vanilla and the popular weather mods, editor ID words, globs, preset overrides,
// the plugin reader, and the WeatherKind type and built-in weather preset. Nothing here touches game state.

#include "ConditionEngine/ConditionEngine.h"
#include "ConditionEngine/Expression.h"
#include "ConditionEngine/Presets.h"
#include "ConditionEngine/Weather/WeatherProfile.h"
#include "ConditionEngine/Weather/WeatherProfiles.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace {
    using json = nlohmann::json;
    using namespace ConditionEngine;
    using Weather::Kind;

    using CheckFn = void (*)(bool ok, const std::string& what);
    CheckFn Check = nullptr;

    // One weather record as the plugin has it (the winning override's fields)
    struct Fixture {
        const char* editorId;
        const char* plugin;
        std::uint8_t flags;
        std::uint8_t wind;
        std::uint8_t thunder;
        int precipType;
        float density;
        const char* texture;
        float fogDay;
        float fogNight;
        const char* aurora;
        std::vector<std::pair<Kind, int>> expect;  // kinds not listed (except Wind) must read 0
    };

    Weather::Record ToRecord(const Fixture& f) {
        Weather::Record r;
        r.flags = f.flags;
        r.windSpeed = f.wind;
        r.thunderFrequency = f.thunder;
        r.precipitationType = f.precipType;
        r.precipitationDensity = f.density;
        r.precipitationTexture = f.texture;
        r.fogDayFar = f.fogDay;
        r.fogNightFar = f.fogNight;
        r.auroraModel = f.aurora;
        r.editorId = f.editorId;
        return r;
    }

    bool HasWord(const std::vector<std::string>& words, const std::string& w) {
        return std::find(words.begin(), words.end(), w) != words.end();
    }

    void Append(std::string& out, const void* data, std::size_t size) { out.append(static_cast<const char*>(data), size); }
    void AppendU32(std::string& out, std::uint32_t v) { Append(out, &v, 4); }
    void AppendU16(std::string& out, std::uint16_t v) { Append(out, &v, 2); }

    std::string Record(const char* type, std::uint32_t flags, std::uint32_t formID, const std::string& body) {
        std::string out(type, 4);
        AppendU32(out, static_cast<std::uint32_t>(body.size()));
        AppendU32(out, flags);
        AppendU32(out, formID);
        AppendU32(out, 0);
        AppendU32(out, 0);
        return out + body;
    }
    std::string Sub(const char* type, const std::string& data) {
        std::string out(type, 4);
        AppendU16(out, static_cast<std::uint16_t>(data.size()));
        return out + data;
    }
    std::string Group(const char* label, const std::string& records) {
        std::string out("GRUP", 4);
        AppendU32(out, static_cast<std::uint32_t>(24 + records.size()));
        out.append(label, 4);
        AppendU32(out, 0);  // group type: top
        AppendU32(out, 0);
        AppendU32(out, 0);
        return out + records;
    }
    std::string Z(const char* s) { return std::string(s) + '\0'; }
}

void RunWeatherTests(CheckFn check) {
    Check = check;

    // ── Kinds ────────────────────────────────────────────────────────────────
    Check(Weather::ParseKind("rain") == Kind::Rain && Weather::ParseKind("Rainy") == Kind::Rain, "kind names and aliases");
    Check(Weather::ParseKind("Overcast") == Kind::Clouds && Weather::ParseKind("Thunderstorm") == Kind::Thunder &&
              Weather::ParseKind("Sandstorm") == Kind::Sand && Weather::ParseKind("ashfall") == Kind::Ash,
          "kind aliases: overcast, thunderstorm, sandstorm, ashfall");
    Check(!Weather::ParseKind("Hail"), "unknown kind");
    for (std::size_t i = 0; i < Weather::kKindCount; ++i) {
        const auto kind = static_cast<Kind>(i);
        Check(Weather::ParseKind(Weather::KindName(kind)) == kind, "kind name round trip: " + std::string(Weather::KindName(kind)));
    }

    // ── Editor ID words ──────────────────────────────────────────────────────
    {
        auto w = Weather::EditorIdWords("SkyrimOvercast_HighRain_Thunder_43");
        Check(HasWord(w, "overcast") && HasWord(w, "high") && HasWord(w, "rain") && HasWord(w, "thunder") &&
                  HasWord(w, "highrain") && HasWord(w, "43"),
              "words of SkyrimOvercast_HighRain_Thunder_43");
        w = Weather::EditorIdWords("SkyrimFogRIFTENWind");
        Check(HasWord(w, "fog") && HasWord(w, "riften") && HasWord(w, "wind"), "all-caps run then a word: RIFTEN, Wind");
        w = Weather::EditorIdWords("CoTAsh_HS");
        Check(HasWord(w, "ash") && HasWord(w, "hs") && HasWord(w, "co"), "CoTAsh_HS: co, t, ash, hs");
        w = Weather::EditorIdWords("SkyrimSnowBlizzard_Down_NoFog1");
        Check(!HasWord(w, "fog") && HasWord(w, "blizzard"), "NoFog drops fog");
        w = Weather::EditorIdWords("DLC02VolcanicAshStorm01");
        Check(HasWord(w, "volcanic") && HasWord(w, "ash") && HasWord(w, "storm") && HasWord(w, "ashstorm"),
              "DLC02VolcanicAshStorm01 words");
    }

    // ── Globs ────────────────────────────────────────────────────────────────
    Check(Weather::MatchesGlob("TS_*_Heavy", "TS_SkyrimFogMA_Heavy"), "glob *");
    Check(Weather::MatchesGlob("skyrimstorm*", "SkyrimStormRainTU"), "glob is case-insensitive");
    Check(Weather::MatchesGlob("Skyrim?lear", "SkyrimClear") && !Weather::MatchesGlob("Skyrim?lear", "SkyrimCloudy"), "glob ?");
    Check(Weather::MatchesGlob("SkyrimFog", "SkyrimFog") && !Weather::MatchesGlob("SkyrimFog", "SkyrimFogRE"), "glob without wildcards is exact");
    Check(!Weather::MatchesGlob("*Storm", ""), "glob never matches an unknown editor ID");

    // ── Classifier, on real records ──────────────────────────────────────────
    const std::vector<Fixture> fixtures = {
        // Vanilla
        {"SkyrimClear", "Skyrim.esm", 1, 25, 255, -1, 0, "", 80000, 40000, "", {{Kind::Clear, 100}}},
        {"SkyrimClear_A", "Skyrim.esm", 1, 25, 255, -1, 0, "", 80000, 40000, R"(Sky\SkyrimAuroraRY.nif)",
         {{Kind::Clear, 100}, {Kind::Aurora, 100}}},
        {"SkyrimCloudy", "Skyrim.esm", 1, 38, 255, -1, 0, "", 100000, 50000, "", {{Kind::Clear, 50}, {Kind::Clouds, 50}}},
        {"SkyrimFog", "Skyrim.esm", 2, 13, 255, -1, 0, "", 25000, 15000, "", {{Kind::Fog, 50}, {Kind::Clouds, 75}}},
        {"SkyrimOvercastRain", "Skyrim.esm", 4, 25, 255, 0, 1, R"(Effects\FXRaindrops.dds)", 15000, 15000, "",
         {{Kind::Rain, 50}, {Kind::Precipitation, 50}, {Kind::Clouds, 75}}},
        {"SkyrimStormRain", "Skyrim.esm", 4, 50, 246, 0, 2, R"(Effects\FXRaindrops.dds)", 25000, 10000, "",
         {{Kind::Rain, 75}, {Kind::Precipitation, 75}, {Kind::Thunder, 25}, {Kind::Storm, 75}, {Kind::Clouds, 100}}},
        {"SkyrimOvercastSnow", "Skyrim.esm", 8, 76, 255, 1, 3, R"(Effects\FXSnowSoftSub.dds)", 50000, 35000, "",
         {{Kind::Snow, 50}, {Kind::Precipitation, 50}, {Kind::Clouds, 75}}},
        {"SkyrimStormSnow", "Skyrim.esm", 8, 178, 203, 1, 6, R"(Effects\FXSnowSoftSub.dds)", 20000, 20000, "",
         {{Kind::Snow, 75}, {Kind::Precipitation, 75}, {Kind::Blizzard, 75}, {Kind::Storm, 75}, {Kind::Clouds, 100}}},
        {"FXMagicStormRain", "Skyrim.esm", 4, 122, 15, 0, 3, R"(Effects\FXRaindrops.dds)", 10000, 10000, "",
         {{Kind::Rain, 75}, {Kind::Precipitation, 75}, {Kind::Thunder, 75}, {Kind::Storm, 100}, {Kind::Clouds, 100}}},
        {"SovngardeClear", "Skyrim.esm", 17, 0, 255, 1, 90, R"(Effects\IceShards01.dds)", 30000, 30000, R"(Sky\SovngardeSky.nif)",
         {{Kind::Clear, 100}}},  // ice-shard sparkles are not snow; Sovngarde's sky model is not an aurora
        // Dragonborn
        {"DLC02VolcanicAsh01", "Dragonborn.esm", 2, 43, 255, -1, 0, "", 60000, 15000, "", {{Kind::Ash, 25}, {Kind::Clouds, 75}}},
        {"DLC02VolcanicAshStorm01", "Dragonborn.esm", 8, 18, 63, 1, 1, R"(DLC02\Effects\FXAshSoft01.dds)", 60000, 15000, "",
         {{Kind::Ash, 75}, {Kind::Storm, 75}, {Kind::Clouds, 100}}},  // ash particles: not snow, no lightning
        // Obsidian: light snow particles in a weather classed pleasant
        {"SkyrimCloudyMA", "Obsidian Weathers.esp", 1, 100, 255, 1, 1, R"(Effects\FXSnowSoftSub.dds)", 20000, 20000, "",
         {{Kind::Snow, 25}, {Kind::Precipitation, 25}, {Kind::Clouds, 75}}},
        // True Storms
        {"TS_SolstheimDustStorm", "TrueStormsSE.esp", 8, 220, 63, 1, 0.25f, R"(DLC02\Effects\FXAshSoft01.dds)", 850, 850, "",
         {{Kind::Sand, 75}, {Kind::Storm, 75}, {Kind::Clouds, 100}}},
        {"TS_SkyrimFog_Heavy", "TrueStormsSE.esp", 2, 13, 255, -1, 0, "", 16000, 11000, "", {{Kind::Fog, 75}, {Kind::Clouds, 75}}},
        // Vivid
        {"SkyrimSnow_HS", "Vivid WeathersSE.esp", 40, 26, 255, 1, 20, R"(Effects\FXSnowSoftSub.dds)", 40000, 40000, "",
         {{Kind::Snow, 75}, {Kind::Precipitation, 75}, {Kind::Clouds, 75}}},  // heavy snow, calm: not a blizzard
        {"SkyrimSnowBlizzard_Down_NoFog1", "Vivid WeathersSE.esp", 8, 217, 255, 1, 9, R"(Effects\FXSnowSoftSub.dds)", 4000, 4000, "",
         {{Kind::Snow, 75}, {Kind::Precipitation, 75}, {Kind::Blizzard, 75}, {Kind::Storm, 75}, {Kind::Clouds, 100}}},
        {"SkyrimOvercast_LowRain_Thunder_46", "Vivid WeathersSE.esp", 4, 26, 246, 0, 9, R"(Effects\fxraindrops-vanilla.dds)",
         110000, 110000, "",
         {{Kind::Rain, 25}, {Kind::Precipitation, 25}, {Kind::Thunder, 50}, {Kind::Storm, 75}, {Kind::Clouds, 100}}},
        {"SkyrimClear_Snow_54", "Vivid WeathersSE.esp", 1, 25, 255, -1, 0, "", 180000, 180000, "",
         {{Kind::Clear, 100}}},  // "Snow" names the region, not the weather
        // Aequinoctium: ash-textured motes drifting in a fog
        {"RiftenOvercastFog", "Aequinoctium.esp", 2, 13, 255, 1, 1, R"(DLC02\Effects\FXAshSoft01.dds)", 50000, 8000, "",
         {{Kind::Fog, 50}, {Kind::Clouds, 75}}},
        // Rustic: a sun shower
        {"RusticSunnyRain", "Rustic Weathers And Lighting.esp", 4, 38, 255, 0, 7, R"(Effects\FXRaindrops.dds)", 120000, 30000, "",
         {{Kind::Rain, 25}, {Kind::Precipitation, 25}, {Kind::Clouds, 75}}},
        // Cathedral: an aurora model on a rainy sky shows nothing
        {"SkyrimOvercastRainFF", "Cathedral Weathers.esp", 4, 85, 255, 0, 2, R"(Effects\Cat_FXRaindrops.dds)", 5000, 4000,
         R"(Sky\SkyrimAuroraRY.nif)", {{Kind::Rain, 50}, {Kind::Precipitation, 50}, {Kind::Clouds, 75}}},
        // Azurite: rare lightning in its overcast rain
        {"SkyrimOvercastRain", "Azurite Weathers III.esp", 4, 140, 253, 0, 12, R"(Effects\Cat_FXRaindrops.dds)", 320000, 320000, "",
         {{Kind::Rain, 50}, {Kind::Precipitation, 50}, {Kind::Thunder, 25}, {Kind::Clouds, 75}}},
    };
    for (const auto& f : fixtures) {
        const auto profile = Weather::Classify(ToRecord(f));
        const std::string name = std::string(f.editorId) + " (" + f.plugin + ")";
        for (std::size_t i = 0; i < Weather::kKindCount; ++i) {
            const auto kind = static_cast<Kind>(i);
            if (kind == Kind::Wind) continue;
            int expected = 0;
            for (const auto& [k, level] : f.expect) {
                if (k == kind) expected = level;
            }
            Check(profile.Level(kind) == expected, name + ": " + std::string(Weather::KindName(kind)) + " " +
                                                       std::to_string(profile.Level(kind)) + ", expected " +
                                                       std::to_string(expected) + " [" + profile.Describe() + "]");
        }
        Check(profile.Level(Kind::Wind) == static_cast<int>(std::lround(f.wind * 100.0 / 255.0)), name + ": wind scaled to 0-100");
    }
    {
        Weather::Record calm;
        calm.flags = Weather::Record::kPleasant;
        auto p = Weather::Classify(calm);
        Check(p.Level(Kind::Clear) == 100 && p.Why(Kind::Clear) == "classed pleasant" && p.Why(Kind::Rain).empty(),
              "reasons are kept for the kinds that have a level");
        Check(p.Describe().find("clear 100 (classed pleasant)") != std::string::npos, "Describe: " + p.Describe());
    }

    // ── Preset overrides ─────────────────────────────────────────────────────
    {
        Weather::ClearOverrides();
        std::vector<std::string> warnings;
        const auto kept = Weather::AddOverrides(
            json::parse(R"({"TS_*_Heavy": {"fog": 50, "Storm": 75}, "Bad": {"hail": 1}, "Worse": 3})"), "test.json", &warnings);
        Check(kept == 1 && warnings.size() == 2, "overrides: one kept, two warnings (" + std::to_string(warnings.size()) + ")");
        Check(std::any_of(warnings.begin(), warnings.end(), [](const auto& w) { return w.find("hail") != std::string::npos; }),
              "an unknown kind is named in the warning");

        auto profile = Weather::Classify(ToRecord(fixtures[14]));  // TS_SkyrimFog_Heavy
        Weather::ApplyOverrides(profile, "TS_SkyrimFog_Heavy", 0);
        Check(profile.Level(Kind::Fog) == 50 && profile.Level(Kind::Storm) == 75 && profile.Level(Kind::Clouds) == 75,
              "override replaces the listed kinds and keeps the rest: " + profile.Describe());
        Check(profile.Why(Kind::Fog) == "preset test.json", "override reason names the preset: " + profile.Why(Kind::Fog));

        auto other = Weather::Classify(ToRecord(fixtures[3]));  // SkyrimFog
        Weather::ApplyOverrides(other, "SkyrimFog", 0);
        Check(other.Level(Kind::Fog) == 50 && other.Level(Kind::Storm) == 0, "override leaves other weathers alone");

        Check(LoadPresetJson(json::parse(R"({"weathers": {"SkyrimFog": {"fog": 75}}})"), "weathers-only.json") == 1,
              "a preset with only a weathers section loads");
        auto fog = Weather::Classify(ToRecord(fixtures[3]));
        Weather::ApplyOverrides(fog, "SkyrimFog", 0);
        Check(fog.Level(Kind::Fog) == 75, "preset weathers section applies");
        Weather::ClearOverrides();
    }

    // ── Plugin reader ────────────────────────────────────────────────────────
    {
        std::string plugin = Record("TES4", 1, 0, Sub("HEDR", std::string(12, '\0')) + Sub("MAST", Z("Skyrim.esm")));
        plugin += Group("GMST", Record("GMST", 0, 0x01000801, Sub("EDID", Z("fTest"))));
        plugin += Group("WTHR", Record("WTHR", 0, 0x0000081A, Sub("EDID", Z("SkyrimClear")) + Sub("DATA", std::string(20, '\0'))) +
                                    Record("WTHR", 0x00040000, 0x01000802, std::string(10, 'z')) +  // compressed: skipped
                                    Record("WTHR", 0, 0x01000803, Sub("EDID", Z("MyMod_Sandstorm"))));
        plugin += Group("LCTN", Record("LCTN", 0, 0x01000804, Sub("EDID", Z("MyLocation"))));
        std::istringstream in(plugin);
        const auto ids = Weather::ReadPluginWeatherEditorIds(in);
        Check(ids.size() == 2, "plugin reader: two uncompressed weather records, got " + std::to_string(ids.size()));
        if (ids.size() == 2) {
            Check(ids[0].formID == 0x0000081A && ids[0].editorId == "SkyrimClear", "an override of a master's weather");
            Check(ids[1].formID == 0x01000803 && ids[1].editorId == "MyMod_Sandstorm", "the plugin's own weather");
        }
        std::istringstream notPlugin("not a plugin file at all");
        Check(Weather::ReadPluginWeatherEditorIds(notPlugin).empty(), "plugin reader: not a plugin");
    }

    // ── WeatherKind and the built-in weather preset ──────────────────────────
    {
        Expression::ParseError error;
        auto parsed = Expression::ParseToJson("WeatherKind(Rain) >= 75", &error);
        Check(parsed && (*parsed)["type"] == "WeatherKind" && (*parsed)["kind"] == "Rain" && (*parsed)["minLevel"] == 75,
              "WeatherKind(Rain) >= 75 parses: " + (parsed ? parsed->dump() : error.message));

        std::string compileError;
        auto bare = Expression::CompileAny("WeatherKind(Storm)", &compileError);
        Check(bare != nullptr, "WeatherKind(Storm) compiles: " + compileError);
        if (bare) {
            const auto j = bare->ToJson();
            Check(j["minLevel"] == 25 && j["operator"] == ComparisonOpToName(ComparisonOperator::GreaterEqual),
                  "a bare WeatherKind is >= 25: " + j.dump());
            const auto again = Expression::CompileAny(Expression::ToExpression(*bare), &compileError);
            Check(again && again->ToJsonComplete() == bare->ToJsonComplete(),
                  "WeatherKind prints and recompiles: " + Expression::ToExpression(*bare));
        }
        Check(Expression::CompileAny("WeatherKind(Hail)", &compileError) == nullptr, "an unknown kind does not compile");

        EvaluationContext context(nullptr);  // no game: no current weather
        auto value = Expression::CompileValueAny("WeatherKind(Rain)", &compileError);
        Check(value != nullptr, "WeatherKind(Rain) compiles as a value: " + compileError);
        if (value) {
            auto r = value->Read(context);
            Check(r.status == ValueStatus::Unavailable && r.reason.find("weather") != std::string::npos,
                  "no weather: unavailable (" + r.reason + ")");
        }
        auto weatherFoggy = Expression::CompileAny("Weather(Foggy)", &compileError);
        Check(weatherFoggy && !weatherFoggy->Evaluate(context), "Weather(Foggy) without a weather is false");

        auto& registry = Registry::Get();
        const char* values[] = {"weather_clear", "weather_clouds", "weather_rain", "weather_snow", "weather_precipitation",
                                "weather_thunder", "weather_storm", "weather_blizzard", "weather_fog", "weather_wind",
                                "weather_ash", "weather_sand", "weather_aurora", "season"};
        for (const char* name : values) {
            auto def = registry.FindNamedValue(name);
            Check(def != nullptr && def->value != nullptr, std::string("built-in value $") + name);
            if (!def || !def->value) continue;
            for (const auto& problem : def->value->Validate()) Check(false, std::string("$") + name + ": " + problem);
            Check(!def->value->ReadsActor(), std::string("$") + name + " is the same for every actor");
        }
        const char* conditions[] = {
            "WeatherClear", "WeatherCloudy", "WeatherOvercast", "WeatherRain", "WeatherHeavyRain", "WeatherSnow",
            "WeatherHeavySnow", "WeatherPrecipitation", "WeatherStorm", "WeatherThunder", "WeatherBlizzard", "WeatherMist",
            "WeatherFog", "WeatherThickFog", "WeatherWindy", "WeatherGale", "WeatherAsh", "WeatherAshStorm", "WeatherDust",
            "WeatherSandstorm", "WeatherAurora", "WeatherBad", "OutsideInRain", "OutsideInSnow", "OutsideInStorm",
            "OutsideInBadWeather", "SeasonWinter", "SeasonSpring", "SeasonSummer", "SeasonAutumn"};
        for (const char* name : conditions) Check(registry.FindNamed(name) != nullptr, std::string("built-in condition @") + name);

        value = Expression::CompileValueAny("$weather_rain", &compileError);
        if (value) {
            auto r = value->Read(context);
            Check(!r.Ok() && r.value == 0, "no weather: $weather_rain is not OK and reads its fallback 0");
        }
        auto raining = Expression::CompileAny("@WeatherRain", &compileError);
        Check(raining && !raining->Evaluate(context), "no weather: @WeatherRain is false");
    }
}
