#include "ConditionEngine/Watcher.h"
#include "ConditionEngine/Conditions/ActorConditions.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Registry.h"

#include <cmath>
#include <thread>

namespace ConditionEngine {

    namespace {
        constexpr auto kAllDependencies = static_cast<EventDependency>((1u << kEventDependencyBitCount) - 1);
        constexpr RE::FormID kAnyActor = static_cast<RE::FormID>(-1);

        // Snapshot flag bits → dependency fired when the bit flips
        enum SnapshotFlag : std::uint32_t {
            kCombat = 1u << 0,
            kSwimming = 1u << 1,
            kSleeping = 1u << 2,
            kMounted = 1u << 3,
            kInWater = 1u << 4,
            kLoveScene = 1u << 5,
            kActorStateShift = 8,  // ComputeActorStateFlags() bits live above this
        };

        RE::Actor* ResolveActor(RE::FormID id) {
            if (id == 0) return RE::PlayerCharacter::GetSingleton();
            return RE::TESForm::LookupByID<RE::Actor>(id);
        }

        bool IsWorldReady() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            return player && player->Is3DLoaded();
        }

        RE::FormID ActorKey(RE::Actor* actor) {
            if (!actor || actor->IsPlayerRef()) return 0;
            return actor->GetFormID();
        }

        std::uint32_t ComputeSnapshotFlags(RE::Actor* actor) {
            std::uint32_t flags = 0;
            if (actor->IsInCombat()) flags |= kCombat;
            if (auto* state = actor->AsActorState(); state && state->IsSwimming()) flags |= kSwimming;
            if (IsActorSleeping(actor)) flags |= kSleeping;
            if (actor->IsOnMount()) flags |= kMounted;
            if (actor->IsInWater()) flags |= kInWater;
            if (Registry::Get().QueryLoveScene(actor)) flags |= kLoveScene;
            flags |= ComputeActorStateFlags(actor) << kActorStateShift;
            return flags;
        }

        EventDependency DiffFlags(std::uint32_t before, std::uint32_t after) {
            const auto changed = before ^ after;
            EventDependency deps = EventDependency::None;
            if (changed & kCombat) deps |= EventDependency::Combat;
            if (changed & kSwimming) deps |= EventDependency::Swimming;
            if (changed & kSleeping) deps |= EventDependency::Sleeping;
            if (changed & kMounted) deps |= EventDependency::Mounting;
            if (changed & kInWater) deps |= EventDependency::InWater;
            if (changed & kLoveScene) deps |= EventDependency::LoveScene;
            if (changed >> kActorStateShift) deps |= EventDependency::ActorState;
            return deps;
        }

        // Posts re-evaluation to the next main-thread task: events fire mid-update
        void Fire(EventDependency events, RE::Actor* actor) {
            auto& watcher = Watcher::Get();
            if (watcher.GetWatchCount() == 0) return;
            if (actor && !watcher.IsWatchingActor(actor)) return;
            const RE::FormID key = ActorKey(actor);
            const bool allActors = actor == nullptr;
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([events, key, allActors]() {
                    RE::Actor* target = allActors ? nullptr : ResolveActor(key);
                    if (!allActors && !target) return;
                    Watcher::Get().NotifyEvent(events, target);
                });
            }
        }

        RE::Actor* AsActor(const RE::NiPointer<RE::TESObjectREFR>& ref) {
            return ref ? ref->As<RE::Actor>() : nullptr;
        }

        class EventSink final : public RE::BSTEventSink<RE::TESActorLocationChangeEvent>,
                                public RE::BSTEventSink<RE::TESCombatEvent>,
                                public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>,
                                public RE::BSTEventSink<RE::TESEquipEvent>,
                                public RE::BSTEventSink<RE::TESContainerChangedEvent>,
                                public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>,
                                public RE::BSTEventSink<RE::TESQuestStageEvent>,
                                public RE::BSTEventSink<RE::TESQuestStartStopEvent>,
                                public RE::BSTEventSink<RE::TESSleepStopEvent>,
                                public RE::BSTEventSink<RE::TESWaitStopEvent>,
                                public RE::BSTEventSink<RE::TESLoadGameEvent>,
                                public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
        public:
            static EventSink* GetSingleton() {
                static EventSink instance;
                return &instance;
            }

            using Control = RE::BSEventNotifyControl;

            Control ProcessEvent(const RE::TESActorLocationChangeEvent* e,
                                 RE::BSTEventSource<RE::TESActorLocationChangeEvent>*) override {
                if (e) Fire(EventDependency::Location, AsActor(e->actor));
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESCombatEvent* e, RE::BSTEventSource<RE::TESCombatEvent>*) override {
                if (e) Fire(EventDependency::Combat, AsActor(e->actor));
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESCellFullyLoadedEvent*,
                                 RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override {
                Fire(EventDependency::Location | EventDependency::Time | EventDependency::Weather, nullptr);
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESEquipEvent* e, RE::BSTEventSource<RE::TESEquipEvent>*) override {
                if (e) Fire(EventDependency::Equipment | EventDependency::ActorValue, AsActor(e->actor));
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESContainerChangedEvent* e,
                                 RE::BSTEventSource<RE::TESContainerChangedEvent>*) override {
                if (!e) return Control::kContinue;
                for (auto id : {e->oldContainer, e->newContainer}) {
                    if (auto* actor = id ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr) {
                        Fire(EventDependency::Inventory, actor);
                    }
                }
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESMagicEffectApplyEvent* e,
                                 RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override {
                if (e) Fire(EventDependency::MagicEffect, AsActor(e->target));
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESQuestStageEvent*, RE::BSTEventSource<RE::TESQuestStageEvent>*) override {
                Fire(EventDependency::Quest, nullptr);
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESQuestStartStopEvent*,
                                 RE::BSTEventSource<RE::TESQuestStartStopEvent>*) override {
                Fire(EventDependency::Quest, nullptr);
                return Control::kContinue;
            }

            // Sleeping/waiting jumps game time
            Control ProcessEvent(const RE::TESSleepStopEvent*, RE::BSTEventSource<RE::TESSleepStopEvent>*) override {
                Fire(EventDependency::Time | EventDependency::Date | EventDependency::Weather, nullptr);
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESWaitStopEvent*, RE::BSTEventSource<RE::TESWaitStopEvent>*) override {
                Fire(EventDependency::Time | EventDependency::Date | EventDependency::Weather, nullptr);
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESLoadGameEvent*, RE::BSTEventSource<RE::TESLoadGameEvent>*) override {
                Fire(kAllDependencies, nullptr);
                return Control::kContinue;
            }

            // Inventory, magic and level-up menus change player state without events
            Control ProcessEvent(const RE::MenuOpenCloseEvent* e, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
                if (e && !e->opening) {
                    Fire(EventDependency::Inventory | EventDependency::Equipment | EventDependency::MagicEffect |
                             EventDependency::ActorValue | EventDependency::ActorState,
                         RE::PlayerCharacter::GetSingleton());
                }
                return Control::kContinue;
            }
        };
    }

    Watcher& Watcher::Get() {
        static Watcher instance;
        return instance;
    }

    WatchId Watcher::Watch(std::shared_ptr<const Condition> condition, RE::Actor* actor, WatchCallback callback,
                           const WatchOptions& options) {
        if (!condition || !callback) return 0;

        Entry entry;
        entry.dependencies = condition->GetEffectiveEventDependencies() | options.extraDependencies;
        entry.condition = std::move(condition);
        entry.actorId = ActorKey(actor);
        entry.callback = std::move(callback);
        entry.pollInterval = options.pollInterval;
        entry.lastEvaluated = Clock::now();

        // Seed the baseline so the first callback reflects a real change
        if (IsWorldReady()) {
            if (auto* target = ResolveActor(entry.actorId)) {
                EvaluationContext context(target);
                entry.lastResult = entry.condition->Evaluate(context);
            }
        }

        WatchId id;
        const std::optional<bool> initial = entry.lastResult;
        WatchCallback initialCallback = options.fireImmediately ? entry.callback : nullptr;
        const RE::FormID actorId = entry.actorId;
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

    bool Watcher::IsWatchingActor(RE::Actor* actor) const {
        const RE::FormID key = ActorKey(actor);
        std::lock_guard lock(mutex_);
        return std::any_of(entries_.begin(), entries_.end(),
                           [key](const auto& kv) { return kv.second.actorId == key; });
    }

    std::optional<bool> Watcher::Refresh(WatchId id) {
        if (!IsWorldReady()) return std::nullopt;
        Evaluate([id](WatchId candidate, const Entry&) { return candidate == id; });
        std::lock_guard lock(mutex_);
        auto it = entries_.find(id);
        return it != entries_.end() ? it->second.lastResult : std::nullopt;
    }

    void Watcher::NotifyEvent(EventDependency events, RE::Actor* actor) {
        if (!IsWorldReady()) return;
        const RE::FormID only = actor ? ActorKey(actor) : kAnyActor;
        Evaluate([events, only](WatchId, const Entry& entry) {
            return HasDependency(entry.dependencies, events) && (only == kAnyActor || entry.actorId == only);
        });
    }

    void Watcher::SetPollInterval(std::chrono::milliseconds interval) {
        if (interval.count() <= 0) return;
        std::lock_guard lock(mutex_);
        pollInterval_ = interval;
    }

    std::chrono::milliseconds Watcher::GetPollInterval() const {
        std::lock_guard lock(mutex_);
        return pollInterval_;
    }

    void Watcher::Tick() {
        if (!IsWorldReady()) return;

        // ── World state ──────────────────────────────────────────────────────
        EventDependency worldEvents = EventDependency::None;
        if (auto* calendar = RE::Calendar::GetSingleton()) {
            const double days = calendar->GetDaysPassed();
            const auto minute = static_cast<std::int64_t>(std::floor(days * 24.0 * 60.0));
            const auto day = static_cast<std::int64_t>(std::floor(days));
            if (world_.gameMinute >= 0 && minute != world_.gameMinute) worldEvents |= EventDependency::Time;
            if (world_.gameDay >= 0 && day != world_.gameDay) worldEvents |= EventDependency::Date;
            world_.gameMinute = minute;
            world_.gameDay = day;
        }
        if (auto* sky = RE::Sky::GetSingleton()) {
            if (world_.weather && sky->currentWeather != world_.weather) worldEvents |= EventDependency::Weather;
            world_.weather = sky->currentWeather;
        }

        // ── Watched actors ───────────────────────────────────────────────────
        std::unordered_set<RE::FormID> watched;
        std::chrono::milliseconds defaultInterval;
        {
            std::lock_guard lock(mutex_);
            for (const auto& [id, entry] : entries_) watched.insert(entry.actorId);
            defaultInterval = pollInterval_;
        }

        std::unordered_map<RE::FormID, EventDependency> actorEvents;
        for (auto id : watched) {
            auto* actor = ResolveActor(id);
            if (!actor) continue;

            ActorSnapshot now;
            now.cell = actor->GetParentCell();
            now.location = actor->GetCurrentLocation();
            now.worldspace = actor->GetWorldspace();
            now.flags = ComputeSnapshotFlags(actor);

            EventDependency events = worldEvents;
            if (auto it = actors_.find(id); it != actors_.end()) {
                const auto& before = it->second;
                if (before.cell != now.cell || before.location != now.location || before.worldspace != now.worldspace) {
                    events |= EventDependency::Location;
                }
                events |= DiffFlags(before.flags, now.flags);
            }
            actors_[id] = now;
            actorEvents[id] = events;
        }
        std::erase_if(actors_, [&](const auto& kv) { return !watched.contains(kv.first); });

        // ── Evaluate what changed, plus due full polls ───────────────────────
        const auto now = Clock::now();
        Evaluate([&](WatchId, const Entry& entry) {
            if (auto it = actorEvents.find(entry.actorId);
                it != actorEvents.end() && HasDependency(entry.dependencies, it->second)) {
                return true;
            }
            const auto interval = entry.pollInterval.count() == 0 ? defaultInterval : entry.pollInterval;
            return interval != WatchOptions::kNever && now - entry.lastEvaluated >= interval;
        });
    }

    void Watcher::Evaluate(const std::function<bool(WatchId, const Entry&)>& select) {
        struct Work {
            WatchId id;
            std::shared_ptr<const Condition> condition;
            RE::FormID actorId;
        };
        std::vector<Work> work;
        {
            std::lock_guard lock(mutex_);
            for (const auto& [id, entry] : entries_) {
                if (select(id, entry)) work.push_back({id, entry.condition, entry.actorId});
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
        const auto evaluatedAt = Clock::now();
        {
            std::lock_guard lock(mutex_);
            for (const auto& [id, result] : results) {
                auto it = entries_.find(id);
                if (it == entries_.end()) continue;  // unwatched meanwhile
                auto& entry = it->second;
                entry.lastEvaluated = evaluatedAt;
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

    void Watcher::Install(std::chrono::milliseconds detectInterval) {
        {
            std::lock_guard lock(mutex_);
            if (installed_) return;
            installed_ = true;
        }
        if (detectInterval.count() <= 0) detectInterval = std::chrono::milliseconds(1000);

        auto* sink = EventSink::GetSingleton();
        if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
            holder->AddEventSink<RE::TESActorLocationChangeEvent>(sink);
            holder->AddEventSink<RE::TESCombatEvent>(sink);
            holder->AddEventSink<RE::TESCellFullyLoadedEvent>(sink);
            holder->AddEventSink<RE::TESEquipEvent>(sink);
            holder->AddEventSink<RE::TESContainerChangedEvent>(sink);
            holder->AddEventSink<RE::TESMagicEffectApplyEvent>(sink);
            holder->AddEventSink<RE::TESQuestStageEvent>(sink);
            holder->AddEventSink<RE::TESQuestStartStopEvent>(sink);
            holder->AddEventSink<RE::TESSleepStopEvent>(sink);
            holder->AddEventSink<RE::TESWaitStopEvent>(sink);
            holder->AddEventSink<RE::TESLoadGameEvent>(sink);
        } else {
            SKSE::log::error("ConditionEngine: ScriptEventSourceHolder unavailable - watches rely on polling only");
        }
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(sink);
        }

        // Detection ticks on a background timer; evaluation happens on the main thread
        std::thread([detectInterval]() {
            for (;;) {
                std::this_thread::sleep_for(detectInterval);
                if (Watcher::Get().GetWatchCount() == 0) continue;
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([]() { Watcher::Get().Tick(); });
                }
            }
        }).detach();

        SKSE::log::info("ConditionEngine: watcher installed (detect every {} ms, full poll every {} ms)",
                        detectInterval.count(), GetPollInterval().count());
    }

} // namespace ConditionEngine
