#pragma once

// Forms.h - Form string helpers ("0x12345|Plugin.esp").
// Same format most SKSE mods use for persisting form references.

#include "ConditionEngine/Common.h"

namespace ConditionEngine::Forms {

    // True when the string looks like "0xID|Plugin.esp" (or .esm/.esl, or "0xID|TYPE.FF" for temp forms)
    bool IsFormString(const std::string& str);

    // Resolve a form string to a form. Returns nullptr when it cannot be resolved.
    RE::TESForm* ParseFormString(const std::string& str);

    // Resolve any form reference: "player"/"PlayerRef", a form string, a raw hex
    // FormID ("0x14") or an editor ID. Editor IDs only resolve for form types the
    // game keeps editor IDs for (keywords, races, globals, cells, worldspaces,
    // quests...) unless powerofthree's Tweaks is installed.
    RE::TESForm* LookupAny(const std::string& text);

    // Resolve either a form string or an editor ID to a form of type T.
    template <class T>
    T* LookupForm(const std::string& idOrEditorId) {
        if (idOrEditorId.empty()) return nullptr;
        if (IsFormString(idOrEditorId)) {
            auto* form = ParseFormString(idOrEditorId);
            return form ? form->As<T>() : nullptr;
        }
        return RE::TESForm::LookupByEditorID<T>(idOrEditorId);
    }

} // namespace ConditionEngine::Forms
