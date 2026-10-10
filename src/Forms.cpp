#include "ConditionEngine/Forms.h"

namespace ConditionEngine::Forms {

    bool IsFormString(const std::string& str) {
        if (str.size() < 4 || str.find('|') == std::string::npos) return false;
        return str.ends_with(".esp") || str.ends_with(".esm") || str.ends_with(".esl") || str.ends_with(".FF");
    }

    RE::TESForm* ParseFormString(const std::string& str) {
        if (!IsFormString(str)) return nullptr;

        const std::size_t pos = str.find('|');
        const std::string idPart = str.substr(0, pos);
        std::string mod = str.substr(pos + 1);

        std::uint32_t baseId = 0;
        try {
            baseId = idPart.rfind("0x", 0) == 0 ? std::stoul(idPart, nullptr, 16)
                                                : static_cast<std::uint32_t>(std::stoul(idPart));
        } catch (const std::exception& e) {
            SKSE::log::warn("ConditionEngine: failed to parse form id '{}': {}", idPart, e.what());
            return nullptr;
        }

        // Temp objects: "0xID|<formType>.FF"
        if (str.ends_with(".FF")) {
            mod.resize(mod.length() - 3);
            std::uint8_t expectedType = 0;
            try {
                expectedType = static_cast<std::uint8_t>(std::stoi(mod));
            } catch (...) {
                return nullptr;
            }
            const std::uint32_t formId = (0xFFu << 24) | baseId;
            auto* form = RE::TESForm::LookupByID(formId);
            if (!form || static_cast<std::uint8_t>(form->GetFormType()) != expectedType) return nullptr;
            return form;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return nullptr;

        const RE::TESFile* file = dataHandler->LookupModByName(mod);
        if (!file) {
            SKSE::log::warn("ConditionEngine: plugin not loaded for form string '{}'", str);
            return nullptr;
        }

        std::uint32_t formId = 0;
        if (file->IsLight()) {
            formId = 0xFE000000 | (static_cast<std::uint32_t>(file->GetSmallFileCompileIndex()) << 12) | (baseId & 0xFFF);
        } else {
            formId = (static_cast<std::uint32_t>(file->GetCompileIndex()) << 24) | (baseId & 0x00FFFFFF);
        }
        return RE::TESForm::LookupByID(formId);
    }

    RE::TESForm* LookupAny(const std::string& text) {
        if (text.empty()) return nullptr;
        if (_stricmp(text.c_str(), "player") == 0 || _stricmp(text.c_str(), "PlayerRef") == 0) {
            return RE::PlayerCharacter::GetSingleton();
        }
        if (IsFormString(text)) return ParseFormString(text);
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            try {
                std::size_t pos = 0;
                auto id = static_cast<RE::FormID>(std::stoul(text, &pos, 16));
                if (pos == text.size()) return RE::TESForm::LookupByID(id);
            } catch (...) {
            }
        }
        return RE::TESForm::LookupByEditorID(text);
    }

} // namespace ConditionEngine::Forms
