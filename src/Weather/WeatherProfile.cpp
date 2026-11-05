#include "ConditionEngine/Weather/WeatherProfile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <unordered_set>

namespace ConditionEngine::Weather {

    namespace {
        constexpr std::array<std::string_view, kKindCount> kKindNames = {
            "Clear", "Clouds", "Rain", "Snow", "Precipitation", "Thunder", "Storm",
            "Blizzard", "Fog", "Wind", "Ash", "Sand", "Aurora"};

        char Lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

        std::string ToLower(std::string_view text) {
            std::string out(text);
            std::transform(out.begin(), out.end(), out.begin(), Lower);
            return out;
        }

        bool Contains(const std::string& haystack, std::string_view needle) {
            return haystack.find(needle) != std::string::npos;
        }

        using WordSet = std::unordered_set<std::string>;

        // Editor ID vocabulary, from the weather mods' own naming (vanilla, Obsidian, Cathedral, Azurite,
        // Rustic, Aequinoctium, True Storms, Climates of Tamriel, Vivid)
        bool Any(const WordSet& words, std::initializer_list<std::string_view> list) {
            for (auto w : list) {
                if (words.contains(std::string(w))) return true;
            }
            return false;
        }
        bool FogWord(const WordSet& w) { return Any(w, {"fog", "foggy", "fogs"}); }
        bool MistWord(const WordSet& w) { return Any(w, {"mist", "misty", "haze", "hazy", "smog"}); }
        bool StormWord(const WordSet& w) {
            return Any(w, {"storm", "storms", "stormy", "tempest", "rainstorm", "snowstorm", "thunderstorm",
                           "duststorm", "sandstorm", "ashstorm", "hurricane", "gale"});
        }
        bool BlizzardWord(const WordSet& w) { return Any(w, {"blizzard", "blizzards", "whiteout"}); }
        bool ThunderWord(const WordSet& w) { return Any(w, {"thunder", "thunderstorm", "lightning", "electric"}); }
        bool AshWord(const WordSet& w) { return Any(w, {"ash", "ashen", "ashes", "ashfall", "ashstorm", "volcanic", "ashy"}); }
        bool SandWord(const WordSet& w) { return Any(w, {"sand", "sandy", "sandstorm", "dust", "dusty", "duststorm"}); }
        bool AuroraWord(const WordSet& w) { return Any(w, {"aurora", "auroras"}); }
        bool CloudyWord(const WordSet& w) { return Any(w, {"cloudy", "clouds"}); }
        bool OvercastWord(const WordSet& w) { return Any(w, {"overcast", "gloomy", "grey", "gray", "darksky", "dark"}); }
        bool LightWord(const WordSet& w) { return Any(w, {"light", "lite", "low", "ls", "drizzle", "sunny", "weak"}); }
        bool HeavyWord(const WordSet& w) {
            return Any(w, {"heavy", "high", "hs", "pouring", "downpour", "extreme", "thick", "dense", "strong", "intense"});
        }

        // Particle textures: vanilla rain / snow, Dragonborn's ash (FXAshSoft01), and the debris and
        // sparkle effects some weathers use (pine needles, grass, Sovngarde's ice shards, embers)
        bool DebrisTexture(const std::string& tex) {
            for (auto s : {"pine", "leaf", "leaves", "grass", "iceshard", "ember"}) {
                if (Contains(tex, s)) return true;
            }
            return false;
        }

        std::uint32_t ReadU32(const char* p) {
            std::uint32_t v;
            std::memcpy(&v, p, 4);
            return v;
        }
        std::uint16_t ReadU16(const char* p) {
            std::uint16_t v;
            std::memcpy(&v, p, 2);
            return v;
        }
    }

    std::string_view KindName(Kind kind) {
        const auto i = static_cast<std::size_t>(kind);
        return i < kKindCount ? kKindNames[i] : std::string_view{};
    }

    std::optional<Kind> ParseKind(std::string_view text) {
        const auto s = ToLower(text);
        for (std::size_t i = 0; i < kKindCount; ++i) {
            if (s == ToLower(kKindNames[i])) return static_cast<Kind>(i);
        }
        static const std::pair<std::string_view, Kind> kAliases[] = {
            {"fair", Kind::Clear},           {"sunny", Kind::Clear},
            {"cloudy", Kind::Clouds},        {"overcast", Kind::Clouds},      {"cloud", Kind::Clouds},
            {"rainy", Kind::Rain},           {"raining", Kind::Rain},
            {"snowy", Kind::Snow},           {"snowing", Kind::Snow},
            {"precipitating", Kind::Precipitation},
            {"thunderstorm", Kind::Thunder}, {"lightning", Kind::Thunder},
            {"stormy", Kind::Storm},
            {"foggy", Kind::Fog},            {"mist", Kind::Fog},             {"misty", Kind::Fog},
            {"windy", Kind::Wind},
            {"ashfall", Kind::Ash},          {"ashstorm", Kind::Ash},
            {"dust", Kind::Sand},            {"dusty", Kind::Sand},           {"sandstorm", Kind::Sand},
            {"duststorm", Kind::Sand},
            {"auroras", Kind::Aurora},
        };
        for (const auto& [alias, kind] : kAliases) {
            if (s == alias) return kind;
        }
        return std::nullopt;
    }

    void Profile::Set(Kind kind, int value, std::string reason) {
        const auto i = static_cast<std::size_t>(kind);
        level[i] = static_cast<std::uint8_t>(std::clamp(value, 0, 100));
        why[i] = level[i] ? std::move(reason) : std::string{};
    }

    std::string Profile::Describe() const {
        std::string out;
        for (std::size_t i = 0; i < kKindCount; ++i) {
            if (!level[i] && static_cast<Kind>(i) != Kind::Wind) continue;
            if (!out.empty()) out += ", ";
            out += ToLower(kKindNames[i]) + " " + std::to_string(level[i]);
            if (!why[i].empty()) out += " (" + why[i] + ")";
        }
        return out;
    }

    std::vector<std::string> EditorIdWords(std::string_view editorId) {
        // Runs of: an all-caps word not followed by lowercase ("RIFTEN", "TU"), a capitalised or
        // lowercase word ("Storm", "x"), digits
        std::vector<std::string> parts;
        std::size_t i = 0;
        const std::size_t n = editorId.size();
        auto isUpper = [](char c) { return c >= 'A' && c <= 'Z'; };
        auto isLowerC = [](char c) { return c >= 'a' && c <= 'z'; };
        auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
        while (i < n) {
            const char c = editorId[i];
            std::size_t j = i;
            if (isDigit(c)) {
                while (j < n && isDigit(editorId[j])) ++j;
            } else if (isUpper(c)) {
                while (j < n && isUpper(editorId[j])) ++j;
                const bool lowerNext = j < n && isLowerC(editorId[j]);
                if (lowerNext && j - i == 1) {
                    while (j < n && isLowerC(editorId[j])) ++j;  // "Storm"
                } else if (lowerNext) {
                    --j;  // "RIFTENWind": "RIFTEN", then "Wind"
                }
            } else if (isLowerC(c)) {
                while (j < n && isLowerC(editorId[j])) ++j;
            } else {
                ++i;
                continue;
            }
            parts.push_back(ToLower(editorId.substr(i, j - i)));
            i = j;
        }

        std::vector<std::string> words = parts;
        for (std::size_t k = 0; k + 1 < parts.size(); ++k) words.push_back(parts[k] + parts[k + 1]);

        // "NoFog": the weather lacks it
        std::vector<std::string> negated;
        for (const auto& w : words) {
            if (w.size() > 2 && w.starts_with("no")) negated.push_back(w.substr(2));
        }
        std::erase_if(words, [&](const std::string& w) {
            return std::find(negated.begin(), negated.end(), w) != negated.end() &&
                   std::find(words.begin(), words.end(), "no" + w) != words.end();
        });
        std::sort(words.begin(), words.end());
        words.erase(std::unique(words.begin(), words.end()), words.end());
        return words;
    }

    bool MatchesGlob(std::string_view pattern, std::string_view text) {
        std::size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
        while (t < text.size()) {
            if (p < pattern.size() && (pattern[p] == '?' || Lower(pattern[p]) == Lower(text[t]))) {
                ++p;
                ++t;
            } else if (p < pattern.size() && pattern[p] == '*') {
                star = p++;
                mark = t;
            } else if (star != std::string_view::npos) {
                p = star + 1;
                t = ++mark;
            } else {
                return false;
            }
        }
        while (p < pattern.size() && pattern[p] == '*') ++p;
        return p == pattern.size();
    }

    Profile Classify(const Record& r) {
        Profile profile;
        const bool pleasant = r.flags & Record::kPleasant;
        const bool cloudyFlag = r.flags & Record::kCloudy;
        const bool rainy = r.flags & Record::kRainy;
        const bool snowFlag = r.flags & Record::kSnow;
        const int wind = r.windSpeed;
        const int thunder = r.thunderFrequency;

        const bool hasParticles = r.precipitationType >= 0 && r.precipitationDensity > 0.0f;
        const std::string tex = hasParticles ? ToLower(r.precipitationTexture) : std::string{};
        const std::string sky = ToLower(r.auroraModel);
        const auto list = EditorIdWords(r.editorId);
        const WordSet words(list.begin(), list.end());

        const bool texAsh = Contains(tex, "ash");
        const bool texSand = Contains(tex, "sand") || Contains(tex, "dust");
        const bool texDebris = DebrisTexture(tex);
        const bool idSand = SandWord(words);
        const bool idAsh = AshWord(words) && !idSand;
        const bool idStorm = StormWord(words) || BlizzardWord(words);
        const bool idHeavy = HeavyWord(words);
        const bool idLight = LightWord(words);
        const bool idFog = FogWord(words);
        const bool idMist = MistWord(words);
        const bool windy = wind >= 128;  // half the engine's maximum

        // ── Ash and sand first: they fall as snow-type particles
        if (idSand || (texSand && !idAsh)) {
            const int lvl = (idStorm || wind >= 150) ? 75 : (hasParticles ? 50 : 25);
            profile.Set(Kind::Sand, lvl, idSand ? "editor ID" : "sand particles");
        } else if (idAsh || (texAsh && (snowFlag || !(idFog || idMist)))) {
            const int lvl = (idStorm || wind >= 150) ? 75 : ((hasParticles || snowFlag) ? 50 : 25);
            profile.Set(Kind::Ash, lvl, idAsh ? "editor ID" : "ash particles");
        }
        const bool gritty = profile.Level(Kind::Ash) || profile.Level(Kind::Sand);
        const bool rainParticles = hasParticles && r.precipitationType == 0 && !texDebris;
        const bool snowParticles = hasParticles && r.precipitationType == 1 && !(texAsh || texSand || texDebris);

        // ── Rain: classed rainy, or rain particles in a weather that isn't (a sun shower)
        if (rainy || rainParticles) {
            int lvl = rainy ? 50 : 25;
            std::string why = rainy ? "classed rainy" : "rain particles";
            if (idLight) lvl = 25, why += ", editor ID says light";
            if (idHeavy || idStorm || (rainy && thunder < 200)) {
                lvl = 75;
                why += idHeavy || idStorm ? ", editor ID says heavy or storm" : ", frequent lightning";
            }
            if ((idHeavy && StormWord(words)) || words.contains("extreme")) lvl = 100;
            profile.Set(Kind::Rain, lvl, why);
        }

        // ── Snow: classed snow, or snow particles (flurries in a fair or cloudy weather)
        if (!gritty && (snowFlag || snowParticles)) {
            int lvl = snowFlag ? 50 : 25;
            std::string why = snowFlag ? "classed snow" : "snow particles";
            if (idLight && snowFlag) lvl = 25, why += ", editor ID says light";
            if (idHeavy || idStorm || (snowFlag && wind >= 200)) {
                lvl = 75;
                why += idHeavy || idStorm ? ", editor ID says heavy or storm" : ", gale";
            }
            profile.Set(Kind::Snow, lvl, why);
        }
        if (profile.Level(Kind::Snow) >= 50 && (idStorm || windy)) {
            profile.Set(Kind::Blizzard, 75, idStorm ? "snow, editor ID says storm or blizzard" : "snow and strong wind");
        }
        profile.Set(Kind::Precipitation, std::max(profile.Level(Kind::Rain), profile.Level(Kind::Snow)),
                    profile.Level(Kind::Rain) >= profile.Level(Kind::Snow) ? "rain" : "snow");

        // ── Thunder: lightning only strikes in weathers classed rainy; frequency 255 is never
        if (rainy && thunder < 255) {
            const int lvl = thunder >= 240 ? 25 : (thunder >= 150 ? 50 : 75);
            profile.Set(Kind::Thunder, lvl, "lightning frequency " + std::to_string(thunder));
        }
        if (ThunderWord(words) && profile.Level(Kind::Thunder) < 50) profile.Set(Kind::Thunder, 50, "editor ID");

        // ── Storm: any rough weather
        const bool stormSky = Contains(sky, "heavystormsky");
        const int wet = std::max(profile.Level(Kind::Rain), profile.Level(Kind::Snow));
        const int grit = std::max(profile.Level(Kind::Ash), profile.Level(Kind::Sand));
        if (idStorm || stormSky || profile.Level(Kind::Thunder) >= 50 || profile.Level(Kind::Blizzard) || grit >= 75 ||
            (wet >= 75 && windy)) {
            const bool severe = profile.Level(Kind::Rain) >= 100 || profile.Level(Kind::Thunder) >= 75;
            std::string why = idStorm                               ? "editor ID"
                              : stormSky                            ? "storm sky"
                              : profile.Level(Kind::Thunder) >= 50  ? "thunder"
                              : profile.Level(Kind::Blizzard)       ? "blizzard"
                              : grit >= 75                          ? (profile.Level(Kind::Ash) ? "ash storm" : "dust storm")
                                                                    : "heavy precipitation and strong wind";
            profile.Set(Kind::Storm, severe ? 100 : 75, why);
        }

        // ── Fog: the editor ID (its fog distance sets how thick), or a thick fog in a dry, unfair weather
        const bool precipitating = wet || profile.Level(Kind::Ash) >= 50 || profile.Level(Kind::Sand) >= 50;
        const float dayFar = r.fogDayFar;
        const float farthest = std::max(r.fogDayFar, r.fogNightFar);
        if (idFog || idMist) {
            int lvl = (idMist && !idFog) ? 25 : 50;
            if (dayFar > 100000.0f && lvl >= 50) lvl = 25;
            else if (dayFar > 0.0f && dayFar <= 12000.0f) lvl = 75;
            if (idHeavy && idFog) lvl = 75;
            profile.Set(Kind::Fog, lvl, "editor ID, fog distance " + std::to_string(static_cast<int>(dayFar)));
        } else if (!precipitating && !pleasant && farthest > 0.0f && farthest <= 10000.0f) {
            profile.Set(Kind::Fog, farthest <= 5000.0f ? 75 : 50,
                        "fog distance " + std::to_string(static_cast<int>(farthest)));
        }

        // ── Wind, aurora
        profile.Set(Kind::Wind, static_cast<int>(std::lround(wind * 100.0 / 255.0)), "wind speed");
        // An aurora model shows in fair weather; mods also hang one on overcast or rainy skies
        const bool overcast = cloudyFlag || OvercastWord(words);
        if (AuroraWord(words)) {
            profile.Set(Kind::Aurora, 100, "editor ID");
        } else if (Contains(sky, "aurora") && !precipitating && profile.Level(Kind::Fog) < 50 && !overcast) {
            profile.Set(Kind::Aurora, 100, "aurora model");
        }

        // ── Clouds, clear
        if (precipitating || profile.Level(Kind::Storm)) {
            profile.Set(Kind::Clouds, profile.Level(Kind::Storm) ? 100 : 75, profile.Level(Kind::Storm) ? "storm" : "precipitation");
        } else if (overcast) {
            profile.Set(Kind::Clouds, 75, cloudyFlag ? "classed cloudy" : "editor ID");
        } else if (CloudyWord(words)) {
            profile.Set(Kind::Clouds, 50, "editor ID");
        } else if (profile.Level(Kind::Fog) >= 50) {
            profile.Set(Kind::Clouds, 50, "fog");
        }
        if (pleasant && !precipitating && profile.Level(Kind::Fog) < 50 && profile.Level(Kind::Clouds) < 75 &&
            !profile.Level(Kind::Storm)) {
            profile.Set(Kind::Clear, profile.Level(Kind::Clouds) >= 50 ? 50 : 100, "classed pleasant");
        }
        return profile;
    }

    std::vector<PluginWeatherId> ReadPluginWeatherEditorIds(std::istream& in) {
        // Plugin layout: a TES4 record, then top-level groups (GRUP, size including its 24-byte header,
        // label = record type). Records: type, data size, flags, form ID, 8 more bytes, then subrecords
        // (type, u16 size; XXXX carries a u32 size for the next one).
        std::vector<PluginWeatherId> out;
        char header[24];
        if (!in.read(header, 24) || std::memcmp(header, "TES4", 4) != 0) return out;
        std::streamoff pos = 24 + static_cast<std::streamoff>(ReadU32(header + 4));

        std::vector<char> body;
        while (in.seekg(pos) && in.read(header, 24)) {
            const std::uint32_t groupSize = ReadU32(header + 4);
            if (std::memcmp(header, "GRUP", 4) != 0 || groupSize < 24) break;
            if (std::memcmp(header + 8, "WTHR", 4) != 0) {
                pos += groupSize;
                continue;
            }
            const std::streamoff end = pos + groupSize;
            std::streamoff rec = pos + 24;
            while (rec + 24 <= end && in.seekg(rec) && in.read(header, 24)) {
                const std::uint32_t size = ReadU32(header + 4);
                if (std::memcmp(header, "GRUP", 4) == 0) {  // no subgroups in WTHR, but skip one safely
                    rec += size;
                    continue;
                }
                const std::uint32_t flags = ReadU32(header + 8);
                const std::uint32_t formID = ReadU32(header + 12);
                rec += 24 + static_cast<std::streamoff>(size);
                if (flags & 0x00040000) continue;  // compressed

                // EDID comes first; read just enough of the record for it
                const std::uint32_t want = std::min<std::uint32_t>(size, 512);
                body.resize(want);
                if (!in.read(body.data(), want)) break;
                std::size_t i = 0;
                std::uint32_t bigSize = 0;
                while (i + 6 <= want) {
                    std::uint32_t sub = ReadU16(body.data() + i + 4);
                    if (bigSize) sub = bigSize, bigSize = 0;
                    const char* type = body.data() + i;
                    i += 6;
                    if (std::memcmp(type, "XXXX", 4) == 0 && i + 4 <= want) {
                        bigSize = ReadU32(body.data() + i);
                        i += sub;
                        continue;
                    }
                    if (std::memcmp(type, "EDID", 4) == 0) {
                        std::size_t len = std::min<std::size_t>(sub, want - i);
                        while (len > 0 && body[i + len - 1] == '\0') --len;
                        out.push_back({formID, std::string(body.data() + i, len)});
                    }
                    break;
                }
            }
            pos = end;
        }
        return out;
    }

} // namespace ConditionEngine::Weather
