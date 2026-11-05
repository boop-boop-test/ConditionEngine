// Serialization.cpp - Dial values in the SKSE co-save (record 'VALS' under 'DIAL').
//
// A copy SKSE loaded itself owns that record. A hosted copy (one another plugin started on its
// own SKSE handle, from Data/SKSE/Plugins/<Owner>/ConditionEngine/) must not touch the handle's
// co-save registration: SKSE allows one unique ID per handle, and a second SetUniqueID on it
// asserts and stops plugin loading. The host saves the values in a record of its own instead,
// through ConditionEngine_SaveDials / LoadDials / RevertDials.

#include "Serialization.h"
#include "Service.h"

#include <filesystem>

namespace ConditionEngine::Dials::Serialization {

    namespace {
        constexpr std::uint32_t kUniqueId = 'DIAL';
        constexpr std::uint32_t kValues = 'VALS';
        constexpr std::uint32_t kVersion = 1;

        bool g_hosted = false;

        void Save(SKSE::SerializationInterface* intfc) {
            const auto bytes = Service::Get().GetEngine().Save();
            if (!intfc->WriteRecord(kValues, kVersion, bytes.data(), static_cast<std::uint32_t>(bytes.size()))) {
                SKSE::log::error("Dials: could not write {} bytes of dial values to the co-save", bytes.size());
            }
        }

        void Load(SKSE::SerializationInterface* intfc) {
            std::uint32_t type = 0, version = 0, length = 0;
            while (intfc->GetNextRecordInfo(type, version, length)) {
                if (type != kValues) continue;
                if (version != kVersion) {
                    SKSE::log::warn("Dials: co-save record version {} not understood; values start fresh", version);
                    continue;
                }
                std::vector<std::uint8_t> bytes(length);
                if (intfc->ReadRecordData(bytes.data(), length) != length) {
                    SKSE::log::error("Dials: short co-save record");
                    continue;
                }
                LoadBytes(bytes);
            }
        }

        void Revert(SKSE::SerializationInterface*) { Service::Get().OnSessionEnd(); }

        // The folder this DLL was loaded from
        std::filesystem::path OwnFolder() {
            HMODULE self = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    reinterpret_cast<LPCWSTR>(&OwnFolder), &self)) {
                return {};
            }
            std::wstring buf(MAX_PATH, L'\0');
            for (;;) {
                const DWORD n = GetModuleFileNameW(self, buf.data(), static_cast<DWORD>(buf.size()));
                if (n == 0) return {};
                if (n < buf.size()) {
                    buf.resize(n);
                    return std::filesystem::path(buf).parent_path();
                }
                buf.resize(buf.size() * 2);
            }
        }
    }

    bool DetectHosted() {
        // SKSE loads plugins only from Data/SKSE/Plugins itself; anywhere else, another plugin started us
        const auto folder = OwnFolder();
        if (folder.empty()) return false;
        const auto name = Lower(folder.filename().string());
        const auto parent = Lower(folder.parent_path().filename().string());
        g_hosted = !(name == "plugins" && parent == "skse");
        return g_hosted;
    }

    bool Hosted() { return g_hosted; }

    void Install() {
        if (g_hosted) {
            SKSE::log::info("Dials: started by another plugin (a bundled copy); dial values are saved through it "
                            "(ConditionEngine_SaveDials / LoadDials / RevertDials), or not at all if it does not");
            return;
        }
        auto* s = SKSE::GetSerializationInterface();
        s->SetUniqueID(kUniqueId);
        s->SetSaveCallback(Save);
        s->SetLoadCallback(Load);
        s->SetRevertCallback(Revert);
    }

    std::vector<std::uint8_t> SaveBytes() { return Service::Get().GetEngine().Save(); }

    bool LoadBytes(std::span<const std::uint8_t> bytes) {
        auto* intfc = SKSE::GetSerializationInterface();
        const auto resolve = [intfc](std::uint32_t id) -> std::optional<std::uint32_t> {
            RE::FormID out = 0;
            if (!intfc || !intfc->ResolveFormID(id, out)) return std::nullopt;
            return out;
        };
        if (!Service::Get().GetEngine().Load(bytes, resolve)) {
            SKSE::log::error("Dials: saved dial values could not be read; values start fresh");
            return false;
        }
        return true;
    }

    void RevertValues() { Service::Get().OnSessionEnd(); }

} // namespace ConditionEngine::Dials::Serialization
