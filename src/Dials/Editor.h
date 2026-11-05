#pragma once

// Editor.h - The ConditionEngine editor: a Magelight UI page (Data/Magelight/ConditionEngine/views/editor) where
// the player browses, makes and edits dials, named conditions and values, tests rules and copies snippets.
// Opened by the [Editor] iHotkey, "ce ui", Dials.OpenEditor or ConditionEngine_OpenEditor.
//
// Page -> host: channel "dials", JSON {"op": ..., "req": n, ...}
// Host -> page: "dials_state" (Snapshot), "dials_live" (values, log), "dials_reply"
//               ({"req", "ok", "errors", "warnings", "text"}), "dials_catalog", "dials_focus"

#include <string>

namespace ConditionEngine::Editor {

    void Install();     // kDataLoaded: registers with Magelight (no-op without it)
    // After the dials started (User.json is read): the key that opens the editor, through InputManager when
    // it is installed, else Magelight; and the VR button. Main thread.
    void InstallHotkey();
    bool Available();   // Magelight is there and the page was created
    void Open(const std::string& dial = {});  // main thread
    void Close();

} // namespace ConditionEngine::Editor
