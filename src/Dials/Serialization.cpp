// Serialization.cpp - Dial values in the SKSE co-save (record 'VALS' under 'DIAL').

#include "Serialization.h"
#include "Service.h"

namespace ConditionEngine::Dials::Serialization {

    namespace {
        constexpr std::uint32_t kUniqueId = 'DIAL';
        constexpr std::uint32_t kValues = 'VALS';
        constexpr std::uint32_t kVersion = 1;

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
                const auto resolve = [intfc](std::uint32_t id) -> std::optional<std::uint32_t> {
                    RE::FormID out = 0;
                    if (!intfc->ResolveFormID(id, out)) return std::nullopt;
                    return out;
                };
                if (!Service::Get().GetEngine().Load(bytes, resolve)) {
                    SKSE::log::error("Dials: co-save values could not be read; values start fresh");
                }
            }
        }

        void Revert(SKSE::SerializationInterface*) { Service::Get().OnSessionEnd(); }
    }

    void Install() {
        auto* s = SKSE::GetSerializationInterface();
        s->SetUniqueID(kUniqueId);
        s->SetSaveCallback(Save);
        s->SetLoadCallback(Load);
        s->SetRevertCallback(Revert);
    }

} // namespace ConditionEngine::Dials::Serialization
