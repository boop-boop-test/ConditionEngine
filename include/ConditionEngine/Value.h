#pragma once

// Value.h - Numeric value expressions (API v5).
//
// A value is the number a numeric condition type compares, read on its own, plus
// arithmetic over such numbers:
//
//   ActorValue(Health, usePercentage=true)      a numeric type, its comparison left out
//   $vampire_hunger                             a named value (Registry::RegisterNamedValue)
//   $hunger * (1 - $light)                      + - * /, unary -, literals
//   clamp($x, 0, 1), min(a, b), max(a, b), abs(x), lerp(a, b, t)
//   IsInterior                                  any condition, read as 0 / 1
//
// Values serialize to JSON like conditions do:
//   3.5                                                   literal
//   {"type": "Math", "operator": "*", "args": [a, b]}     + - * / neg min max clamp abs lerp
//   {"type": "NamedValue", "name": "hunger"}              $hunger
//   {"type": "Truth", "condition": {...}}                 condition as 0 / 1
//   {"type": "ActorValue", "actorValue": "Health"}        numeric type (TypeInfo::value set)
//
// The Compare condition ({"type": "Compare", "left", "operator", "right"}) compares
// two values; see Conditions/CompareCondition.h.

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/ValueResult.h"

#include <atomic>

namespace ConditionEngine {

    struct AvailabilityStatus;

    class Value {
    public:
        Value() = default;
        Value(const Value&) = delete;
        Value& operator=(const Value&) = delete;
        virtual ~Value();

        // Availability::CheckValue(*this), kept until the availability revision changes
        AvailabilityStatus CheckAvailability() const;

        // Reads the value. Non-blocking: async nodes return their latest sample.
        virtual ValueResult Read(const EvaluationContext& context) const = 0;

        // Read, timed per node type while Stats are enabled. Parents call this for children.
        ValueResult ReadCounted(const EvaluationContext& context) const;

        // Stats row (Stats::FixedSlot or the type's TypeInfo::statsSlot)
        std::uint32_t statsSlot = 0;

    private:
        mutable std::atomic<detail::AvailabilityMemo*> availabilityMemo_{nullptr};

    public:

        // Event dependency bits that can change the value
        virtual EventDependency GetEventDependencies() const = 0;

        virtual nlohmann::json ToJson() const = 0;

        // Configuration errors (missing fields, unknown types), empty when valid
        virtual std::vector<std::string> Validate() const { return {}; }

        // False when the value is the same for every actor (literals, time, weather,
        // globals, quest stages); decides a named value's inferred scope.
        virtual bool ReadsActor() const;

        // Builds a value from JSON. Returns null when the JSON is not a value
        // (unknown type, unresolved ${param}, malformed node).
        static std::unique_ptr<Value> FromJson(const nlohmann::json& json, std::string* error = nullptr);
    };

    // Reads `value` for the actor, applying static availability (required plugins,
    // host readiness) the way Condition::EvaluateAvailable does for conditions.
    ValueResult ReadValueAvailable(const Value& value, const EvaluationContext& context);

} // namespace ConditionEngine
