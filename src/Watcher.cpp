#include "ConditionEngine/Watcher.h"
#include "ConditionEngine/Availability.h"
#include "ConditionEngine/Conditions/ActorConditions.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Stats.h"
#include "ConditionEngine/Values/PushedValues.h"
#include "ConditionEngine/Values/RefreshOn.h"

#include <cmath>
#include <thread>

namespace ConditionEngine {

    namespace {
        constexpr auto kAllDependencies = static_cast<EventDependency>((1u << kEventDependencyBitCount) - 1);
        constexpr RE::FormID kAnyActor = static_cast<RE::FormID>(-1);
        constexpr auto kFrame = std::chrono::milliseconds(16);

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
        entry.extraDependencies = options.extraDependencies;
        entry.keys = CollectValueKeys(condition->ToJsonComplete());
        entry.condition = std::move(condition);
        entry.actorId = ActorKey(actor);
        entry.callback = std::move(callback);
        entry.pollInterval = options.pollInterval;
        entry.lastEvaluated = Clock::now();

        // Seed the baseline so the first callback reflects a real change
        if (IsWorldReady()) {
            if (auto* target = ResolveActor(entry.actorId)) {
                EvaluationContext context(target);
                entry.lastResult = entry.condition->EvaluateAvailable(context);
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
        {
            std::lock_guard lock(mutex_);
            if (entries_.erase(id) > 0) return;
        }
        {
            std::unique_lock lock(listMutex_);
            if (lists_.erase(id) > 0) return;
        }
        std::unique_lock lock(valueMutex_);
        auto it = valueEntries_.find(id);
        if (it == valueEntries_.end()) return;
        auto sampler = it->second.sampler;
        valueEntries_.erase(it);
        if (--sampler->users == 0) {
            // An orphaned sampler's key may already belong to a newer sampler
            if (auto at = samplers_.find(sampler->key); at != samplers_.end() && at->second == sampler) samplers_.erase(at);
        } else {
            sampler->interval = WatchOptions::kNever;
            for (const auto& [otherId, entry] : valueEntries_) {
                if (entry.sampler == sampler) sampler->interval = std::min(sampler->interval, entry.pollInterval);
            }
        }
        lock.unlock();
        shortestValueIntervalMs_ = ShortestValueInterval().count();
    }

    std::size_t Watcher::GetWatchCount() const {
        std::size_t count;
        {
            std::lock_guard lock(mutex_);
            count = entries_.size();
        }
        {
            std::shared_lock lock(listMutex_);
            count += lists_.size();
        }
        return count + GetValueWatchCount();
    }

    bool Watcher::IsWatchingActor(RE::Actor* actor) const {
        const RE::FormID key = ActorKey(actor);
        {
            std::lock_guard lock(mutex_);
            if (std::any_of(entries_.begin(), entries_.end(), [key](const auto& kv) { return kv.second.actorId == key; })) {
                return true;
            }
        }
        {
            std::shared_lock lock(listMutex_);
            if (std::any_of(lists_.begin(), lists_.end(), [key](const auto& kv) { return kv.second->actorId == key; })) {
                return true;
            }
        }
        std::shared_lock lock(valueMutex_);
        return std::any_of(samplers_.begin(), samplers_.end(),
                           [key](const auto& kv) { return kv.second->actorId == key; });
    }

    std::optional<bool> Watcher::Refresh(WatchId id) {
        if (!IsWorldReady()) return std::nullopt;
        Evaluate([id](WatchId candidate, const Entry&) { return candidate == id; });
        std::lock_guard lock(mutex_);
        auto it = entries_.find(id);
        return it != entries_.end() ? it->second.lastResult : std::nullopt;
    }

    void Watcher::RefreshDependencies() {
        const auto revision = Availability::Get().Revision();
        if (dependenciesRevision_.exchange(revision) == revision) return;
        {
            std::lock_guard lock(mutex_);
            for (auto& [id, entry] : entries_) {
                entry.dependencies = entry.condition->GetEffectiveEventDependencies() | entry.extraDependencies;
                entry.keys = CollectValueKeys(entry.condition->ToJsonComplete());
            }
        }
        {
            std::shared_lock lock(listMutex_);
            for (auto& [id, list] : lists_) list->list->RefreshDependencies();
        }
        std::shared_lock lock(valueMutex_);
        for (auto& [key, sampler] : samplers_) {
            sampler->dependencies = sampler->value->GetEventDependencies() | EventDependency::Value |
                                    EventDependency::Availability | sampler->extraDependencies;
            sampler->keys = CollectValueKeys(sampler->value->ToJson());
        }
    }

    void Watcher::NotifyEvent(EventDependency events, RE::Actor* actor) { NotifyEvent(events, actor, nullptr); }

    void Watcher::NotifyEvent(EventDependency events, RE::Actor* actor, const std::set<std::string>* keys) {
        if (!IsWorldReady()) return;
        RefreshDependencies();
        const RE::FormID only = actor ? ActorKey(actor) : kAnyActor;
        Evaluate([events, only, keys](WatchId, const Entry& entry) {
            return (only == kAnyActor || entry.actorId == only) && ChangeTouches(entry.dependencies, entry.keys, events, keys);
        });
        EvaluateLists(
            [events, only, keys](const ListEntry& entry) -> std::pair<int, EventDependency> {
                if (only != kAnyActor && entry.actorId != only) return {0, events};
                const auto plain = keys ? static_cast<EventDependency>(static_cast<std::uint32_t>(events) &
                                                                       ~static_cast<std::uint32_t>(EventDependency::Value))
                                        : events;
                const bool touched = HasDependency(entry.list->ListenedDependencies(), plain) ||
                                     (keys && HasDependency(events, EventDependency::Value) && entry.list->ListensToKeys(*keys));
                return {touched ? 1 : 0, events};
            },
            keys);
        SampleValues([events, only, keys](const Sampler& sampler) {
            // Global values answer for every actor
            return (only == kAnyActor || sampler.actorId == only || !sampler.value->ReadsActor()) &&
                   ChangeTouches(sampler.dependencies, sampler.keys, events, keys);
        });
    }

    void Watcher::PostEvent(EventDependency events, RE::FormID actorFormId) {
        if (GetWatchCount() == 0) return;
        const RE::FormID key = actorFormId == kAllActors ? kAnyActor : (actorFormId == 0x14 ? 0 : actorFormId);
        bool schedule;
        {
            std::lock_guard lock(postMutex_);
            posted_[key].events |= events;
            Stats::CountPostedEvent();
            schedule = !postQueued_;
            postQueued_ = true;
        }
        if (!schedule) return;
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([]() { Watcher::Get().DrainPosted(); });
        } else {
            std::lock_guard lock(postMutex_);
            posted_.clear();
            postQueued_ = false;
        }
    }

    void Watcher::PostValueChange(std::string valueKey, RE::FormID actorFormId) {
        if (GetWatchCount() == 0) return;
        const RE::FormID key = actorFormId == kAllActors ? kAnyActor : (actorFormId == 0x14 ? 0 : actorFormId);
        bool schedule;
        {
            std::lock_guard lock(postMutex_);
            posted_[key].valueKeys.insert(std::move(valueKey));
            Stats::CountPostedEvent();
            schedule = !postQueued_;
            postQueued_ = true;
        }
        if (!schedule) return;
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([]() { Watcher::Get().DrainPosted(); });
        } else {
            std::lock_guard lock(postMutex_);
            posted_.clear();
            postQueued_ = false;
        }
    }

    void Watcher::DrainPosted() {
        std::unordered_map<RE::FormID, Posted> posted;
        {
            std::lock_guard lock(postMutex_);
            posted.swap(posted_);
            postQueued_ = false;
        }
        Stats::CountDrain();
        auto deliver = [this](const Posted& p, RE::Actor* actor) {
            if (p.events != EventDependency::None) NotifyEvent(p.events, actor, nullptr);
            if (!p.valueKeys.empty()) NotifyEvent(EventDependency::Value, actor, &p.valueKeys);
        };
        if (auto it = posted.find(kAnyActor); it != posted.end()) {
            deliver(it->second, nullptr);
            posted.erase(it);
        }
        for (const auto& [key, p] : posted) {
            if (auto* actor = ResolveActor(key)) deliver(p, actor);
        }
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
        tickQueued_ = false;
        if (!IsWorldReady()) return;

        Stats::CountConsoleCheck(RE::ConsoleLog::IsConsoleMode());
        RefreshDependencies();
        const auto tickStart = Clock::now();
        struct RecordOnExit {
            Clock::time_point start;
            ~RecordOnExit() { Stats::RecordTick(Clock::now() - start); }
        } record{tickStart};
        PushedValues::Get().ExpireDue();

        // Value watches due for a sample (a little early beats a whole tick late)
        SampleValues([tickStart](const Sampler& sampler) {
            if (sampler.interval == WatchOptions::kNever) return false;
            const auto slack = std::min<std::chrono::milliseconds>(sampler.interval / 4, kFrame);
            return tickStart - sampler.lastSampled >= sampler.interval - slack;
        });

        if (tickStart - lastDetect_ < detectInterval_ - kFrame) return;
        lastDetect_ = tickStart;

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
        {
            std::shared_lock lock(valueMutex_);
            for (const auto& [key, sampler] : samplers_) watched.insert(sampler->actorId);
        }
        {
            std::shared_lock lock(listMutex_);
            for (const auto& [id, list] : lists_) watched.insert(list->actorId);
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
        // Lists: a due poll re-evaluates fully (polled rules may have changed); otherwise the
        // actor's detected changes re-evaluate what they touch
        EvaluateLists(
            [&](const ListEntry& entry) -> std::pair<int, EventDependency> {
                const auto interval = entry.pollInterval.count() == 0 ? defaultInterval : entry.pollInterval;
                if (!entry.evaluated || (interval != WatchOptions::kNever && now - entry.lastPolled >= interval)) {
                    return {2, EventDependency::None};
                }
                auto it = actorEvents.find(entry.actorId);
                const auto changed = it != actorEvents.end() ? it->second : worldEvents;
                return {HasDependency(entry.list->ListenedDependencies(), changed) ? 1 : 0, changed};
            },
            nullptr);
        // Event-driven value samples (world changes reach global values too)
        SampleValues([&](const Sampler& sampler) {
            if (sampler.lastSampled >= tickStart) return false;  // sampled this tick already
            if (HasDependency(sampler.dependencies, worldEvents)) return true;
            auto it = actorEvents.find(sampler.actorId);
            return it != actorEvents.end() && HasDependency(sampler.dependencies, it->second);
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
                if (!entry.orphaned && select(id, entry)) work.push_back({id, entry.condition, entry.actorId});
            }
        }
        if (work.empty()) return;
        Stats::AddConditionEvaluations(work.size());

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
            results.emplace_back(item.id, item.condition->EvaluateAvailable(*context));
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

    // ── Value watches ────────────────────────────────────────────────────────

    std::uint32_t ValueSample::AgeMs() const {
        if (!sampledAt) return 0xFFFFFFFFu;
        const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - *sampledAt).count();
        if (age <= 0) return 0;
        return age >= 0xFFFFFFFELL ? 0xFFFFFFFEu : static_cast<std::uint32_t>(age);
    }

    std::size_t Watcher::GetValueWatchCount() const {
        std::shared_lock lock(valueMutex_);
        return valueEntries_.size();
    }

    std::chrono::milliseconds Watcher::ShortestValueInterval() const {
        std::shared_lock lock(valueMutex_);
        auto shortest = std::chrono::milliseconds(0);
        for (const auto& [key, sampler] : samplers_) {
            if (sampler->interval == WatchOptions::kNever) continue;
            if (shortest.count() == 0 || sampler->interval < shortest) shortest = sampler->interval;
        }
        return shortest;
    }

    WatchId Watcher::WatchValue(std::shared_ptr<const Value> value, RE::Actor* actor, ValueWatchCallback callback,
                                const ValueWatchOptions& options) {
        if (!value) return 0;

        // Values that read no actor share one sample across actors
        const RE::FormID actorId = value->ReadsActor() ? ActorKey(actor) : 0;
        const std::string key = value->ToJson().dump() + "@" + std::to_string(actorId);
        auto interval = options.pollInterval.count() == 0 ? GetPollInterval() : options.pollInterval;
        if (interval != WatchOptions::kNever) interval = std::max<std::chrono::milliseconds>(interval, kFrame);

        ValueEntry entry;
        entry.callback = std::move(callback);
        entry.options = options;
        entry.pollInterval = interval;
        entry.above.assign(options.thresholds.size(), false);

        std::shared_ptr<Sampler> sampler;
        bool fresh = false;
        WatchId id;
        {
            std::unique_lock lock(valueMutex_);
            auto& slot = samplers_[key];
            if (!slot) {
                slot = std::make_shared<Sampler>();
                slot->value = std::move(value);
                slot->key = key;
                slot->actorId = actorId;
                slot->dependencies = slot->value->GetEventDependencies() | EventDependency::Value |
                                     EventDependency::Availability;
                slot->keys = CollectValueKeys(slot->value->ToJson());
                fresh = true;
            }
            sampler = slot;
            sampler->dependencies |= options.extraDependencies;
            sampler->extraDependencies |= options.extraDependencies;
            sampler->interval = std::min(sampler->interval, interval);
            ++sampler->users;
            entry.sampler = sampler;
            {
                std::lock_guard idLock(mutex_);  // condition and value watches share ids
                id = nextId_++;
                if (nextId_ == 0) nextId_ = 1;
            }
            valueEntries_.emplace(id, std::move(entry));
        }
        shortestValueIntervalMs_ = ShortestValueInterval().count();

        // First sample now when possible; an existing sampler's sample seeds the new watch
        if (IsWorldReady()) {
            if (fresh) SampleValues([&](const Sampler& s) { return &s == sampler.get(); });
            else SampleValues([](const Sampler&) { return false; });
        }
        return id;
    }

    bool Watcher::ReadValueWatch(WatchId id, ValueSample& out) const {
        std::shared_ptr<Sampler> sampler;
        {
            std::shared_lock lock(valueMutex_);
            auto it = valueEntries_.find(id);
            if (it == valueEntries_.end()) return false;
            sampler = it->second.sampler;
        }
        std::lock_guard lock(sampler->sampleMutex);
        out = sampler->sample;
        return true;
    }

    std::optional<ValueSample> Watcher::RefreshValue(WatchId id) {
        if (!IsWorldReady()) return std::nullopt;
        std::shared_ptr<Sampler> sampler;
        {
            std::shared_lock lock(valueMutex_);
            auto it = valueEntries_.find(id);
            if (it == valueEntries_.end()) return std::nullopt;
            sampler = it->second.sampler;
        }
        SampleValues([&](const Sampler& s) { return &s == sampler.get(); });
        std::lock_guard lock(sampler->sampleMutex);
        return sampler->sample;
    }

    void Watcher::SampleValues(const std::function<bool(const Sampler&)>& select) {
        std::vector<std::shared_ptr<Sampler>> due;
        {
            std::shared_lock lock(valueMutex_);
            for (const auto& [key, sampler] : samplers_) {
                if (select(*sampler)) due.push_back(sampler);
            }
        }

        const auto now = Clock::now();
        const auto revision = Availability::Get().Revision();
        Stats::AddValueSamples(due.size());
        // One context per actor per pass; contexts snapshot lazily, so this is cheap
        std::unordered_map<RE::FormID, std::unique_ptr<EvaluationContext>> contexts;
        for (const auto& sampler : due) {
            if (sampler->checkedRevision != revision) {
                const auto status = sampler->value->CheckAvailability();
                sampler->availability = status.state;
                sampler->availabilityReason = status.reason;
                sampler->checkedRevision = revision;
            }

            ValueResult result;
            bool keepValue = false;  // engine-side failure: keep the last number for smoothing
            RE::Actor* target = ResolveActor(sampler->actorId);
            if (sampler->availability != AvailabilityState::Available) {
                result = ValueResult::Fail(sampler->availability == AvailabilityState::Pending ? ValueStatus::Pending
                                                                                               : ValueStatus::Unavailable,
                                           sampler->availabilityReason);
                keepValue = true;
            } else if (!target || !target->Is3DLoaded()) {
                result = ValueResult::Fail(ValueStatus::Unavailable, "actor not loaded");
                keepValue = true;
            } else {
                auto& context = contexts[sampler->actorId];
                if (!context) context = std::make_unique<EvaluationContext>(target);
                result = sampler->value->ReadCounted(*context);
            }

            sampler->lastSampled = now;
            std::lock_guard lock(sampler->sampleMutex);
            auto& sample = sampler->sample;
            if (!keepValue || !sample.sampledAt) sample.value = result.value;
            sample.status = result.status;
            sample.reason = std::move(result.reason);
            sample.from = result.FromText();
            sample.sampledAt = result.sampledAt.value_or(now);
            sample.availabilityRevision = revision;
        }

        // Fire callbacks for every watch whose sampler has a sample it has not seen
        struct Fire {
            ValueWatchCallback callback;
            WatchId id;
            RE::FormID actorId;
            ValueSample sample;
            int crossed;
        };
        std::vector<Fire> toFire;
        {
            std::unique_lock lock(valueMutex_);
            for (auto& [id, entry] : valueEntries_) {
                const auto& sampler = entry.sampler;
                const bool wasDue = std::any_of(due.begin(), due.end(), [&](const auto& d) { return d == sampler; });
                if (!wasDue && entry.delivered) continue;

                ValueSample sample;
                {
                    std::lock_guard sampleLock(sampler->sampleMutex);
                    sample = sampler->sample;
                }
                if (!sample.sampledAt) continue;  // never sampled yet

                const double v = sample.value;
                const auto& thresholds = entry.options.thresholds;
                if (!entry.delivered) {
                    entry.delivered = true;
                    entry.lastStatus = sample.status;
                    entry.lastFiredValue = v;
                    for (std::size_t i = 0; i < thresholds.size(); ++i) entry.above[i] = v >= thresholds[i];
                    if (entry.callback && entry.options.fireImmediately) {
                        toFire.push_back({entry.callback, id, sampler->actorId, sample, -1});
                    }
                    continue;
                }

                bool general = sample.status != entry.lastStatus;
                entry.lastStatus = sample.status;
                std::vector<int> crossings;
                if (sample.status == ValueStatus::Ok) {
                    for (std::size_t i = 0; i < thresholds.size(); ++i) {
                        if (!entry.above[i] && v >= thresholds[i]) {
                            entry.above[i] = true;
                            crossings.push_back(static_cast<int>(i));
                        } else if (entry.above[i] && v < thresholds[i] - entry.options.hysteresis) {
                            entry.above[i] = false;
                            crossings.push_back(static_cast<int>(i));
                        }
                    }
                    const auto& last = entry.lastFiredValue;
                    if (entry.options.minStep > 0.0) {
                        general = general || !last || std::abs(v - *last) >= entry.options.minStep;
                    } else if (thresholds.empty()) {
                        general = general || !last || v != *last;
                    }
                }
                if (general || !crossings.empty()) entry.lastFiredValue = v;
                if (!entry.callback) continue;
                for (int index : crossings) toFire.push_back({entry.callback, id, sampler->actorId, sample, index});
                if (general && crossings.empty()) toFire.push_back({entry.callback, id, sampler->actorId, sample, -1});
            }
        }

        for (const auto& fire : toFire) {
            try {
                fire.callback(fire.id, ResolveActor(fire.actorId), fire.sample, fire.crossed);
            } catch (const std::exception& e) {
                SKSE::log::error("ConditionEngine: value watch {} callback threw: {}", fire.id, e.what());
            }
        }
    }

    // ── List watches ─────────────────────────────────────────────────────────

    namespace {
        const char* TriName(TriState state) {
            switch (state) {
                case TriState::True: return "true";
                case TriState::Pending: return "pending";
                case TriState::Unavailable: return "unavailable";
                default: return "false";
            }
        }
    }

    WatchId Watcher::WatchList(std::vector<std::shared_ptr<const Condition>> rules, RE::Actor* actor,
                               ListWatchCallback callback, const WatchOptions& options) {
        if (rules.empty()) return 0;
        auto entry = std::make_shared<ListEntry>();
        entry->list = std::make_unique<FirstMatchList>(std::move(rules));
        entry->actorId = ActorKey(actor);
        entry->callback = std::move(callback);
        entry->extraDependencies = options.extraDependencies;
        entry->pollInterval = options.pollInterval;
        entry->stateJson = {{"winner", -1}, {"rules", nlohmann::json::array()}};

        WatchId id;
        {
            std::lock_guard idLock(mutex_);  // all watch kinds share ids
            id = nextId_++;
            if (nextId_ == 0) nextId_ = 1;
        }
        {
            std::unique_lock lock(listMutex_);
            lists_.emplace(id, entry);
        }
        if (IsWorldReady()) {
            const bool fire = options.fireImmediately;
            EvaluateLists([&](const ListEntry& e) { return std::pair{&e == entry.get() ? 2 : 0, EventDependency::None}; },
                          nullptr);
            if (fire && entry->callback) entry->callback(id, ResolveActor(entry->actorId), entry->winner.load());
        }
        return id;
    }

    int Watcher::ReadListWatch(WatchId id) const {
        std::shared_lock lock(listMutex_);
        auto it = lists_.find(id);
        return it == lists_.end() ? -2 : it->second->winner.load(std::memory_order_acquire);
    }

    int Watcher::RefreshListWatch(WatchId id) {
        if (!IsWorldReady()) return -2;
        std::shared_ptr<ListEntry> entry;
        {
            std::shared_lock lock(listMutex_);
            auto it = lists_.find(id);
            if (it == lists_.end()) return -2;
            entry = it->second;
        }
        EvaluateLists([&](const ListEntry& e) { return std::pair{&e == entry.get() ? 2 : 0, EventDependency::None}; },
                      nullptr);
        return entry->winner.load();
    }

    nlohmann::json Watcher::ReadListWatchState(WatchId id) const {
        std::shared_ptr<ListEntry> entry;
        {
            std::shared_lock lock(listMutex_);
            auto it = lists_.find(id);
            if (it == lists_.end()) return nullptr;
            entry = it->second;
        }
        std::lock_guard lock(entry->stateMutex);
        return entry->stateJson;
    }

    void Watcher::EvaluateLists(const std::function<std::pair<int, EventDependency>(const ListEntry&)>& select,
                                const std::set<std::string>* keys) {
        struct Work {
            std::shared_ptr<ListEntry> entry;
            int mode;
            EventDependency changed;
        };
        std::vector<Work> work;
        {
            std::shared_lock lock(listMutex_);
            for (const auto& [id, entry] : lists_) {
                if (entry->orphaned) continue;
                if (const auto [mode, changed] = select(*entry); mode != 0) work.push_back({entry, mode, changed});
            }
        }
        if (work.empty()) return;

        struct Fire {
            ListWatchCallback callback;
            WatchId id;
            RE::FormID actorId;
            int winner;
        };
        std::vector<Fire> toFire;
        std::unordered_map<RE::FormID, std::unique_ptr<EvaluationContext>> contexts;
        const auto now = Clock::now();
        for (auto& [entry, mode, changed] : work) {
            auto* target = ResolveActor(entry->actorId);
            if (!target || !target->Is3DLoaded()) continue;
            auto& context = contexts[entry->actorId];
            if (!context) context = std::make_unique<EvaluationContext>(target);

            const int before = entry->evaluated ? entry->winner.load() : -3;
            const int after = mode == 2 ? entry->list->EvaluateAll(*context)
                                        : entry->list->EvaluateChanged(*context, changed | entry->extraDependencies, keys);
            if (mode == 2) entry->lastPolled = now;
            entry->evaluated = true;
            entry->winner.store(after, std::memory_order_release);

            nlohmann::json rules = nlohmann::json::array();
            const auto& states = entry->list->States();
            const auto& seen = entry->list->Evaluated();
            for (std::size_t i = 0; i < states.size(); ++i) {
                nlohmann::json rule{{"state", seen[i] ? TriName(states[i].state) : "unread"}};
                if (!states[i].reason.empty()) rule["reason"] = states[i].reason;
                rules.push_back(std::move(rule));
            }
            {
                std::lock_guard lock(entry->stateMutex);
                entry->stateJson = {{"winner", after}, {"rules", std::move(rules)}};
            }
            if (before != -3 && before != after && entry->callback) {
                WatchId id = 0;
                {
                    std::shared_lock lock(listMutex_);
                    for (const auto& [candidate, e] : lists_) {
                        if (e == entry) id = candidate;
                    }
                }
                toFire.push_back({entry->callback, id, entry->actorId, after});
            }
        }
        for (const auto& fire : toFire) {
            try {
                fire.callback(fire.id, ResolveActor(fire.actorId), fire.winner);
            } catch (const std::exception& e) {
                SKSE::log::error("ConditionEngine: list watch {} callback threw: {}", fire.id, e.what());
            }
        }
    }

    void Watcher::OnGameLoad() {
        auto isCreated = [](RE::FormID id) { return (id & 0xFF000000u) == 0xFF000000u; };
        {
            std::lock_guard lock(mutex_);
            for (auto& [id, entry] : entries_) {
                if (isCreated(entry.actorId)) entry.orphaned = true;
            }
        }
        {
            std::shared_lock lock(listMutex_);
            for (auto& [id, entry] : lists_) {
                if (isCreated(entry->actorId)) {
                    entry->orphaned = true;
                    entry->winner.store(-1);
                }
            }
        }
        std::vector<std::shared_ptr<Sampler>> orphaned;
        {
            std::unique_lock lock(valueMutex_);
            for (auto it = samplers_.begin(); it != samplers_.end();) {
                if (isCreated(it->second->actorId)) {
                    orphaned.push_back(it->second);  // watches keep it; nothing samples it again
                    it = samplers_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (const auto& sampler : orphaned) {
            std::lock_guard lock(sampler->sampleMutex);
            sampler->sample.status = ValueStatus::Unavailable;
            sampler->sample.reason = "actor from a previous game (created reference); watch it again";
        }
        shortestValueIntervalMs_ = ShortestValueInterval().count();
    }

    void Watcher::Install(std::chrono::milliseconds detectInterval) {
        static const auto availabilitySubscription = Availability::Get().Subscribe([](std::uint64_t) {
            if (auto* tasks = SKSE::GetTaskInterface()) tasks->AddTask([]() {
                Watcher::Get().NotifyEvent(EventDependency::Availability, nullptr);
            });
        });
        (void)availabilitySubscription;
        {
            std::lock_guard lock(mutex_);
            if (installed_) return;
            installed_ = true;
        }
        if (detectInterval.count() <= 0) detectInterval = std::chrono::milliseconds(1000);
        detectInterval_ = detectInterval;

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
        RefreshOn::Install();

        // Ticks on a background timer (every detect interval, or faster while value
        // watches need it, down to a frame); evaluation happens on the main thread
        std::thread([detectInterval]() {
            for (;;) {
                auto& watcher = Watcher::Get();
                auto step = detectInterval;
                if (const auto shortest = watcher.shortestValueIntervalMs_.load(); shortest > 0) {
                    step = std::min(step, std::max<std::chrono::milliseconds>(std::chrono::milliseconds(shortest), kFrame));
                }
                std::this_thread::sleep_for(step);
                if (watcher.GetWatchCount() == 0) {
                    // Idle: only sample the main thread's console mode for "ce stats"
                    if (!watcher.tickQueued_.exchange(true)) {
                        if (auto* tasks = SKSE::GetTaskInterface()) {
                            tasks->AddTask([]() {
                                Stats::CountConsoleCheck(RE::ConsoleLog::IsConsoleMode());
                                Watcher::Get().tickQueued_ = false;
                            });
                        } else {
                            watcher.tickQueued_ = false;
                        }
                    }
                    continue;
                }
                // One queued tick at a time, so a stalled main thread does not pile them up
                if (watcher.tickQueued_.exchange(true)) continue;
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([]() { Watcher::Get().Tick(); });
                } else {
                    watcher.tickQueued_ = false;
                }
            }
        }).detach();

        SKSE::log::info("ConditionEngine: watcher installed (detect every {} ms, full poll every {} ms)",
                        detectInterval.count(), GetPollInterval().count());
    }

} // namespace ConditionEngine
