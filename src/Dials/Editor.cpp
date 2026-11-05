#include "Editor.h"
#include "CEApi.h"
#include "Service.h"
#include "Settings.h"
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

        void SendState() { Send("dials_state", Service::Get().Snapshot().dump()); }

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
        if (const auto key = Dials::GetSettings().editorKey) {
            const auto action = Dials::GetSettings().pauseGame ? ML::kHotkeyActionToggleUIModePaused : ML::kHotkeyActionToggleUIMode;
            if (g_api->BindHotkey(g_view, key, action) != ML::Result::Ok) {
                SKSE::log::warn("Editor: editor hotkey {} not bound: {}", KeyName(key), g_api->GetLastErrorMessage(g_mod));
            }
        }
        SKSE::log::info("Editor: editor ready ({})", page);
    }

    bool Available() { return g_api && g_view; }

    void Open(const std::string& dial) {
        if (!Available()) {
            if (auto* log = RE::ConsoleLog::GetSingleton()) log->Print("Editor: the editor needs Magelight UI 0.30.1 or newer");
            return;
        }
        g_focus = dial;
        g_api->ShowView(g_view, true);
        const auto flags = Dials::GetSettings().pauseGame ? ML::kUIModeFlagPause : ML::kUIModeFlagNone;
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
