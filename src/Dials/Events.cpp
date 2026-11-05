#include "Events.h"
#include "Service.h"

#include <atomic>

// Declared but not defined by CommonLib
namespace RE {
    struct TESSleepStartEvent {
        float startTime;
        float endTime;
    };
}

namespace ConditionEngine::Dials::Events {

    namespace {
        std::atomic<RE::FormID> g_crosshair{0};
        std::atomic<double> g_sleepStartHours{-1.0};

        ActorKey Key(const RE::TESObjectREFR* ref) { return KeyOf(ref); }

        double GameHours() {
            auto* calendar = RE::Calendar::GetSingleton();
            return calendar ? static_cast<double>(calendar->GetCurrentGameTime()) * 24.0 : 0.0;
        }

        // Events can arrive mid-update or off the main thread: rules run on the next task
        void Post(GameEvent event, EventInfo info) {
            auto& service = Service::Get();
            if (!service.InSession() || !service.Uses(event)) return;
            info.crosshair = g_crosshair.load();
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([event, info = std::move(info)]() {
                    auto& s = Service::Get();
                    if (!s.InSession()) return;
                    s.GetEngine().OnGameEvent(event, info);
                    s.Publish();
                });
            }
        }

        class Sink final : public RE::BSTEventSink<RE::TESHitEvent>,
                           public RE::BSTEventSink<RE::TESCombatEvent>,
                           public RE::BSTEventSink<RE::TESDeathEvent>,
                           public RE::BSTEventSink<RE::TESEquipEvent>,
                           public RE::BSTEventSink<RE::TESSpellCastEvent>,
                           public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>,
                           public RE::BSTEventSink<RE::TESActivateEvent>,
                           public RE::BSTEventSink<RE::TESContainerChangedEvent>,
                           public RE::BSTEventSink<RE::TESActorLocationChangeEvent>,
                           public RE::BSTEventSink<RE::TESSleepStartEvent>,
                           public RE::BSTEventSink<RE::TESSleepStopEvent>,
                           public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
                           public RE::BSTEventSink<RE::BSAnimationGraphEvent>,
                           public RE::BSTEventSink<RE::SkillIncrease::Event>,
                           public RE::BSTEventSink<SKSE::ModCallbackEvent>,
                           public RE::BSTEventSink<SKSE::CrosshairRefEvent>,
                           public RE::BSTEventSink<RE::InputEvent*> {
        public:
            using Control = RE::BSEventNotifyControl;

            static Sink* Get() {
                static Sink sink;
                return &sink;
            }

            Control ProcessEvent(const RE::TESHitEvent* e, RE::BSTEventSource<RE::TESHitEvent>*) override {
                if (e && e->target) {
                    EventInfo info;
                    info.actor = Key(e->target.get());
                    info.other = Key(e->cause.get());
                    info.form = e->source;
                    if (info.actor) Post(GameEvent::Hit, info);
                }
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESCombatEvent* e, RE::BSTEventSource<RE::TESCombatEvent>*) override {
                if (e && e->actor) {
                    EventInfo info;
                    info.actor = Key(e->actor.get());
                    info.other = Key(e->targetActor.get());
                    const auto state = e->newState.get();
                    if (info.actor && state == RE::ACTOR_COMBAT_STATE::kCombat) Post(GameEvent::CombatStart, info);
                    if (info.actor && state == RE::ACTOR_COMBAT_STATE::kNone) Post(GameEvent::CombatEnd, info);
                }
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESDeathEvent* e, RE::BSTEventSource<RE::TESDeathEvent>*) override {
                if (e && e->dead && e->actorDying) {
                    EventInfo death;
                    death.actor = Key(e->actorDying.get());
                    death.other = Key(e->actorKiller.get());
                    if (death.actor) Post(GameEvent::Death, death);
                    if (death.other) {
                        EventInfo kill;
                        kill.actor = death.other;
                        kill.other = death.actor;
                        Post(GameEvent::Kill, kill);
                    }
                }
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESEquipEvent* e, RE::BSTEventSource<RE::TESEquipEvent>*) override {
                if (e && e->actor) {
                    EventInfo info;
                    info.actor = Key(e->actor.get());
                    info.form = e->baseObject;
                    if (info.actor) Post(e->equipped ? GameEvent::Equip : GameEvent::Unequip, info);
                }
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESSpellCastEvent* e, RE::BSTEventSource<RE::TESSpellCastEvent>*) override {
                if (e && e->object) {
                    EventInfo info;
                    info.actor = Key(e->object.get());
                    info.form = e->spell;
                    if (info.actor) Post(GameEvent::SpellCast, info);
                }
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESMagicEffectApplyEvent* e,
                                 RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override {
                if (e && e->target) {
                    EventInfo info;
                    info.actor = Key(e->target.get());
                    info.other = Key(e->caster.get());
                    info.form = e->magicEffect;
                    if (info.actor) Post(GameEvent::MagicEffect, info);
                }
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESActivateEvent* e, RE::BSTEventSource<RE::TESActivateEvent>*) override {
                if (e && e->actionRef && e->objectActivated) {
                    EventInfo info;
                    info.actor = Key(e->actionRef.get());
                    const auto* base = e->objectActivated->GetBaseObject();
                    info.form = base ? base->GetFormID() : e->objectActivated->GetFormID();
                    info.other = Key(e->objectActivated.get());
                    if (info.actor) Post(GameEvent::Activate, info);
                }
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESContainerChangedEvent* e,
                                 RE::BSTEventSource<RE::TESContainerChangedEvent>*) override {
                auto& service = Service::Get();
                if (!e || (!service.Uses(GameEvent::ItemAdded) && !service.Uses(GameEvent::ItemRemoved))) {
                    return Control::kContinue;
                }
                const auto actorAt = [](RE::FormID id) -> ActorKey {
                    auto* form = id ? RE::TESForm::LookupByID(id) : nullptr;
                    return KeyOf(form);
                };
                const ActorKey to = actorAt(e->newContainer);
                const ActorKey from = actorAt(e->oldContainer);
                const double count = std::abs(e->itemCount);
                if (to) Post(GameEvent::ItemAdded, {to, from, e->baseObj, {}, count, 0});
                if (from) Post(GameEvent::ItemRemoved, {from, to, e->baseObj, {}, count, 0});
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESActorLocationChangeEvent* e,
                                 RE::BSTEventSource<RE::TESActorLocationChangeEvent>*) override {
                if (e && e->actor) {
                    EventInfo info;
                    info.actor = Key(e->actor.get());
                    info.form = e->newLoc ? e->newLoc->GetFormID() : 0;
                    if (info.actor) Post(GameEvent::LocationChange, info);
                }
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESSleepStartEvent*, RE::BSTEventSource<RE::TESSleepStartEvent>*) override {
                g_sleepStartHours.store(GameHours());
                EventInfo info;
                info.actor = kPlayer;
                Post(GameEvent::SleepStart, info);
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::TESSleepStopEvent*, RE::BSTEventSource<RE::TESSleepStopEvent>*) override {
                EventInfo info;
                info.actor = kPlayer;
                const double start = g_sleepStartHours.exchange(-1.0);
                info.number = start >= 0 ? std::max(0.0, GameHours() - start) : 0.0;
                Post(GameEvent::SleepStop, info);
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::MenuOpenCloseEvent* e, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
                if (e) {
                    EventInfo info;
                    info.actor = kPlayer;
                    info.text = e->menuName.c_str();
                    Post(e->opening ? GameEvent::MenuOpen : GameEvent::MenuClose, info);
                }
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::BSAnimationGraphEvent* e,
                                 RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override {
                if (e && Service::Get().Uses(GameEvent::Animation)) {
                    EventInfo info;
                    info.actor = kPlayer;
                    info.text = e->tag.c_str();
                    Post(GameEvent::Animation, info);
                }
                return Control::kContinue;
            }

            Control ProcessEvent(const RE::SkillIncrease::Event* e,
                                 RE::BSTEventSource<RE::SkillIncrease::Event>*) override {
                if (e) {
                    EventInfo info;
                    info.actor = kPlayer;
                    info.text = std::string(RE::ActorValueToString(e->actorValue));
                    Post(GameEvent::SkillUp, info);
                }
                return Control::kContinue;
            }

            Control ProcessEvent(const SKSE::ModCallbackEvent* e, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override {
                auto& service = Service::Get();
                if (!e || !service.InSession()) return Control::kContinue;
                EventInfo info;
                info.actor = KeyOf(e->sender);
                info.text = e->strArg.c_str();
                info.number = e->numArg;
                info.crosshair = g_crosshair.load();
                std::string name = e->eventName.c_str();
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([name = std::move(name), info = std::move(info)]() {
                        auto& s = Service::Get();
                        if (!s.InSession()) return;
                        s.GetEngine().OnModEvent(name, info);
                        s.Publish();
                    });
                }
                return Control::kContinue;
            }

            Control ProcessEvent(const SKSE::CrosshairRefEvent* e, RE::BSTEventSource<SKSE::CrosshairRefEvent>*) override {
                g_crosshair.store(e && e->crosshairRef ? KeyOf(e->crosshairRef.get()) : 0);
                return Control::kContinue;
            }

            Control ProcessEvent(RE::InputEvent* const* events, RE::BSTEventSource<RE::InputEvent*>*) override {
                if (!events || !Service::Get().InSession()) return Control::kContinue;
                auto* ui = RE::UI::GetSingleton();
                if (ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME))) return Control::kContinue;
                for (auto* e = *events; e; e = e->next) {
                    if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) continue;
                    const auto* button = e->AsButtonEvent();
                    if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard || !button->IsDown()) continue;
                    const auto code = button->GetIDCode();
                    if (!Service::Get().UsesKey(code)) continue;
                    // Input arrives on the main thread, between frames: run now
                    auto& s = Service::Get();
                    s.GetEngine().OnKey(code, g_crosshair.load());
                    s.Publish();
                }
                return Control::kContinue;
            }
        };
    }

    void Install() {
        auto* sink = Sink::Get();
        if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
            holder->AddEventSink<RE::TESHitEvent>(sink);
            holder->AddEventSink<RE::TESCombatEvent>(sink);
            holder->AddEventSink<RE::TESDeathEvent>(sink);
            holder->AddEventSink<RE::TESEquipEvent>(sink);
            holder->AddEventSink<RE::TESSpellCastEvent>(sink);
            holder->AddEventSink<RE::TESMagicEffectApplyEvent>(sink);
            holder->AddEventSink<RE::TESActivateEvent>(sink);
            holder->AddEventSink<RE::TESContainerChangedEvent>(sink);
            holder->AddEventSink<RE::TESActorLocationChangeEvent>(sink);
            holder->AddEventSink<RE::TESSleepStartEvent>(sink);
            holder->AddEventSink<RE::TESSleepStopEvent>(sink);
        }
        if (auto* ui = RE::UI::GetSingleton()) ui->AddEventSink<RE::MenuOpenCloseEvent>(sink);
        if (auto* skills = RE::SkillIncrease::GetEventSource()) skills->AddEventSink(sink);
        if (auto* mods = SKSE::GetModCallbackEventSource()) mods->AddEventSink(sink);
        if (auto* crosshair = SKSE::GetCrosshairRefEventSource()) crosshair->AddEventSink(sink);
        if (auto* input = RE::BSInputDeviceManager::GetSingleton()) input->AddEventSink(sink);
        SKSE::log::info("Dials: event sinks installed");
    }

    void OnSessionStart() {
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            auto* sink = static_cast<RE::BSTEventSink<RE::BSAnimationGraphEvent>*>(Sink::Get());
            player->RemoveAnimationGraphEventSink(sink);
            player->AddAnimationGraphEventSink(sink);
        }
    }

    RE::FormID Crosshair() { return g_crosshair.load(); }

} // namespace ConditionEngine::Dials::Events
