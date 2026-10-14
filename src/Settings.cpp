#include "ConditionEngine/Settings.h"
#include "ConditionEngine/Forms.h"
#include "ConditionEngine/Registry.h"

#include <fstream>

namespace ConditionEngine {

    namespace {
        std::string Trim(std::string s) {
            auto start = s.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) return {};
            auto end = s.find_last_not_of(" \t\r\n");
            return s.substr(start, end - start + 1);
        }

        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        std::chrono::milliseconds Seconds(const std::string& text, std::chrono::milliseconds fallback) {
            try {
                const double seconds = std::stod(text);
                if (seconds > 0.0) return std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0));
            } catch (...) {
            }
            return fallback;
        }
    }

    std::filesystem::path GetDefaultSettingsPath() {
        return std::filesystem::path("Data") / "SKSE" / "Plugins" / "ConditionEngine.ini";
    }

    Settings LoadSettings(const std::filesystem::path& path) {
        Settings settings;
        std::ifstream in(path);
        if (!in) return settings;

        std::string line;
        std::string section;
        while (std::getline(in, line)) {
            line = Trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (line.front() == '[' && line.back() == ']') {
                section = Lower(Trim(line.substr(1, line.size() - 2)));
                continue;
            }
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const auto key = Lower(Trim(line.substr(0, eq)));
            auto value = Trim(line.substr(eq + 1));
            if (auto comment = value.find(';'); comment != std::string::npos) value = Trim(value.substr(0, comment));

            if (section == "watcher") {
                if (key == "fpollintervalseconds") settings.pollInterval = Seconds(value, settings.pollInterval);
                else if (key == "fdetectintervalseconds") settings.detectInterval = Seconds(value, settings.detectInterval);
            } else if (section == "console") {
                if (key == "sreplacecommand") settings.consoleCommand = value;
            } else if (section == "temperature") {
                try {
                    if (key == "sglobal") settings.temperatureGlobal = value;
                    else if (key == "fscale") settings.temperatureScale = std::stof(value);
                    else if (key == "foffset") settings.temperatureOffset = std::stof(value);
                } catch (...) {
                    SKSE::log::warn("ConditionEngine.ini: invalid number for [Temperature] {}", key);
                }
            }
        }
        return settings;
    }

    void ApplyTemperatureSettings(const Settings& settings) {
        if (settings.temperatureGlobal.empty()) return;

        auto* global = Forms::LookupAny(settings.temperatureGlobal);
        auto* asGlobal = global ? global->As<RE::TESGlobal>() : nullptr;
        if (!asGlobal) {
            SKSE::log::warn("ConditionEngine.ini: temperature global '{}' not found", settings.temperatureGlobal);
            return;
        }

        const float scale = settings.temperatureScale;
        const float offset = settings.temperatureOffset;
        Registry::Get().SetTemperatureProvider(
            [asGlobal, scale, offset](RE::Actor*) { return asGlobal->value * scale + offset; });
        SKSE::log::info("ConditionEngine: temperature from global '{}' (x{} + {})", settings.temperatureGlobal, scale,
                        offset);
    }

} // namespace ConditionEngine
