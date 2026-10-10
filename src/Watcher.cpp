#include "ConditionEngine/Watcher.h"
#include "ConditionEngine/EvaluationContext.h"

#include <thread>

namespace ConditionEngine {

    namespace {
        // State with no reliable game event; re-checked every poll tick. Location and
        // combat have events too, but polling them catches changes the events miss.
        constexpr EventDependency kPolledDependencies =
            EventDependency::Time | EventDependency::Date | EventDependency::Weather | EventDependency::Location |
            EventDependency::Combat | EventDependency::Equipment | EventDependency::Faction |
            EventDependency::ActorValue | EventDependency::GlobalVariable | EventDependency::Script |
            EventDependency::Temperature | EventDependency::Swimming | EventDependency::Sleeping |
            EventDependency::Mounting | EventDependency::InWater | EventDependency::LoveScene |
            EventDependency::Polled;

        RE::Actor* ResolveActor(RE::FormID id) {
            if (id == 0) return RE::PlayerCharacter::GetSingleton();
            return RE::TESForm::LookupByID<RE::Actor>(id);
        }

        bool IsWorldReady() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            return player && player->Is3DLoaded();
        }

        RE::FormID ActorKey(RE::Actor* actor) {
            if (!actor || actor == RE::PlayerCharacter::GetSingleton()) return 0;
            return actor->GetFormID();
        }

        class EventSink final : public RE::BSTEventSink<RE::TESActorLocationChangeEvent>,
                                public RE::BSTEventSink<RE::TESCombatEvent>,
                                public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>,
                                public RE::BSTEventSink<RE::TESEquipEvent>,
                                public RE::BSTEventSink<RE::TESQuestStageEvent>,
                                public RE::BSTEventSink<RE::TESQuestStartStopEvent> {
        public:
            static EventSink* GetSingleton() {
                static EventSink instance;
                return &instance;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESActorLocationChangeEvent* e,
                                                  RE::BSTEventSource<RE::TESActorLocationChangeEvent>*) override {
                if (e && e->actor) Fire(EventDependency::Location, e->actor->As<RE::Actor>());
                return RE::BSEventNotifyControl::kContinue;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* e,
                                                  RE::BSTEventSource<RE::TESCombatEvent>*) override {
                if (e && e->actor) Fire(EventDependency::Combat, e->actor->As<RE::Actor>());
                return RE::BSEventNotifyControl::kContinue;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESCellFullyLoadedEvent*,
                                                  RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override {
                Fire(EventDependency::Location | EventDependency::Time, nullptr);
                return RE::BSEventNotifyControl::kContinue;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* e,
                                                  RE::BSTEventSource<RE::TESEquipEvent>*) override {
                if (e && e->actor) Fire(EventDependency::Equipment | EventDependency::ActorValue, e->actor->As<RE::Actor>());
                return RE::BSEventNotifyControl::kContinue;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESQuestStageEvent*,
                                                  RE::BSTEventSource<RE::TESQuestStageEvent>*) override {
                Fire(EventDependency::Quest, nullptr);
                return RE::BSEventNotifyControl::kContinue;
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::TESQuestStartStopEvent*,
                                                  RE::BSTEventSource<RE::TESQuestStartStopEvent>*) override {
                Fire(EventDependency::Quest, nullptr);
                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            static void Fire(EventDependency events, RE::Actor* actor) {
                auto& watcher = Watcher::Get();
                if (watcher.GetWatchCount() == 0) return;
                // Events fire mid-update; evaluate on the next task tick when state has settled
                const RE::FormID key = ActorKey(actor);
                const bool allActors = actor == nullptr;
                SKSE::GetTaskInterface()->AddTask([events, key, allActors]() {
                    RE::Actor* target = allActors ? nullptr : ResolveActor(key);
                    if (!allActors && !target) return;
                    Watcher::Get().NotifyEvent(events, target);
                });
            }
        };
    }

    Watcher& Watcher::Get() {
        static Watcher instance;
        return instance;
    }

    WatchId Watcher::Watch(std::shared_ptr<const Condition> condition, RE::Actor* actor, WatchCallback callback,
                           bool fireImmediately) {
        if (!condition || !callback) return 0;

        Entry entry;
        entry.dependencies = condition->GetEffectiveEventDependencies();
        entry.condition = std::move(condition);
        entry.actorId = ActorKey(actor);
        entry.callback = std::move(callback);

        // Seed the baseline so the first callback reflects a real change
        if (IsWorldReady()) {
            if (auto* target = ResolveActor(entry.actorId)) {
                EvaluationContext context(target);
                entry.lastResult = entry.condition->Evaluate(context);
            }
        }

        WatchId id;
        std::optional<bool> initial = entry.lastResult;
        WatchCallback initialCallback = fireImmediately ? entry.callback : nullptr;
        RE::FormID actorId = entry.actorId;
        {
            std::lock_guard lock(mutex_);
            id = nextId_++;
            if (nextId_ == 0) nextId_ = 1;
            entries_.emplace(id, std::move(entry));
        }

        if (initialCallback && initial.has_value()) {
            initialCallback(id, ResolveActor(actorId), *initial);
        }
        return id;
    }

    void Watcher::Unwatch(WatchId id) {
        std::lock_guard lock(mutex_);
        entries_.erase(id);
    }

    std::size_t Watcher::GetWatchCount() const {
        std::lock_guard lock(mutex_);
        return entries_.size();
    }

    void Watcher::NotifyEvent(EventDependency events, RE::Actor* actor) {
        Reevaluate(events, actor ? ActorKey(actor) : static_cast<RE::FormID>(-1));
    }

    void Watcher::Poll() {
        Reevaluate(kPolledDependencies, static_cast<RE::FormID>(-1));
    }

    void Watcher::Reevaluate(EventDependency events, RE::FormID onlyActor) {
        if (!IsWorldReady()) return;
        constexpr RE::FormID kAnyActor = static_cast<RE::FormID>(-1);

        struct Work {
            WatchId id;
            std::shared_ptr<const Condition> condition;
            RE::FormID actorId;
        };
        std::vector<Work> work;
        {
            std::lock_guard lock(mutex_);
            for (const auto& [id, entry] : entries_) {
                if (!HasDependency(entry.dependencies, events)) continue;
                if (onlyActor != kAnyActor && entry.actorId != onlyActor) continue;
                work.push_back({id, entry.condition, entry.actorId});
            }
        }
        if (work.empty()) return;

        // One snapshot per actor per pass
        std::unordered_map<RE::FormID, std::unique_ptr<EvaluationContext>> contexts;
        std::vector<std::pair<WatchId, bool>> results;
        results.reserve(work.size());
        for (const auto& item : work) {
            auto& context = contexts[item.actorId];
            if (!context) {
                auto* target = ResolveActor(item.actorId);
                if (!target) continue;
                context = std::make_unique<EvaluationContext>(target);
            }
            results.emplace_back(item.id, item.condition->Evaluate(*context));
        }

        struct Fire {
            WatchCallback callback;
            WatchId id;
            RE::FormID actorId;
            bool result;
        };
        std::vector<Fire> toFire;
        {
            std::lock_guard lock(mutex_);
            for (const auto& [id, result] : results) {
                auto it = entries_.find(id);
                if (it == entries_.end()) continue;  // unwatched meanwhile
                auto& entry = it->second;
                const bool changed = !entry.lastResult.has_value() || *entry.lastResult != result;
                entry.lastResult = result;
                if (changed) toFire.push_back({entry.callback, id, entry.actorId, result});
            }
        }

        // Callbacks run unlocked so they can Watch/Unwatch freely
        for (const auto& fire : toFire) {
            try {
                fire.callback(fire.id, ResolveActor(fire.actorId), fire.result);
            } catch (const std::exception& e) {
                SKSE::log::error("ConditionEngine: watch {} callback threw: {}", fire.id, e.what());
            }
        }
    }

    void Watcher::Install(std::chrono::milliseconds pollInterval) {
        {
            std::lock_guard lock(mutex_);
            if (installed_) return;
            installed_ = true;
        }

        if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
            auto* sink = EventSink::GetSingleton();
            holder->AddEventSink<RE::TESActorLocationChangeEvent>(sink);
            holder->AddEventSink<RE::TESCombatEvent>(sink);
            holder->AddEventSink<RE::TESCellFullyLoadedEvent>(sink);
            holder->AddEventSink<RE::TESEquipEvent>(sink);
            holder->AddEventSink<RE::TESQuestStageEvent>(sink);
            holder->AddEventSink<RE::TESQuestStartStopEvent>(sink);
        } else {
            SKSE::log::error("ConditionEngine: ScriptEventSourceHolder unavailable - watches will only poll");
        }

        // Poll on a background timer, evaluate on the main thread
        std::thread([pollInterval]() {
            for (;;) {
                std::this_thread::sleep_for(pollInterval);
                if (Watcher::Get().GetWatchCount() == 0) continue;
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([]() { Watcher::Get().Poll(); });
                }
            }
        }).detach();

        SKSE::log::info("ConditionEngine: watcher installed (poll every {} ms)", pollInterval.count());
    }

} // namespace ConditionEngine
