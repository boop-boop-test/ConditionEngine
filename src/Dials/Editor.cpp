#include "Editor.h"
#include "CEApi.h"
#include "Service.h"
#include "Settings.h"
#include "InputManagerAPI.h"
#include "MagelightUI_API.h"
#include "../SkyrimNet/Bridge.h"

#include <atomic>
#include <filesystem>

namespace ConditionEngine::Editor {

    namespace {
        namespace ML = MAGELIGHT_API;
        using namespace ConditionEngine::Dials;
        using Dials::Service;

        const ML::MagelightApi4* g_api = nullptr;
        ML::ModId g_mod = 0;
        ML::ViewId g_view = 0;
        std::atomic<bool> g_open{false};
        std::string g_focus;  // the dial to show when the page is up
        std::chrono::steady_clock::time_point g_lastLive{};

        constexpr std::uint32_t kMinHost = ML::PackVersion(0, 30, 1);  // PostGameTask

        std::string PagePath() {
            wchar_t buf[MAX_PATH]{};
            GetModuleFileNameW(nullptr, buf, MAX_PATH);
            const auto p = std::filesystem::path(buf).parent_path() / L"Data" / L"Magelight" / L"ConditionEngine" / L"views" /
                           L"editor" / L"index.html";
            return p.string();
        }

        void Send(const char* channel, const std::string& payload) {
            if (g_api && g_view) g_api->InteropCall(g_view, channel, payload.c_str());
        }

        nlohmann::json HotkeyStatus();

        void SendState() {
            auto snapshot = Service::Get().Snapshot();
            snapshot.update(HotkeyStatus());
            Send("dials_state", snapshot.dump());
        }

        void SendLive(bool force) {
            if (!g_open.load()) return;
            const auto now = std::chrono::steady_clock::now();
            if (!force && now - g_lastLive < std::chrono::milliseconds(250)) return;
            g_lastLive = now;
            Send("dials_live", Service::Get().LiveValues().dump());
        }

        void Reply(const nlohmann::json& request, const Dials::Outcome& o, const std::string& text = {}) {
            auto j = o.ToJson();
            j["req"] = request.value("req", 0);
            j["op"] = request.value("op", "");
            if (!text.empty()) j["text"] = text;
            Send("dials_reply", j.dump());
        }

        ActorKey ActorFrom(const nlohmann::json& m) {
            const auto s = m.value("actor", std::string());
            if (s.empty()) return 0;
            try {
                return static_cast<ActorKey>(std::stoul(s, nullptr, 0));
            } catch (...) {
                return 0;
            }
        }

        // ── The key that opens the editor ────────────────────────────────────
        // With InputManager it is an InputManager action the player rebinds there (gamepad too). Without it a
        // bare key is a Magelight hotkey, and a key with Shift / Ctrl / Alt is watched here (Magelight binds bare
        // keys only). The player's pick in the editor (User.json "editor") wins over the ini.
        enum class Via { None, InputManager, Magelight, Watched };
        constexpr const char* kInputManagerAction = "ConditionEngine_OpenEditor";
        constexpr const char* kInputManagerOwner = "ConditionEngine";

        Via g_via = Via::None;
        KeyCombo g_combo;  // the key that opens the editor ({} = none)
        bool g_pause = true;
        int g_imAction = -1;
        std::string g_keyError;

        KeyCombo EffectiveKey() {
            const auto prefs = Service::Get().EditorPrefs();
            if (auto it = prefs.find("hotkey"); it != prefs.end() && it->is_string()) return ParseCombo(it->get<std::string>());
            return Dials::GetSettings().editorKey;
        }

        bool EffectivePause() {
            const auto prefs = Service::Get().EditorPrefs();
            if (auto it = prefs.find("pauseGame"); it != prefs.end() && it->is_boolean()) return it->get<bool>();
            return Dials::GetSettings().pauseGame;
        }

        bool ModifiersHeld(std::uint32_t mods) {
            auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
            return down(VK_SHIFT) == ((mods & kModShift) != 0) && down(VK_CONTROL) == ((mods & kModCtrl) != 0) &&
                   down(VK_MENU) == ((mods & kModAlt) != 0);
        }

        void Toggle() {
            if (g_open.load()) {
                Close();
                return;
            }
            auto* ui = RE::UI::GetSingleton();
            if (ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME))) return;  // in a menu
            Open({});
        }

        // DirectInput scan codes <-> the virtual keys InputManager stores (extended keys carry 0x80 / an E0 prefix)
        std::uint32_t VirtualKeyOf(std::uint32_t dx) {
            if (!dx) return 0;
            const UINT scan = (dx & 0x80) ? (0xE000u | (dx & 0x7F)) : dx;
            return MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX);
        }

        std::uint32_t ScanCodeOf(std::uint32_t vk) {
            if (!vk) return 0;
            const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
            return (scan & 0xFF) | ((scan >> 8) == 0xE0 ? 0x80u : 0u);
        }

        // InputManager takes one modifier key; Shift wins over Ctrl over Alt
        std::uint32_t ModifierVirtualKey(std::uint32_t mods) {
            if (mods & kModShift) return VK_SHIFT;
            if (mods & kModCtrl) return VK_CONTROL;
            if (mods & kModAlt) return VK_MENU;
            return 0;
        }

        std::uint32_t ModsOfVirtualKey(std::uint32_t vk) {
            switch (vk) {
                case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return kModShift;
                case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return kModCtrl;
                case VK_MENU: case VK_LMENU: case VK_RMENU: return kModAlt;
                default: return 0;
            }
        }

        InputManagerAPI::IInputManager* InputManager() {
            return InputManagerAPI::_API ? InputManagerAPI::_API : InputManagerAPI::RequestAPIDirect();
        }

        bool SetInputManagerKey(const KeyCombo& combo) {
            auto* im = InputManager();
            if (!im || g_imAction < 0) return false;
            auto info = im->GetActionInfo(g_imAction);
            info.id = g_imAction;
            info.pcMainKey = VirtualKeyOf(combo.key);
            info.pcModifierKey = ModifierVirtualKey(combo.mods);
            info.isValid = true;
            return im->UpdateActionMapping(g_imAction, info);
        }

        void SetupInputManager() {
            auto* im = InputManager();
            if (!im) return;
            for (std::size_t i = 0, n = im->GetInputCount(0); i < n; ++i) {
                const char* name = im->GetInputName(0, static_cast<int>(i));
                if (name && std::string_view(name) == kInputManagerAction) g_imAction = static_cast<int>(i);
            }
            bool created = false;
            if (g_imAction < 0) {
                g_imAction = im->CreateInput(0, kInputManagerAction);
                created = g_imAction >= 0;
            }
            if (g_imAction < 0) {
                SKSE::log::warn("Editor: InputManager would not create the action {}", kInputManagerAction);
                return;
            }
            im->UpdateListener(0, g_imAction, kInputManagerOwner, "Open the ConditionEngine editor", true);
            if (created) SetInputManagerKey(EffectiveKey());  // its first binding; the player rebinds it in InputManager
            g_via = Via::InputManager;
            SKSE::log::info("Editor: the editor key is InputManager action {} (id {})", kInputManagerAction, g_imAction);
        }

        // The key InputManager has now (it may have been rebound there)
        KeyCombo InputManagerKey(bool& gamepad) {
            gamepad = false;
            auto* im = InputManager();
            if (!im || g_imAction < 0) return {};
            const auto info = im->GetActionInfo(g_imAction);
            if (!info.isValid) return {};
            gamepad = info.gamepadMainKey != 0;
            return {ScanCodeOf(info.pcMainKey), ModsOfVirtualKey(info.pcModifierKey)};
        }

        // Magelight: (re)binds a bare key and the VR button. False and g_keyError when another mod holds the key.
        bool BindMagelight(std::uint32_t key, bool pause) {
            if (!g_api || !g_view) return false;
            g_api->BindHotkey(g_view, 0, ML::kHotkeyActionUnbind);
            const auto action = pause ? ML::kHotkeyActionToggleUIModePaused : ML::kHotkeyActionToggleUIMode;
            bool ok = true;
            if (key) {
                if (g_api->BindHotkey(g_view, key, action) != ML::Result::Ok) {
                    g_keyError = KeyName(key) + " is not free: " + std::string(g_api->GetLastErrorMessage(g_mod));
                    SKSE::log::warn("Editor: {}", g_keyError);
                    ok = false;
                }
            }
            const auto& s = Dials::GetSettings();
            if (REL::Module::IsVR() && s.vrButton && g_api->BindVRHotkey) {
                ML::VRHotkeyDesc vr{};
                vr.size = sizeof(vr);
                vr.button = s.vrButton;
                vr.modifier = s.vrModifier;
                vr.action = action;
                if (g_api->BindVRHotkey(g_view, &vr) != ML::Result::Ok) {
                    SKSE::log::warn("Editor: VR button {} not bound: {}", s.vrButton, g_api->GetLastErrorMessage(g_mod));
                }
            }
            return ok;
        }

        // A combo is watched here; a bare key goes to Magelight. False when Magelight refused the key.
        bool Bind(const KeyCombo& combo, bool pause) {
            if (combo.key && combo.mods) {
                BindMagelight(0, pause);  // only the VR button
                return true;
            }
            return BindMagelight(combo.key, pause);
        }

        void ApplyBinding() {
            g_combo = EffectiveKey();
            g_pause = EffectivePause();
            g_keyError.clear();
            if (g_via == Via::InputManager) {
                BindMagelight(0, g_pause);  // the VR button only: InputManager has the keyboard
                return;
            }
            if (!Bind(g_combo, g_pause) || !g_combo.key) g_via = Via::None;
            else g_via = g_combo.mods ? Via::Watched : Via::Magelight;
        }

        // Keys with modifiers, outside InputManager (main thread, between frames)
        class KeySink final : public RE::BSTEventSink<RE::InputEvent*> {
        public:
            static KeySink* Get() {
                static KeySink sink;
                return &sink;
            }
            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events, RE::BSTEventSource<RE::InputEvent*>*) override {
                if (g_via != Via::Watched || !events) return RE::BSEventNotifyControl::kContinue;
                for (auto* e = *events; e; e = e->next) {
                    if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) continue;
                    const auto* button = e->AsButtonEvent();
                    if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard || !button->IsDown()) continue;
                    if (button->GetIDCode() != g_combo.key || !ModifiersHeld(g_combo.mods)) continue;
                    Toggle();
                    break;
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        nlohmann::json HotkeyStatus() {
            nlohmann::json j;
            KeyCombo combo = g_combo;
            bool gamepad = false;
            if (g_via == Via::InputManager) combo = InputManagerKey(gamepad);
            j["editorKey"] = ComboName(combo);
            j["editorKeyVia"] = g_via == Via::InputManager ? "InputManager"
                                : g_via == Via::None       ? "none"
                                                           : "ConditionEngine";
            j["editorKeyGamepad"] = gamepad;
            j["editorKeyError"] = g_keyError;
            j["pauseGame"] = g_pause;
            return j;
        }

        Dials::Outcome SetEditorKey(const nlohmann::json& m) {
            Dials::Outcome o;
            nlohmann::json prefs = nlohmann::json::object();
            KeyCombo combo = g_combo;
            if (auto it = m.find("key"); it != m.end() && it->is_string()) {
                const auto name = it->get<std::string>();
                combo = ParseCombo(name);
                if (!name.empty() && !combo.key) {
                    o.errors.push_back("unknown key '" + name + "'");
                    return o;
                }
                prefs["hotkey"] = ComboName(combo);
            }
            const bool pause = m.contains("pauseGame") && m["pauseGame"].is_boolean() ? m["pauseGame"].get<bool>() : g_pause;
            prefs["pauseGame"] = pause;

            if (g_via == Via::InputManager) {
                if (prefs.contains("hotkey") && !SetInputManagerKey(combo)) {
                    o.errors.push_back("InputManager did not take the new key; change it in InputManager's menu");
                    return o;
                }
            } else if (!Bind(combo, pause)) {
                o.errors.push_back(g_keyError);
                Bind(g_combo, g_pause);  // the old key back
                return o;
            }
            std::string error;
            if (!Service::Get().SetEditorPrefs(prefs, error)) o.warnings.push_back(error);
            ApplyBinding();
            o.ok = true;
            return o;
        }

        // InputManager reports a bound action as a mod event (numArg = action id, strArg = action name)
        class InputManagerSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
        public:
            RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* e,
                                                  RE::BSTEventSource<SKSE::ModCallbackEvent>*) override {
                if (!e || g_imAction < 0 || std::string_view(e->eventName.c_str()) != "InputManager_ActionTriggered") {
                    return RE::BSEventNotifyControl::kContinue;
                }
                const bool ours = static_cast<int>(e->numArg) == g_imAction ||
                                  std::string_view(e->strArg.c_str()) == kInputManagerAction;
                if (!ours) return RE::BSEventNotifyControl::kContinue;
                if (auto* tasks = SKSE::GetTaskInterface()) tasks->AddTask([]() { Toggle(); });
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        // Game thread
        void Handle(const std::string& text) {
            nlohmann::json m;
            try {
                m = nlohmann::json::parse(text);
            } catch (...) {
                SKSE::log::warn("Editor: a message that is not JSON");
                return;
            }
            auto& service = Service::Get();
            auto& engine = service.GetEngine();
            const auto op = m.value("op", std::string());
            const auto id = m.value("id", std::string());
            Dials::Outcome ok{true, {}, {}};

            if (op == "ready" || op == "state") {
                SendState();
                if (!g_focus.empty()) {
                    Send("dials_focus", nlohmann::json(g_focus).dump());
                    g_focus.clear();
                }
            } else if (op == "catalog") {
                if (CE::Ready()) {
                    const auto& ce = CE::Get();
                    const char* filter = R"({"for": "User", "include": ["types", "values", "conditions", "categories"], "offered": false})";
                    Send("dials_catalog", CE::ReadString([&](char* b, std::uint32_t n) { return ce.GetCatalog(filter, b, n); }));
                }
            } else if (op == "validate") {
                Reply(m, service.Validate(id, m.value("json", nlohmann::json::object())));
            } else if (op == "save") {
                const auto o = service.SaveUser(id, m.value("json", nlohmann::json::object()), m.value("previousId", ""));
                Reply(m, o);
                SendState();
            } else if (op == "revert" || op == "delete") {
                Reply(m, {service.RevertUser(id), {}, {}});
                SendState();
            } else if (op == "set" || op == "add") {
                const double v = m.value("value", 0.0);
                if (op == "set") engine.Set(id, ActorFrom(m), v, "editor");
                else engine.Add(id, ActorFrom(m), v, "editor");
                service.Publish();
                Reply(m, ok);
            } else if (op == "reset" || op == "toggle" || op == "cycle") {
                if (op == "reset") engine.Reset(id, ActorFrom(m), "editor");
                else if (op == "toggle") engine.Toggle(id, ActorFrom(m), "editor");
                else engine.Cycle(id, ActorFrom(m), m.value("step", 1), "editor");
                service.Publish();
                Reply(m, ok);
            } else if (op == "fire") {
                const bool fired = engine.FireRule(id, m.value("rule", 0), ActorFrom(m));
                service.Publish();
                Reply(m, {fired, fired ? std::vector<std::string>{} : std::vector<std::string>{"no such rule (save first)"}, {}});
            } else if (op == "signal") {
                EventInfo info;
                info.actor = kPlayer;
                info.number = m.value("number", 0.0);
                info.text = m.value("text", "");
                engine.OnModEvent(m.value("event", ""), info);
                service.Publish();
                Reply(m, ok);
            } else if (op == "explain") {
                Reply(m, ok, service.Explain(m.value("expr", ""), ActorFrom(m), m.value("value", false)));
            } else if (op == "saveNamed") {
                const bool isValue = m.value("kind", "") == "value";
                const auto o = service.SaveUserNamed(isValue, m.value("name", ""), m.value("json", nlohmann::json::object()),
                                                     m.value("previousName", ""));
                Reply(m, o);
                SendState();
            } else if (op == "deleteNamed") {
                const bool isValue = m.value("kind", "") == "value";
                Reply(m, {service.DeleteUserNamed(isValue, m.value("name", "")), {}, {}});
                SendState();
            } else if (op == "skyrimnet") {
                auto status = SkyrimNet::Status();
                status["dials"] = service.DialsFor(ActorFrom(m) ? ActorFrom(m) : kPlayer);
                Reply(m, ok, status.dump());
            } else if (op == "setEditorKey") {
                Reply(m, SetEditorKey(m));
                SendState();
            } else if (op == "reload") {
                service.ReloadFiles();
                SendState();
            } else if (op == "close") {
                Close();
            } else {
                SKSE::log::warn("Editor: unknown op '{}'", op);
            }
        }

        struct Box {
            std::string text;
        };

        void RunBox(void* user) {
            std::unique_ptr<Box> box(static_cast<Box*>(user));
            try {
                Handle(box->text);
            } catch (const std::exception& e) {
                SKSE::log::error("Editor: {}", e.what());
            }
        }

        // Render thread, mid-frame: copy and hand to the game thread
        void OnMessage(ML::ViewId, const char* argument, void*) {
            if (!g_api) return;
            auto* box = new Box{argument ? argument : ""};
            if (g_api->PostGameTask(&RunBox, box) != ML::Result::Ok) delete box;
        }

        void OnEvent(const ML::EventData* ev, void*) {
            if (!ev || ev->view != g_view) return;
            switch (ev->type) {
                case ML::Event::UIModeEntered:
                    g_open.store(true);
                    Service::Get().onChanged = [] { SendLive(false); };
                    SendState();
                    break;
                case ML::Event::UIModeExited:
                case ML::Event::UIModeRefused:
                    g_open.store(false);
                    Service::Get().onChanged = nullptr;
                    if (g_api) g_api->ShowView(g_view, false);
                    break;
                case ML::Event::ViewLoadFailed:
                    SKSE::log::error("Editor: the page did not load: {}", ev->detail ? ev->detail : "");
                    break;
                case ML::Event::ViewReloaded:
                    if (g_open.load()) SendState();
                    break;
                default: break;
            }
        }
    }

    void Install() {
        g_api = ML::RequestApi4();
        if (!g_api) {
            SKSE::log::info("Editor: Magelight UI not installed; the editor is off (everything else runs)");
            return;
        }
        if (g_api->hostVersionNumber < kMinHost) {
            SKSE::log::warn("Editor: Magelight {} is older than 0.30.1; the editor is off", g_api->hostVersion);
            g_api = nullptr;
            return;
        }
        ML::ModDesc d{};
        d.size = sizeof(d);
        d.modId = "ConditionEngine";
        d.displayName = "ConditionEngine";
        d.minHostVersion = kMinHost;
        d.callbackThread = ML::CallbackThread::GameThread;
        d.onEvent = &OnEvent;
        if (g_api->RegisterMod(&d, &g_mod) != ML::Result::Ok) {
            SKSE::log::warn("Editor: Magelight refused the mod registration: {}", g_api->GetLastErrorMessage(0));
            g_api = nullptr;
            return;
        }
        const auto page = PagePath();
        if (!std::filesystem::exists(page)) {
            SKSE::log::warn("Editor: editor page missing ({}); the editor is off", page);
            return;
        }
        ML::ViewDesc vd{};
        vd.size = sizeof(vd);
        vd.name = "editor";
        vd.htmlPath = page.c_str();
        vd.fullscreen = true;
        vd.startVisible = false;
        vd.layer = ML::Layer::Panel;
        if (g_api->CreateViewEx(g_mod, &vd, &g_view) != ML::Result::Ok) {
            SKSE::log::warn("Editor: the editor view was not created: {}", g_api->GetLastErrorMessage(g_mod));
            g_view = 0;
            return;
        }
        g_api->RegisterJSListenerEx(g_view, "dials", &OnMessage, nullptr);
        SKSE::log::info("Editor: editor ready ({})", page);
    }

    void InstallHotkey() {
        if (!Available()) return;
        static InputManagerSink sink;
        SetupInputManager();
        if (g_via == Via::InputManager) {
            if (auto* mods = SKSE::GetModCallbackEventSource()) mods->AddEventSink(&sink);
        } else if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
            input->AddEventSink(KeySink::Get());  // keys with Shift / Ctrl / Alt (Magelight binds bare keys)
        }
        ApplyBinding();
        const auto status = HotkeyStatus();
        SKSE::log::info("Editor: opened by {} ({})",
                        status["editorKey"].get<std::string>().empty() ? "no key" : status["editorKey"].get<std::string>(),
                        status["editorKeyVia"].get<std::string>());
    }

    bool Available() { return g_api && g_view; }

    void Open(const std::string& dial) {
        if (!Available()) {
            if (auto* log = RE::ConsoleLog::GetSingleton()) log->Print("Editor: the editor needs Magelight UI 0.30.1 or newer");
            return;
        }
        g_focus = dial;
        g_api->ShowView(g_view, true);
        const auto flags = g_pause ? ML::kUIModeFlagPause : ML::kUIModeFlagNone;
        if (g_api->RequestUIMode(g_view, flags) != ML::Result::Ok) {
            SKSE::log::warn("Editor: the editor could not take the UI: {}", g_api->GetLastErrorMessage(g_mod));
            g_api->ShowView(g_view, false);
            return;
        }
        if (!dial.empty()) Send("dials_focus", nlohmann::json(dial).dump());
    }

    void Close() {
        if (!Available()) return;
        g_api->ReleaseUIMode(g_mod);
        g_api->ShowView(g_view, false);
    }

} // namespace ConditionEngine::Editor
