#pragma once

// ValueResult.h - The result of reading a numeric value (see Value.h).
//
// Kept separate from Value.h so Condition.h can declare Condition::ReadValue
// without pulling in the value node classes.

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

namespace ConditionEngine {

    // Matches CE_ValueStatus in the C API
    enum class ValueStatus : std::int32_t {
        Ok = 0,
        Pending = 1,      // no sample yet (async) or a dependency is not ready
        Unavailable = 2,  // missing plugin/form/actor, disabled for this actor, expired push
        Error = 3         // misconfigured or failed to evaluate
    };

    const char* ValueStatusName(ValueStatus status);  // "ok", "pending", "unavailable", "error"

    struct ValueResult {
        using Clock = std::chrono::steady_clock;

        double value = 0.0;
        ValueStatus status = ValueStatus::Ok;
        std::string reason;                       // set when status != Ok
        // Combined values: the provider that answered (its definition's name; definitions
        // outlive the read). Use FromText().
        const std::string* from = nullptr;
        bool fromOwnExpression = false;
        std::optional<Clock::time_point> sampledAt;  // nullopt = read live just now

        bool Ok() const { return status == ValueStatus::Ok; }
        std::string FromText() const { return from ? *from + (fromOwnExpression ? " (own expression)" : "") : std::string{}; }

        static ValueResult Of(double v) { return ValueResult{v}; }
        static ValueResult Fail(ValueStatus status, std::string reason, double fallback = 0.0) {
            return ValueResult{fallback, status, std::move(reason), nullptr, false, std::nullopt};
        }
    };

    // Folds `other` into `into`: the worse status wins (Error > Unavailable > Pending > Ok),
    // reasons are joined and the older sample time is kept. The value is left alone.
    void MergeValueStatus(ValueResult& into, const ValueResult& other);

    // What a numeric type reads, for catalogs and editors
    struct ValueInfo {
        std::string unit;             // "%", "h", "" ...
        std::optional<double> min;
        std::optional<double> max;
        bool integer = false;

        nlohmann::json ToJson() const;  // {"unit", "range": [min, max], "integer"}
    };

} // namespace ConditionEngine
