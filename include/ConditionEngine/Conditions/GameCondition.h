#pragma once

// GameCondition - runs any vanilla Creation Kit condition function (GetLevel,
// IsSneaking, WornHasKeyword, GetIsRace, HasPerk, ...) through the engine's own
// condition evaluator, exactly as perks/dialogue/packages do.
//
//   Game(IsSneaking)                      -> IsSneaking != 0
//   Game(GetLevel) >= 20
//   Game(WornHasKeyword, ArmorHeavy)
//   Game(GetActorValuePercent, Health) < 0.3
//   Game(GetDistance, player, runOn=Subject) < 500
//
// Parameters are given as strings and converted using the function's declared
// parameter types: forms (form string "0x123|Plugin.esp", editor ID, or "player"),
// actor values (by name), sex (Male/Female), casting source (Left/Right/Voice/
// Instant) or plain integers. Functions are resolved lazily on first evaluation,
// so conditions can be built before game data is loaded.

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

#include <mutex>

namespace ConditionEngine {

    class GameCondition : public Condition {
    public:
        enum class RunOn { Subject, Target, Player };

        GameCondition(std::string function, std::vector<std::string> params, ComparisonOperator op, float value,
                      RunOn runOn = RunOn::Subject);

        bool Evaluate(const EvaluationContext& context) const override;
        // The function's raw result (GetLevel -> 20, GetActorValuePercent -> 0.3)
        ValueResult ReadValue(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override { return EventDependency::Polled; }
        std::vector<std::string> Validate() const override;

        static RunOn ParseRunOn(const std::string& text);

    private:
        struct Compiled {
            bool ok = false;
            std::string error;
            std::uint16_t functionId = 0;
            RE::SCRIPT_FUNCTION::Condition_t* function = nullptr;  // raw result, for values
            void* params[2] = {nullptr, nullptr};
        };

        const Compiled& Compile() const;

        std::string function_;
        std::vector<std::string> params_;
        ComparisonOperator op_;
        float value_;
        RunOn runOn_;

        mutable std::once_flag compileOnce_;
        mutable Compiled compiled_;
    };

} // namespace ConditionEngine
