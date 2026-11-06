#pragma once

// Service.h - Dials in the game: where definitions come from (dial files, other mods,
// the player's own in the editor), their ConditionEngine names ($id, @Conditions), the
// tick, and publishing changes (ConditionEngine pushes, the Dials_Changed mod event,
// C API subscribers, the editor).
//
// Definitions come in two layers per id: the base (a preset file's "dials", or a mod's Define)
// and the player's edit (User/User.json). The edit wins; reverting it brings the base back.
// User.json also holds the named conditions and values the player made in the editor.

#include "Engine.h"
#include "GameHost.h"

#include <atomic>
#include <functional>

namespace ConditionEngine::Dials {

    struct Outcome {
        bool ok = false;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        nlohmann::json ToJson() const;
    };

    using ChangedFn = void (*)(const char* id, void* actor, double value, double previous, void* user);

    class Service {
    public:
        static Service& Get();

        // kPostLoad: resolve ConditionEngine. kDataLoaded: Start (files, ticker).
        bool Connect();
        void Start();
        bool Ready() const { return ready_; }
        const std::string& NotReadyReason() const { return why_; }
        Engine& GetEngine() { return engine_; }

        // ── Definitions ──────────────────────────────────────────────────────
        // A mod or script defines (or replaces) a base dial. `json` is the dial object.
        Outcome Define(const std::string& id, const nlohmann::json& json, const std::string& source);
        Outcome DefineText(const std::string& id, const std::string& jsonText, const std::string& source);
        bool Remove(const std::string& id);              // a base dial (API)
        std::size_t RemoveSource(const std::string& source);
        // The editor: save the player's version, revert it, or delete a dial the player made
        Outcome SaveUser(const std::string& id, const nlohmann::json& json, const std::string& previousId);
        bool RevertUser(const std::string& id);
        // Checks a definition without applying it (the editor's live validation)
        Outcome Validate(const std::string& id, const nlohmann::json& json);
        void ReloadFiles();

        // ── Runtime (main thread) ────────────────────────────────────────────
        void Tick();
        void Publish();
        void RequestPublish();  // any thread: Publish on the next main-thread task
        void OnSessionEnd();    // before a load / new game
        void OnSessionStart();  // after it
        bool InSession() const { return session_; }

        // Which game events any enabled rule listens for (the sinks skip the rest)
        bool Uses(GameEvent event) const { return (usedEvents_.load() >> static_cast<int>(event)) & 1u; }
        bool UsesKey(std::uint32_t scanCode) const;

        // ── Information ──────────────────────────────────────────────────────
        nlohmann::json Snapshot();             // every dial, its layers, values; the editor's state
        nlohmann::json LiveValues();           // values and the log tail, for the open editor
        nlohmann::json Describe(const std::string& id);
        std::string Explain(const std::string& expression, ActorKey actor, bool isValue);

        std::uint32_t Subscribe(ChangedFn fn, void* user);
        void Unsubscribe(std::uint32_t id);

        // ── The player's own named conditions and values (the editor) ────────
        // `json`: {"expr", "label", "description", "category", "knowledge"} for a condition,
        // a named value definition for a value. Registered with owner "User".
        Outcome SaveUserNamed(bool isValue, const std::string& name, const nlohmann::json& json, const std::string& previousName);
        bool DeleteUserNamed(bool isValue, const std::string& name);

        // ── The editor's own preferences (User.json "editor") ────────────────
        // {"hotkey": "F10" | "" (none), "pauseGame": bool}; missing keys fall back to the ini
        nlohmann::json EditorPrefs();
        bool SetEditorPrefs(const nlohmann::json& prefs, std::string& error);

        // ── For SkyrimNet ────────────────────────────────────────────────────
        // Every dial's value for this actor, as get_dials() returns it: numbers, toggles as
        // true / false, choice dials as their option label. Any thread.
        nlohmann::json DialsFor(ActorKey actor);
        struct Knowledge {
            std::string condition;  // @name it hangs on
            std::string text;
        };
        std::vector<Knowledge> KnowledgeEntries();
        struct AiAction {
            std::string name;
            std::string description;
            std::vector<std::pair<std::string, std::size_t>> rules;  // (dial, rule index) that fire
        };
        std::vector<AiAction> AiActions();
        std::vector<std::string> SkyrimNetEventTypes();  // "dialogue", ... used by "skyrimnet" rules

        // Hooks (main thread): the editor's view is open (values changed); rules told SkyrimNet
        // something; definitions changed (knowledge, actions, event types may differ)
        std::function<void()> onChanged;
        std::function<void(std::vector<Note>&&)> onNotes;
        std::function<void()> onDefinitionsChanged;
        std::function<void()> onTick;  // after every tick

    private:
        Service();

        struct Layers {
            std::string id;  // as written
            std::optional<nlohmann::json> base;
            std::string baseSource;
            bool baseFromFile = false;
            std::optional<nlohmann::json> user;
            std::vector<std::string> errors;  // why the effective definition failed, if it did
            std::vector<std::string> warnings;
        };

        Outcome Apply(const std::string& id);  // makes the effective layer live (or removes the dial)
        Outcome Prepare(const std::string& id, const nlohmann::json& json, const std::string& source,
                        std::optional<DialDef>& out);
        void RegisterNames(const DialDef& def, std::vector<std::string>& errors);
        void UnregisterNames(const std::string& id);
        void RefreshUsage();
        void LoadUserFile();
        bool WriteUserFile(std::string& error);
        std::map<std::string, nlohmann::json> ReadDialFiles(std::vector<std::string>& errors,
                                                            std::map<std::string, std::string>& sources);

        GameHost host_;
        Engine engine_;
        bool ready_ = false;
        bool started_ = false;
        std::string why_;
        std::atomic<bool> session_{false};

        std::recursive_mutex mutex_;  // layers and names
        std::map<std::string, Layers> layers_;  // lower-case id
        std::map<std::string, std::vector<std::string>> conditionNames_;  // lower-case id -> @names it registered
        std::map<std::string, nlohmann::json> userConditions_;  // name as written -> definition
        std::map<std::string, nlohmann::json> userValues_;
        nlohmann::json editorPrefs_ = nlohmann::json::object();
        void RegisterUserNamed(bool isValue, const std::string& name, const nlohmann::json& json, std::vector<std::string>& errors);
        void DefinitionsChanged();
        std::vector<std::string> fileErrors_;

        std::atomic<std::uint32_t> usedEvents_{0};
        std::mutex keysMutex_;
        std::vector<std::uint32_t> usedKeys_;

        std::atomic<bool> publishQueued_{false};
        std::chrono::steady_clock::time_point lastTick_{};

        // Mod events rules sent ("send"): sent at the start of the next tick, so two dials that answer
        // each other's events go one round per tick instead of looping at once
        void QueueSends();
        void FlushSends();
        std::mutex outboxMutex_;
        std::vector<Send> outbox_;

    public:
        // While a dial's event is being sent (main thread): that dial's id, so it does not hear it
        static const std::string& SendingDial();

        std::mutex subscribersMutex_;
        std::map<std::uint32_t, std::pair<ChangedFn, void*>> subscribers_;
        std::uint32_t nextSubscriber_ = 1;
    };

} // namespace ConditionEngine::Dials
