/// @file OarConditions.cpp
/// @brief Open Animation Replacer custom conditions that evaluate ConditionEngine conditions.

#include "OarConditions.h"

#include "OAR/OpenAnimationReplacerAPI-Conditions.h"

#include "ConditionEngine/Availability.h"
#include "ConditionEngine/Conditions/RuleReferenceCondition.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Expression.h"
#include "ConditionEngine/Watcher.h"

#include <atomic>
#include <chrono>
#include <shared_mutex>

namespace ConditionEngine::OarConditions {

    namespace {
        using Clock = std::chrono::steady_clock;

        enum class Kind : char { Named = 'N', Expression = 'E' };

        // A (text, actor) pair nobody evaluated for this long loses its watch
        constexpr std::int64_t kIdleDropMs = 60000;
        // Housekeeping (new watches, idle drops, status texts) at most this often while OAR reads
        constexpr std::int64_t kPassIntervalMs = 2000;

        std::atomic<std::uint32_t> g_mainThreadId{0};

        bool IsMainThread() {
            const auto main = g_mainThreadId.load(std::memory_order_relaxed);
            return main != 0 && REX::W32::GetCurrentThreadId() == main;
        }

        std::int64_t NowMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
        }

        // The latest result of one condition for one actor, published by a main-thread watch
        struct Sample {
            std::atomic<int> result{-1};  // -1 no result yet, 0 false, 1 true
            std::atomic<std::int64_t> lastReadMs{0};
            WatchId watch = 0;  // written on the main thread only
        };

        // One distinct condition text. `condition`, `builtRevision`, `ready`, `status` and
        // `samples` are guarded by g_mutex; only the main thread writes the first four.
        struct Gate {
            Kind kind = Kind::Named;
            std::string text;
            std::shared_ptr<const Condition> condition;  // built once, kept for the gate's life
            std::uint64_t builtRevision = ~0ull;         // availability revision of the last failed build
            bool ready = false;
            std::string status;  // why it reads false regardless of the actor
            std::atomic<std::int64_t> lastUsedMs{0};
            std::unordered_map<RE::FormID, std::shared_ptr<Sample>> samples;
        };

        // Held only around map lookups and field copies, never across evaluation or Watcher calls
        std::shared_mutex g_mutex;
        std::unordered_map<std::string, std::shared_ptr<Gate>> g_gates;

        std::atomic<bool> g_passQueued{false};
        std::atomic<std::int64_t> g_lastPassMs{0};

        RE::Actor* AsActor(RE::TESObjectREFR* refr) { return refr ? refr->As<RE::Actor>() : nullptr; }

        std::string Normalize(Kind kind, std::string_view text) {
            auto trim = [](std::string_view s) {
                const auto first = s.find_first_not_of(" \t\r\n");
                if (first == std::string_view::npos) return std::string_view{};
                const auto last = s.find_last_not_of(" \t\r\n");
                return s.substr(first, last - first + 1);
            };
            text = trim(text);
            if (kind == Kind::Named && !text.empty() && text.front() == '@') text = trim(text.substr(1));
            return std::string(text);
        }

        std::shared_ptr<Gate> FindOrAddGate(Kind kind, std::string text) {
            std::string key(1, static_cast<char>(kind));
            key += text;
            {
                std::shared_lock lock(g_mutex);
                if (auto it = g_gates.find(key); it != g_gates.end()) return it->second;
            }
            std::unique_lock lock(g_mutex);
            auto& gate = g_gates[key];
            if (!gate) {
                gate = std::make_shared<Gate>();
                gate->kind = kind;
                gate->text = std::move(text);
            }
            return gate;
        }

        // Main thread. The gate's condition, built on first use; a failed build is retried
        // when the availability revision moves (types or named conditions registered later).
        std::shared_ptr<const Condition> EnsureBuilt(Gate& gate) {
            std::uint64_t builtRevision;
            {
                std::shared_lock lock(g_mutex);
                if (gate.condition) return gate.condition;
                builtRevision = gate.builtRevision;
            }
            const auto revision = Availability::Get().Revision();
            if (builtRevision == revision) return nullptr;

            std::shared_ptr<const Condition> condition;
            std::string error;
            if (gate.text.empty()) {
                error = gate.kind == Kind::Named ? "no named condition set" : "no expression set";
            } else if (gate.kind == Kind::Named) {
                // Rule("name") resolves on every read, so redefined presets reach this gate
                condition = std::make_shared<RuleReferenceCondition>(gate.text);
            } else if (auto compiled = Expression::CompileAny(gate.text, &error)) {
                condition = std::shared_ptr<const Condition>(std::move(compiled));
            } else {
                error = "expression error: " + error;
            }

            std::unique_lock lock(g_mutex);
            if (gate.condition) return gate.condition;
            gate.builtRevision = revision;
            if (condition) {
                gate.condition = condition;
            } else {
                gate.ready = false;
                gate.status = std::move(error);
            }
            return condition;
        }

        std::string DescribeAvailability(const AvailabilityStatus& status) {
            return (status.state == AvailabilityState::Pending ? "pending: " : "unavailable: ") + status.reason;
        }

        // Main thread: builds the gate if needed and publishes why it reads false for everyone
        void RefreshStatus(Gate& gate) {
            const auto condition = EnsureBuilt(gate);
            if (!condition) return;
            const auto availability = condition->CheckAvailability();
            std::unique_lock lock(g_mutex);
            gate.ready = availability.Ready();
            gate.status = gate.ready ? std::string() : DescribeAvailability(availability);
        }

        void RunPass();

        void QueuePass() {
            if (g_passQueued.exchange(true)) return;
            const auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) {
                g_passQueued = false;
                return;
            }
            tasks->AddTask([] {
                g_passQueued = false;
                RunPass();
            });
        }

        // Main thread: drops idle pairs, refreshes status texts and starts watches for new pairs
        void RunPass() {
            const auto now = NowMs();
            g_lastPassMs = now;

            std::vector<WatchId> dropped;
            std::vector<std::shared_ptr<Gate>> gates;
            {
                std::unique_lock lock(g_mutex);
                for (auto it = g_gates.begin(); it != g_gates.end();) {
                    auto& gate = *it->second;
                    for (auto s = gate.samples.begin(); s != gate.samples.end();) {
                        if (now - s->second->lastReadMs.load(std::memory_order_relaxed) > kIdleDropMs) {
                            if (s->second->watch) dropped.push_back(s->second->watch);
                            s = gate.samples.erase(s);
                        } else {
                            ++s;
                        }
                    }
                    if (gate.samples.empty() && now - gate.lastUsedMs.load(std::memory_order_relaxed) > kIdleDropMs) {
                        it = g_gates.erase(it);
                    } else {
                        gates.push_back(it->second);
                        ++it;
                    }
                }
            }
            auto& watcher = Watcher::Get();
            for (const auto id : dropped) watcher.Unwatch(id);

            for (const auto& gate : gates) {
                RefreshStatus(*gate);

                std::shared_ptr<const Condition> condition;
                std::vector<std::pair<RE::FormID, std::shared_ptr<Sample>>> unwatched;
                {
                    std::shared_lock lock(g_mutex);
                    condition = gate->condition;
                    if (!condition) continue;
                    for (const auto& [formId, sample] : gate->samples) {
                        if (!sample->watch) unwatched.emplace_back(formId, sample);
                    }
                }
                for (auto& [formId, sample] : unwatched) {
                    auto* actor = RE::TESForm::LookupByID<RE::Actor>(formId);
                    if (!actor) continue;  // dropped once idle
                    WatchOptions options;
                    options.fireImmediately = true;
                    std::weak_ptr<Sample> weak = sample;
                    sample->watch = watcher.Watch(
                        condition, actor,
                        [weak](WatchId, RE::Actor*, bool result) {
                            if (auto alive = weak.lock()) alive->result.store(result ? 1 : 0, std::memory_order_release);
                        },
                        options);
                }
            }
        }

        // Off the main thread: the pair's latest watched result. A new pair reads false and
        // asks the main thread to start its watch.
        bool ReadWatched(const std::shared_ptr<Gate>& gate, RE::FormID formId) {
            const auto now = NowMs();
            std::shared_ptr<Sample> sample;
            {
                std::shared_lock lock(g_mutex);
                if (auto it = gate->samples.find(formId); it != gate->samples.end()) sample = it->second;
            }
            bool added = false;
            if (!sample) {
                std::unique_lock lock(g_mutex);
                auto& slot = gate->samples[formId];
                if (!slot) {
                    slot = std::make_shared<Sample>();
                    added = true;
                }
                sample = slot;
            }
            sample->lastReadMs.store(now, std::memory_order_relaxed);
            if (added || now - g_lastPassMs.load(std::memory_order_relaxed) > kPassIntervalMs) QueuePass();
            return sample->result.load(std::memory_order_acquire) == 1;
        }

        bool EvaluateGate(Kind kind, std::string_view text, RE::TESObjectREFR* refr) {
            auto* actor = AsActor(refr);
            if (!actor) return false;
            const auto gate = FindOrAddGate(kind, Normalize(kind, text));
            gate->lastUsedMs.store(NowMs(), std::memory_order_relaxed);
            if (!IsMainThread()) return ReadWatched(gate, actor->GetFormID());

            const auto condition = EnsureBuilt(*gate);
            if (!condition) return false;
            EvaluationContext context(actor);
            return condition->EvaluateAvailable(context);
        }

        // Shown by OAR's editor next to the condition for the selected reference
        std::string DescribeGate(Kind kind, std::string_view text, RE::TESObjectREFR* refr) {
            auto* actor = AsActor(refr);
            if (!actor) return "false (not an actor)";
            const auto gate = FindOrAddGate(kind, Normalize(kind, text));
            gate->lastUsedMs.store(NowMs(), std::memory_order_relaxed);

            if (IsMainThread()) {
                const auto condition = EnsureBuilt(*gate);
                if (!condition) {
                    std::shared_lock lock(g_mutex);
                    return "false (" + gate->status + ")";
                }
                const auto availability = condition->CheckAvailability();
                if (!availability.Ready()) return "false (" + DescribeAvailability(availability) + ")";
                EvaluationContext context(actor);
                return condition->EvaluateCounted(context) ? "true" : "false";
            }

            std::shared_lock lock(g_mutex);
            if (!gate->ready && !gate->status.empty()) return "false (" + gate->status + ")";
            const auto it = gate->samples.find(actor->GetFormID());
            const int result = it != gate->samples.end() ? it->second->result.load(std::memory_order_acquire) : -1;
            if (result < 0) return "false (no result yet: evaluated on the main thread)";
            return result == 1 ? "true" : "false";
        }

        RE::BSString ToBSString(const std::string& text) { return RE::BSString(std::string_view(text)); }

        class ConditionEngineCondition : public ::Conditions::CustomCondition {
        public:
            inline static constexpr std::string_view CONDITION_NAME = "ConditionEngine";

            ConditionEngineCondition() {
                textComponent_ = static_cast<::Conditions::ITextConditionComponent*>(AddBaseComponent(
                    ::Conditions::ConditionComponentType::kText, "Preset",
                    "A ConditionEngine named condition: a preset in Data/SKSE/Plugins/ConditionEngine/*.json, or "
                    "one registered by a script or another plugin."));
                textComponent_->SetAllowSpaces(true);
            }

            RE::BSString GetName() const override { return CONDITION_NAME.data(); }
            RE::BSString GetDescription() const override {
                return "True when the ConditionEngine named condition (preset) is true for this actor. Missing "
                       "presets and conditions whose plugins or integrations are unavailable read false.";
            }
            REL::Version GetRequiredVersion() const override {
                return SKSE::PluginDeclaration::GetSingleton()->GetVersion();
            }
            RE::BSString GetCurrent(RE::TESObjectREFR* refr) const override {
                return ToBSString(DescribeGate(Kind::Named, textComponent_->GetTextValue().c_str(), refr));
            }

        protected:
            bool EvaluateImpl(RE::TESObjectREFR* refr, RE::hkbClipGenerator*, void*) const override {
                return EvaluateGate(Kind::Named, textComponent_->GetTextValue().c_str(), refr);
            }

        private:
            ::Conditions::ITextConditionComponent* textComponent_ = nullptr;
        };

        class ConditionEngineExpressionCondition : public ::Conditions::CustomCondition {
        public:
            inline static constexpr std::string_view CONDITION_NAME = "ConditionEngineExpression";

            ConditionEngineExpressionCondition() {
                textComponent_ = static_cast<::Conditions::ITextConditionComponent*>(AddBaseComponent(
                    ::Conditions::ConditionComponentType::kText, "Expression",
                    "A ConditionEngine expression, e.g. Time(20, 6) && !Sneaking || @IsNight."));
                textComponent_->SetAllowSpaces(true);
            }

            RE::BSString GetName() const override { return CONDITION_NAME.data(); }
            RE::BSString GetDescription() const override {
                return "True when the ConditionEngine expression is true for this actor. Invalid expressions and "
                       "conditions whose plugins or integrations are unavailable read false.";
            }
            REL::Version GetRequiredVersion() const override {
                return SKSE::PluginDeclaration::GetSingleton()->GetVersion();
            }
            RE::BSString GetCurrent(RE::TESObjectREFR* refr) const override {
                return ToBSString(DescribeGate(Kind::Expression, textComponent_->GetTextValue().c_str(), refr));
            }

        protected:
            bool EvaluateImpl(RE::TESObjectREFR* refr, RE::hkbClipGenerator*, void*) const override {
                return EvaluateGate(Kind::Expression, textComponent_->GetTextValue().c_str(), refr);
            }

        private:
            ::Conditions::ITextConditionComponent* textComponent_ = nullptr;
        };

        const char* ResultName(OAR_API::Conditions::APIResult result) {
            switch (result) {
                case OAR_API::Conditions::APIResult::OK: return "registered";
                case OAR_API::Conditions::APIResult::AlreadyRegistered: return "already registered";
                case OAR_API::Conditions::APIResult::Invalid: return "invalid";
                default: return "failed";
            }
        }
    }  // namespace

    void Register() {
        g_mainThreadId = REX::W32::GetCurrentThreadId();
        if (!REX::W32::GetModuleHandleA("OpenAnimationReplacer.dll")) {
            SKSE::log::info("Open Animation Replacer not loaded: no OAR conditions registered");
            return;
        }
        if (!OAR_API::Conditions::GetAPI()) {
            SKSE::log::warn("Open Animation Replacer is loaded but returned no conditions API: no OAR conditions registered");
            return;
        }
        const auto named = OAR_API::Conditions::AddCustomCondition<ConditionEngineCondition>();
        const auto expression = OAR_API::Conditions::AddCustomCondition<ConditionEngineExpressionCondition>();
        SKSE::log::info("OAR conditions: {} {}, {} {}", ConditionEngineCondition::CONDITION_NAME, ResultName(named),
                        ConditionEngineExpressionCondition::CONDITION_NAME, ResultName(expression));
    }

    void OnGameLoad() {
        g_mainThreadId = REX::W32::GetCurrentThreadId();
        std::vector<WatchId> dropped;
        {
            std::unique_lock lock(g_mutex);
            for (const auto& [key, gate] : g_gates) {
                for (const auto& [formId, sample] : gate->samples) {
                    if (sample->watch) dropped.push_back(sample->watch);
                }
            }
            g_gates.clear();
        }
        auto& watcher = Watcher::Get();
        for (const auto id : dropped) watcher.Unwatch(id);
    }

}  // namespace ConditionEngine::OarConditions
