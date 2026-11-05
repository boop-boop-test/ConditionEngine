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

    Settings LoadSettings() {
        Settings s;
        s.tickMs = std::clamp(static_cast<int>(ReadDouble("Dials", "iTickMs", s.tickMs)), 50, 2000);
        s.maxCatchUpHours = std::max(0.0, ReadDouble("Dials", "fMaxCatchUpHours", s.maxCatchUpHours));
        s.trackInterval = std::clamp(ReadDouble("Dials", "fTrackSeconds", s.trackInterval), 0.1, 60.0);
        s.editorKey = ReadKey("Editor", "iHotkey", s.editorKey);
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
