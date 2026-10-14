#include "ConditionEngine/Conditions/PapyrusCallCondition.h"
#include "ConditionEngine/Conditions/CompareCondition.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Forms.h"
#include "ConditionEngine/Stats.h"
#include "ConditionEngine/Watcher.h"

#include <cmath>

namespace ConditionEngine {

    namespace {
        // A call that has not answered by then counts as failed and may be retried
        constexpr auto kCallTimeout = std::chrono::seconds(10);
        constexpr RE::FormID kPlayerFormId = 0x14;

        class Callback final : public RE::BSScript::IStackCallbackFunctor {
        public:
            explicit Callback(std::function<void(RE::BSScript::Variable)> fn) : fn_(std::move(fn)) {}
            void operator()(RE::BSScript::Variable result) override { fn_(result); }
            void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

        private:
            std::function<void(RE::BSScript::Variable)> fn_;
        };

        bool GamePaused() {
            auto* ui = RE::UI::GetSingleton();
            return ui && ui->GameIsPaused();
        }

        std::optional<double> ToNumber(const RE::BSScript::Variable& v) {
            if (v.IsInt()) return static_cast<double>(v.GetSInt());
            if (v.IsFloat() && std::isfinite(v.GetFloat())) return static_cast<double>(v.GetFloat());
            if (v.IsBool()) return v.GetBool() ? 1.0 : 0.0;
            return std::nullopt;
        }

        // Starts the call; false (with `error`) when it could not be dispatched
        bool Dispatch(const PapyrusCallCondition::Call& call, RE::Actor* actor,
                      std::function<void(RE::BSScript::Variable)> onResult, std::string& error) {
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!vm) {
                error = "Papyrus VM not available";
                return false;
            }
            RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{new Callback(std::move(onResult))};
            auto* args = call.passActor ? RE::MakeFunctionArguments(std::move(actor)) : RE::MakeFunctionArguments();

            if (call.form.empty()) {
                if (vm->DispatchStaticCall(call.script, call.function, args, callback)) return true;
                error = "could not call " + call.script + "." + call.function;
                return false;
            }

            auto* owner = Forms::LookupAny(call.form);
            auto* policy = vm->GetObjectHandlePolicy();
            if (!owner || !policy) {
                error = "form not found: " + call.form;
                return false;
            }
            const auto handle = policy->GetHandleForObject(static_cast<RE::VMTypeID>(owner->GetFormType()), owner);
            if (!handle || !vm->DispatchMethodCall(handle, call.script, call.function, args, callback)) {
                error = "could not call " + call.script + "." + call.function + " on " + call.form;
                return false;
            }
            return true;
        }

        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        const std::string& KeyOf(const PapyrusCallCondition::Call& call, std::string& scratch) {
            if (!call.key.empty()) return call.key;
            scratch = PapyrusCallCondition::Call::MakeKey(call);
            return scratch;
        }
    }

    // ── Cache and dispatcher ─────────────────────────────────────────────────

    PapyrusCalls& PapyrusCalls::Get() {
        static PapyrusCalls instance;
        return instance;
    }

    std::string PapyrusCallCondition::Call::MakeKey(const Call& call) {
        return Lower(call.form + "|" + call.script + "." + call.function);
    }

    void PapyrusCalls::Expire(const PapyrusCallCondition::Call& call, RE::FormID actorId) {
        std::string scratch;
        const auto& key = KeyOf(call, scratch);
        std::lock_guard lock(mutex_);
        auto byCall = samples_.find(key);
        if (byCall == samples_.end()) return;
        for (auto& [slot, sample] : byCall->second) {
            if (actorId == 0 || slot == actorId) sample.expired = true;
        }
    }

    void PapyrusCalls::ClearAll() {
        std::lock_guard lock(mutex_);
        samples_.clear();
        ++generation_;
    }

    ValueResult PapyrusCalls::Read(const PapyrusCallCondition::Call& call, RE::Actor* actor) {
        if (call.passActor && !actor) return ValueResult::Fail(ValueStatus::Unavailable, "no actor");
        const RE::FormID actorId = actor ? actor->GetFormID() : kPlayerFormId;
        std::string scratch;
        const std::string& key = KeyOf(call, scratch);
        const RE::FormID slot = call.passActor ? actorId : 0;
        const auto interval = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(std::max(call.intervalSeconds, 0.05)));

        ValueResult result;
        bool dispatch = false;
        std::uint64_t generation = 0, request = 0;
        {
            std::lock_guard lock(mutex_);
            auto byCall = samples_.find(key);
            if (byCall == samples_.end()) byCall = samples_.emplace(key, std::unordered_map<RE::FormID, Sample>{}).first;
            auto& sample = byCall->second[slot];
            const auto now = Clock::now();
            // Waiting only counts while the game runs (the VM is stopped while paused).
            // Gaps between reads count at most 2 s, so a long pause without reads is not
            // charged all at once when the game resumes.
            if (sample.lastObserved != Clock::time_point{} && sample.pending && !GamePaused()) {
                sample.pendingFor += std::min<Clock::duration>(now - sample.lastObserved, std::chrono::seconds(2));
            }
            sample.lastObserved = now;
            if (sample.pending && sample.pendingFor > kCallTimeout) {
                Stats::CountPapyrusCall(Stats::PapyrusOutcome::TimedOut);
                sample.pending = false;
                sample.failed = true;
                sample.reason = call.script + "." + call.function + " did not answer";
            }
            if (!sample.pending && (sample.expired || sample.request == 0 || now - sample.requestedAt >= interval)) {
                sample.expired = false;
                sample.pending = true;
                sample.requestedAt = now;
                sample.pendingFor = {};
                sample.request = ++nextRequest_;
                dispatch = true;
                generation = generation_;
                request = sample.request;
            }

            if (sample.value && !sample.failed) {
                result = ValueResult::Of(*sample.value);
                result.sampledAt = sample.sampledAt;
            } else if (sample.failed) {
                result = ValueResult::Fail(ValueStatus::Unavailable, sample.reason, sample.value.value_or(0.0));
                if (sample.value) result.sampledAt = sample.sampledAt;
            } else {
                result = ValueResult::Fail(ValueStatus::Pending, "waiting for " + call.script + "." + call.function);
            }
        }

        if (dispatch) {
            Stats::CountPapyrusCall(Stats::PapyrusOutcome::Dispatched);
            std::string error;
            const bool started = Dispatch(call, actor,
                [this, key = std::string(key), slot, generation, request, actorId](RE::BSScript::Variable value) {
                    auto number = ToNumber(value);
                    Complete(key, slot, generation, request, actorId, number, number ? std::string{} : "returned no number");
                },
                error);
            if (!started) Complete(key, slot, generation, request, actorId, std::nullopt, error);
        }
        return result;
    }

    void PapyrusCalls::Complete(const std::string& callKey, RE::FormID slot, std::uint64_t generation, std::uint64_t request,
                                RE::FormID actorId, std::optional<double> value, std::string reason) {
        {
            std::lock_guard lock(mutex_);
            if (generation != generation_) return;  // from before a game load
            auto byCall = samples_.find(callKey);
            if (byCall == samples_.end()) return;
            auto it = byCall->second.find(slot);
            if (it == byCall->second.end() || it->second.request != request) return;
            auto& sample = it->second;
            sample.pending = false;
            Stats::CountPapyrusCall(value ? Stats::PapyrusOutcome::Completed : Stats::PapyrusOutcome::Failed);
            if (value) {
                sample.value = value;
                sample.sampledAt = Clock::now();
                sample.failed = false;
                sample.reason.clear();
            } else {
                sample.failed = true;
                sample.reason = std::move(reason);
            }
        }
        Watcher::Get().PostEvent(EventDependency::Value, actorId);
    }

    // ── Condition ────────────────────────────────────────────────────────────

    PapyrusCallCondition::PapyrusCallCondition(Call call, ComparisonOperator op, double value)
        : call_(std::move(call)), op_(op), value_(value) {
        call_.key = Call::MakeKey(call_);
    }

    ValueResult PapyrusCallCondition::ReadValue(const EvaluationContext& context) const {
        if (call_.script.empty() || call_.function.empty()) {
            return ValueResult::Fail(ValueStatus::Error, "PapyrusCall needs a script and a function");
        }
        return PapyrusCalls::Get().Read(call_, context.GetActor());
    }

    bool PapyrusCallCondition::Evaluate(const EvaluationContext& context) const {
        return EvaluateTri(context).state == TriState::True;
    }

    TriResult PapyrusCallCondition::EvaluateTri(const EvaluationContext& context) const {
        const auto current = ReadValue(context);
        if (current.status == ValueStatus::Pending) return TriResult::Pending(current.reason);
        if (!current.Ok()) return TriResult::Unavailable(current.reason);
        return TriResult::Of(CompareValues(current.value, op_, value_));
    }

    std::string PapyrusCallCondition::GetDescription() const {
        std::string desc = call_.script + "." + call_.function + "()";
        if (!call_.form.empty()) desc += " on " + call_.form;
        return desc + " " + ComparisonOpToString(op_) + " " + std::format("{}", value_);
    }

    nlohmann::json PapyrusCallCondition::ToJson() const {
        nlohmann::json json{{"type", "PapyrusCall"}, {"script", call_.script}, {"function", call_.function}};
        if (!call_.form.empty()) json["form"] = call_.form;
        if (!call_.passActor) json["passActor"] = false;
        if (call_.intervalSeconds != 1.0) json["interval"] = call_.intervalSeconds;
        // "!= 0" is the default (true-ish) and is left implicit
        if (!(op_ == ComparisonOperator::NotEqual && value_ == 0.0)) {
            json["operator"] = ComparisonOpToName(op_);
            json["value"] = value_;
        }
        return json;
    }

    std::unique_ptr<Condition> PapyrusCallCondition::Clone() const {
        return std::make_unique<PapyrusCallCondition>(call_, op_, value_);
    }

    std::vector<std::string> PapyrusCallCondition::Validate() const {
        std::vector<std::string> errors;
        if (call_.script.empty()) errors.push_back("PapyrusCall: no script specified");
        if (call_.function.empty()) errors.push_back("PapyrusCall: no function specified");
        return errors;
    }

} // namespace ConditionEngine
