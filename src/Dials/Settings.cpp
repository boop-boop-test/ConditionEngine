#include "Settings.h"
#include "Definition.h"

#include <windows.h>

#include <algorithm>

namespace ConditionEngine::Dials {

    namespace {
        Settings g_settings;

        // GetPrivateProfileString reads the Windows folder for anything but a full path
        std::filesystem::path IniPath() { return std::filesystem::absolute("Data/SKSE/Plugins/ConditionEngine.ini"); }

        std::string ReadString(const char* section, const char* key, const std::string& fallback) {
            char buf[256] = {};
            const auto path = IniPath().string();
            GetPrivateProfileStringA(section, key, fallback.c_str(), buf, sizeof(buf), path.c_str());
            std::string s = buf;
            if (auto semi = s.find(';'); semi != std::string::npos) s.erase(semi);  // trailing comment
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
            return s;
        }

        double ReadDouble(const char* section, const char* key, double fallback) {
            const auto s = ReadString(section, key, "");
            if (s.empty()) return fallback;
            try {
                return std::stod(s);
            } catch (...) {
                return fallback;
            }
        }

        bool ReadBool(const char* section, const char* key, bool fallback) {
            auto s = ReadString(section, key, "");
            if (s.empty()) return fallback;
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s == "1" || s == "true" || s == "yes" || s == "on";
        }
    }

    namespace {
        // A number (0x44, 68) or a key name (F10); missing = the fallback, unreadable = 0
        std::uint32_t ReadKey(const char* section, const char* key, std::uint32_t fallback) {
            const auto s = ReadString(section, key, "");
            if (s.empty()) return fallback;
            try {
                std::size_t used = 0;
                const auto v = std::stoul(s, &used, 0);
                if (used == s.size()) return static_cast<std::uint32_t>(v) & 0xFF;
            } catch (...) {
            }
            return ParseKey(nlohmann::json(s));
        }
    }

    KeyCombo ParseCombo(const std::string& text) {
        KeyCombo combo;
        std::string rest = text;
        for (;;) {
            const auto plus = rest.find('+');
            if (plus == std::string::npos || plus + 1 >= rest.size()) break;  // "Num+" style names keep their +
            auto part = rest.substr(0, plus);
            part.erase(0, part.find_first_not_of(' '));
            part.erase(part.find_last_not_of(' ') + 1);
            if (IEquals(part, "Shift")) combo.mods |= kModShift;
            else if (IEquals(part, "Ctrl") || IEquals(part, "Control")) combo.mods |= kModCtrl;
            else if (IEquals(part, "Alt")) combo.mods |= kModAlt;
            else return {};
            rest = rest.substr(plus + 1);
        }
        rest.erase(0, rest.find_first_not_of(' '));
        rest.erase(rest.find_last_not_of(' ') + 1);
        if (rest.empty() || rest == "0") return {};
        try {
            std::size_t used = 0;
            const auto v = std::stoul(rest, &used, 0);
            if (used == rest.size()) {
                combo.key = static_cast<std::uint32_t>(v) & 0xFF;
                return combo.key ? combo : KeyCombo{};
            }
        } catch (...) {
        }
        combo.key = ParseKey(nlohmann::json(rest));
        return combo.key ? combo : KeyCombo{};
    }

    std::string ComboName(const KeyCombo& combo) {
        if (!combo.key) return {};
        std::string out;
        if (combo.mods & kModCtrl) out += "Ctrl+";
        if (combo.mods & kModAlt) out += "Alt+";
        if (combo.mods & kModShift) out += "Shift+";
        return out + KeyName(combo.key);
    }

    Settings LoadSettings() {
        Settings s;
        s.tickMs = std::clamp(static_cast<int>(ReadDouble("Dials", "iTickMs", s.tickMs)), 50, 2000);
        s.maxCatchUpHours = std::max(0.0, ReadDouble("Dials", "fMaxCatchUpHours", s.maxCatchUpHours));
        s.trackInterval = std::clamp(ReadDouble("Dials", "fTrackSeconds", s.trackInterval), 0.1, 60.0);
        if (const auto hotkey = ReadString("Editor", "iHotkey", ""); !hotkey.empty()) s.editorKey = ParseCombo(hotkey);
        s.vrButton = ReadKey("Editor", "iVRButton", 0);
        s.vrModifier = ReadKey("Editor", "iVRModifier", 0);
        s.pauseGame = ReadBool("Editor", "bPauseGame", s.pauseGame);
        s.verbose = ReadBool("Dials", "bVerbose", s.verbose);
        s.skyrimNet = ReadBool("SkyrimNet", "bEnable", s.skyrimNet);
        s.skyrimNetRefresh = std::clamp(ReadDouble("SkyrimNet", "fRefreshSeconds", s.skyrimNetRefresh), 0.25, 30.0);
        g_settings = s;
        return s;
    }

    const Settings& GetSettings() { return g_settings; }

    std::filesystem::path DataDirectory() { return std::filesystem::path("Data/SKSE/Plugins/ConditionEngine"); }
    std::filesystem::path UserFile() { return DataDirectory() / "User" / "User.json"; }

} // namespace ConditionEngine::Dials
