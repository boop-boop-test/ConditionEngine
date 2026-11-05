#include "ConditionEngine/Weather/WeatherProfiles.h"
#include "ConditionEngine/Forms.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <shared_mutex>

namespace ConditionEngine::Weather {

    namespace {
        // ── Editor IDs read from the plugin files ──────────────────────────────
        struct EditorIdIndex {
            // runtime form ID -> (plugin that has the record, its editor ID there), in load order
            std::unordered_map<RE::FormID, std::vector<std::pair<const RE::TESFile*, std::string>>> byForm;
        };

        RE::FormID RuntimeFormID(const RE::TESFile* file, std::uint32_t localID) {
            const std::uint32_t index = localID >> 24;
            const RE::TESFile* owner = file;
            if (index < file->masterCount && file->masterPtrs) owner = file->masterPtrs[index];
            if (!owner) return 0;
            if (owner->IsLight()) {
                return 0xFE000000u | (static_cast<std::uint32_t>(owner->smallFileCompileIndex) << 12) | (localID & 0xFFF);
            }
            return (static_cast<std::uint32_t>(owner->compileIndex) << 24) | (localID & 0xFFFFFF);
        }

        void ReadFile(const RE::TESFile* file, EditorIdIndex& index, std::size_t& count) {
            if (!file || !file->fileName[0]) return;
            std::ifstream in(std::filesystem::path("Data") / file->fileName, std::ios::binary);
            if (!in) return;
            for (auto& entry : ReadPluginWeatherEditorIds(in)) {
                if (const auto id = RuntimeFormID(file, entry.formID)) {
                    index.byForm[id].emplace_back(file, std::move(entry.editorId));
                    ++count;
                }
            }
        }

        EditorIdIndex BuildEditorIdIndex() {
            EditorIdIndex index;
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!data) return index;
            const auto started = std::chrono::steady_clock::now();
            std::size_t count = 0;
            const auto* mods = data->GetLoadedMods();
            for (std::uint32_t i = 0, n = data->GetLoadedModCount(); mods && i < n; ++i) ReadFile(mods[i], index, count);
            const auto* light = data->GetLoadedLightMods();
            for (std::uint32_t i = 0, n = data->GetLoadedLightModCount(); light && i < n; ++i) ReadFile(light[i], index, count);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
            SKSE::log::info("ConditionEngine: read {} weather editor IDs from the plugin files in {} ms", count, ms.count());
            return index;
        }

        std::once_flag g_primeOnce;
        std::shared_future<EditorIdIndex> g_index;

        const EditorIdIndex& Index() {
            PrimeEditorIds();
            return g_index.get();
        }

        // ── Overrides ──────────────────────────────────────────────────────────
        struct OverrideEntry {
            std::string key;       // glob over editor IDs, or a form string
            bool formKey = false;
            std::optional<RE::FormID> resolved;  // form keys, resolved on first use (game data needed)
            std::vector<std::pair<Kind, int>> levels;
            std::string source;
        };

        std::shared_mutex g_mutex;
        std::vector<OverrideEntry> g_overrides;
        std::unordered_map<RE::FormID, std::shared_ptr<const Profile>> g_cache;
        bool g_unresolved = false;  // a form key waits to be resolved

        void ResolveFormKeys() {
            {
                std::shared_lock lock(g_mutex);
                if (!g_unresolved) return;
            }
            std::unique_lock lock(g_mutex);
            for (auto& entry : g_overrides) {
                if (!entry.formKey || entry.resolved) continue;
                auto* form = Forms::ParseFormString(entry.key);
                entry.resolved = form ? form->GetFormID() : 0;
                if (!form) SKSE::log::warn("ConditionEngine: weather '{}' in '{}' not found", entry.key, entry.source);
            }
            g_unresolved = false;
        }

        RE::BGSShaderParticleGeometryData::SETTING_VALUE Setting(const RE::BGSShaderParticleGeometryData* data,
                                                                RE::BGSShaderParticleGeometryData::DataID id) {
            return const_cast<RE::BGSShaderParticleGeometryData*>(data)->GetSettingValue(id);
        }

        constexpr std::uint32_t kDensityIndex = static_cast<std::uint32_t>(RE::BGSShaderParticleGeometryData::DataID::kParticleDensity);

        std::uint32_t ParticleSettingCount(const RE::BGSShaderParticleGeometryData* data) {
            auto* d = const_cast<RE::BGSShaderParticleGeometryData*>(data);
            return REL::Module::IsVR() ? d->GetVRRuntimeData().data.size() : d->GetRuntimeData().data.size();
        }

        const char* ParticleTexture(const RE::BGSShaderParticleGeometryData* data) {
            if (REL::Module::IsVR()) {
                return const_cast<RE::BGSShaderParticleGeometryData*>(data)->GetVRRuntimeData().particleTexture.textureName.c_str();
            }
            return const_cast<RE::BGSShaderParticleGeometryData*>(data)->GetRuntimeData().particleTexture.textureName.c_str();
        }
    }

    Record ReadRecord(const RE::TESWeather* weather) {
        Record r;
        if (!weather) return r;
        r.flags = static_cast<std::uint8_t>(weather->data.flags.underlying());
        r.windSpeed = weather->data.windSpeed;
        r.thunderFrequency = static_cast<std::uint8_t>(weather->data.thunderLightningFrequency);
        if (const auto* precip = weather->precipitationData; precip && ParticleSettingCount(precip) > kDensityIndex) {
            using ID = RE::BGSShaderParticleGeometryData::DataID;
            r.precipitationType = static_cast<int>(Setting(precip, ID::kParticleType).i);
            r.precipitationDensity = Setting(precip, ID::kParticleDensity).f;
            if (const char* tex = ParticleTexture(precip)) r.precipitationTexture = tex;
        }
        r.fogDayFar = weather->fogData.dayFar;
        r.fogNightFar = weather->fogData.nightFar;
        if (const char* model = weather->aurora.GetModel()) r.auroraModel = model;
        r.editorId = EditorIdOf(weather);
        return r;
    }

    void PrimeEditorIds() {
        std::call_once(g_primeOnce, [] { g_index = std::async(std::launch::async, BuildEditorIdIndex).share(); });
    }

    std::string EditorIdOf(const RE::TESWeather* weather) {
        if (!weather) return {};
        if (const char* id = weather->GetFormEditorID(); id && *id) return id;  // powerofthree's Tweaks keeps them
        const auto& index = Index();
        auto it = index.byForm.find(weather->GetFormID());
        if (it == index.byForm.end() || it->second.empty()) return {};
        const auto* winner = weather->GetFile(-1);  // the last plugin that edits the record
        for (auto e = it->second.rbegin(); e != it->second.rend(); ++e) {
            if (e->first == winner) return e->second;
        }
        return it->second.back().second;
    }

    std::string NameOf(const RE::TESWeather* weather) {
        if (!weather) return "no weather";
        const auto* file = weather->GetFile(0);
        const auto id = EditorIdOf(weather);
        return std::format("{}{:08X}, {}{}", id.empty() ? "0x" : id + " (0x", weather->GetFormID(),
                           file ? file->GetFilename() : "created", id.empty() ? "" : ")");
    }

    std::shared_ptr<const Profile> ProfileOf(const RE::TESWeather* weather) {
        if (!weather) return nullptr;
        const auto formID = weather->GetFormID();
        {
            std::shared_lock lock(g_mutex);
            if (auto it = g_cache.find(formID); it != g_cache.end()) return it->second;
        }
        auto record = ReadRecord(weather);  // outside the lock: may wait for the editor ID index
        auto profile = std::make_shared<Profile>(Classify(record));
        ApplyOverrides(*profile, record.editorId, formID);
        std::unique_lock lock(g_mutex);
        return g_cache.try_emplace(formID, std::move(profile)).first->second;
    }

    std::size_t AddOverrides(const nlohmann::json& section, const std::string& source, std::vector<std::string>* warnings) {
        if (!section.is_object()) {
            if (warnings) warnings->push_back("\"weathers\" must be an object of weather -> {kind: level}");
            return 0;
        }
        std::vector<OverrideEntry> added;
        for (const auto& [key, levels] : section.items()) {
            if (!levels.is_object()) {
                if (warnings) warnings->push_back("weather '" + key + "': expected {\"kind\": level, ...}");
                continue;
            }
            OverrideEntry entry{key, Forms::IsFormString(key), std::nullopt, {}, source};
            for (const auto& [kindName, level] : levels.items()) {
                const auto kind = ParseKind(kindName);
                if (!kind || !level.is_number()) {
                    if (warnings) {
                        warnings->push_back("weather '" + key + "': " +
                                            (!kind ? "unknown kind '" + kindName + "'" : "level of '" + kindName + "' must be a number"));
                    }
                    continue;
                }
                entry.levels.emplace_back(*kind, std::clamp(level.get<int>(), 0, 100));
            }
            if (!entry.levels.empty()) added.push_back(std::move(entry));
        }
        const auto count = added.size();
        std::unique_lock lock(g_mutex);
        for (auto& e : added) {
            g_unresolved = g_unresolved || e.formKey;
            g_overrides.push_back(std::move(e));
        }
        g_cache.clear();
        return count;
    }

    void ApplyOverrides(Profile& profile, std::string_view editorId, std::uint32_t formID) {
        if (formID != 0) ResolveFormKeys();
        std::shared_lock lock(g_mutex);
        for (const auto& entry : g_overrides) {
            bool match = false;
            if (entry.formKey) {
                match = formID != 0 && entry.resolved && *entry.resolved == formID;
            } else {
                match = !editorId.empty() && MatchesGlob(entry.key, editorId);
            }
            if (!match) continue;
            for (const auto& [kind, level] : entry.levels) profile.Set(kind, level, "preset " + entry.source);
        }
    }

    void ClearOverrides() {
        std::unique_lock lock(g_mutex);
        g_overrides.clear();
        g_cache.clear();
        g_unresolved = false;
    }

    void ClearCache() {
        std::unique_lock lock(g_mutex);
        g_cache.clear();
    }

} // namespace ConditionEngine::Weather
