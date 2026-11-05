#include "GameHost.h"
#include "CEApi.h"

#include <atomic>
#include <charconv>
#include <format>
#include <random>
#include <shared_mutex>

namespace ConditionEngine::Dials {

    namespace {
        std::atomic<double> g_eventNumber{0.0};

        // Filters resolve once per text (forms do not move during a session)
        std::shared_mutex g_formMutex;
        std::unordered_map<std::string, RE::FormID> g_forms;

        std::optional<std::uint32_t> ParseHex(std::string_view s) {
            if (s.starts_with("0x") || s.starts_with("0X")) s.remove_prefix(2);
            if (s.empty()) return std::nullopt;
            std::uint32_t v = 0;
            auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v, 16);
            if (ec != std::errc{} || end != s.data() + s.size()) return std::nullopt;
            return v;
        }

        std::string Trim(std::string_view s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
            return std::string(s);
        }

        RE::FormID Resolve(const std::string& text) {
            {
                std::shared_lock lock(g_formMutex);
                if (auto it = g_forms.find(text); it != g_forms.end()) return it->second;
            }
            auto* form = ParseForm(text);
            const RE::FormID id = form ? form->GetFormID() : 0;
            if (!form) SKSE::log::warn("Dials: form '{}' not found", text);
            std::unique_lock lock(g_formMutex);
            g_forms[text] = id;
            return id;
        }

        int32_t ReadEventNumber(void*, const char*, double* out, char*, uint32_t, void*) {
            *out = g_eventNumber.load();
            return CE_VALUE_OK;
        }
    }

    RE::TESForm* ParseForm(const std::string& raw) {
        const auto text = Trim(raw);
        if (text.empty()) return nullptr;
        if (auto bar = text.find('|'); bar != std::string::npos) {
            auto left = Trim(std::string_view(text).substr(0, bar));
            auto right = Trim(std::string_view(text).substr(bar + 1));
            auto id = ParseHex(left);
            std::string plugin = right;
            if (!id) {
                id = ParseHex(right);
                plugin = left;
            }
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!id || !data) return nullptr;
            const auto* file = data->LookupModByName(plugin);
            if (!file) return nullptr;
            const std::uint32_t local = file->IsLight() ? (*id & 0xFFF) : (*id & 0xFFFFFF);
            return data->LookupForm(local, plugin);
        }
        if (auto id = ParseHex(text); id && (text.starts_with("0x") || text.starts_with("0X"))) {
            return RE::TESForm::LookupByID(*id);
        }
        return RE::TESForm::LookupByEditorID(text);
    }

    RE::Actor* ActorOf(ActorKey key) {
        if (key == 0 || key == kPlayer) return RE::PlayerCharacter::GetSingleton();
        return RE::TESForm::LookupByID<RE::Actor>(key);
    }

    ActorKey KeyOf(const RE::TESForm* form) {
        if (!form || !form->Is(RE::FormType::ActorCharacter)) return 0;
        return form->GetFormID();
    }

    std::string ActorName(ActorKey key) {
        if (key == 0) return "global";
        if (key == kPlayer) return "player";
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(key);
        const char* name = actor ? actor->GetName() : nullptr;
        return std::format("{} (0x{:08X})", name && *name ? name : "?", key);
    }

    double EventNumber() { return g_eventNumber.load(); }

    void RegisterEventValueType() {
        const auto& ce = CE::Get();
        const char* schema = R"({
            "name": "DialsEvent", "label": "Dials: event number", "category": "dials", "source": "Dials",
            "description": "Inside a dial rule: the number its event carries (a mod event's number, items added, hours slept). 0 elsewhere.",
            "aliases": ["DialEvent"], "fields": [], "offer": "owner", "cost": 0,
            "value": {"unit": ""}
        })";
        char error[512] = {};
        const auto r = ce.RegisterValueType(schema, &ReadEventNumber, nullptr, 0, error, sizeof(error));
        if (r != CE_OK && r != CE_ERR_ALREADY_EXISTS) SKSE::log::warn("Dials: DialsEvent not registered: {}", error);
    }

    // ── Host ─────────────────────────────────────────────────────────────────

    Handle GameHost::CompileCondition(const std::string& source, std::string& error) {
        char buf[1024] = {};
        const auto h = CE::Get().Compile(source.c_str(), buf, sizeof(buf));
        if (!h) error = buf;
        return h;
    }

    Handle GameHost::CompileValue(const std::string& source, std::string& error) {
        char buf[1024] = {};
        const auto h = CE::Get().CompileValue(source.c_str(), buf, sizeof(buf));
        if (!h) error = buf;
        return h;
    }

    void GameHost::ReleaseCondition(Handle handle) { CE::Get().Release(handle); }
    void GameHost::ReleaseValue(Handle handle) { CE::Get().ReleaseValue(handle); }

    std::optional<bool> GameHost::Evaluate(Handle condition, ActorKey actor) {
        auto* a = ActorOf(actor);
        if (!a) return std::nullopt;
        const auto r = CE::Get().Evaluate(condition, a);
        if (r < 0) return std::nullopt;
        return r != 0;
    }

    std::optional<double> GameHost::EvaluateValue(Handle value, ActorKey actor) {
        auto* a = ActorOf(actor);
        if (!a) return std::nullopt;
        CE_ValueSample sample{};
        sample.structSize = sizeof(sample);
        if (CE::Get().EvaluateValue(value, a, &sample, nullptr, 0) != CE_OK) return std::nullopt;
        if (sample.status != CE_VALUE_OK) return std::nullopt;
        return sample.value;
    }

    bool GameHost::FormMatches(const std::string& filter, std::uint32_t formID) {
        const auto id = Resolve(filter);
        return id != 0 && id == formID;
    }

    bool GameHost::HasKeyword(const std::string& keyword, std::uint32_t formID) {
        const auto id = Resolve(keyword);
        auto* kw = id ? RE::TESForm::LookupByID<RE::BGSKeyword>(id) : nullptr;
        auto* form = RE::TESForm::LookupByID(formID);
        if (!kw || !form) return false;
        if (const auto* keywords = form->As<RE::BGSKeywordForm>()) return keywords->HasKeyword(kw);
        return false;
    }

    double GameHost::Random01() {
        thread_local std::mt19937 rng{std::random_device{}()};
        return std::uniform_real_distribution<double>(0.0, 1.0)(rng);
    }

    void GameHost::SetEventNumber(double number) { g_eventNumber.store(number); }

} // namespace ConditionEngine::Dials
