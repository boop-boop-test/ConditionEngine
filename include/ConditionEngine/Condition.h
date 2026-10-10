#pragma once

// Condition.h - Base condition interface plus shared enums and helpers.

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace ConditionEngine {

    class EvaluationContext;

    // Logical operators for combining conditions
    enum class LogicalOperator {
        And,
        Or,
        Not
    };

    // Event types that conditions can depend on.
    // Used to determine which game events need to trigger re-evaluation.
    enum class EventDependency : std::uint32_t {
        None            = 0,
        Time            = 1 << 0,   // Game time changes (hourly)
        Date            = 1 << 1,   // Game date changes (daily)
        Weather         = 1 << 2,   // Weather changes
        Location        = 1 << 3,   // Location/cell changes
        Combat          = 1 << 4,   // Combat state changes
        Equipment       = 1 << 5,   // Equipment changes (for actor value conditions like armor rating)
        Faction         = 1 << 6,   // Faction membership changes (rare)
        Quest           = 1 << 7,   // Quest stage changes
        ActorValue      = 1 << 8,   // Actor value changes (health, magicka, etc.)
        GlobalVariable  = 1 << 9,   // Global variable changes
        Script          = 1 << 10,  // Script value changes (poll-based)
        Temperature     = 1 << 11,  // Temperature changes (survival mods)
        // Poll-based actor state conditions (no game events, require polling)
        Swimming        = 1 << 12,  // Actor swimming state
        Sleeping        = 1 << 13,  // Actor sleeping state
        Mounting        = 1 << 14,  // Actor mounted state
        InWater         = 1 << 15,  // Actor in water state
        LoveScene       = 1 << 16,  // Actor in love scene (OStim/SexLab)
        // Generic forced-polling bucket: any condition with forcePolled=true
        // gets added to this bucket, which fires every poll tick.
        Polled          = 1 << 17,
        // Host-signalled change (e.g. DressCode player dials). Hosts fire it
        // explicitly via Watcher::NotifyEvent when their own state changes.
        Dial            = 1 << 18,
        // Inventory contents (item counts) - container-changed events
        Inventory       = 1 << 19,
        // Active magic effects, spells and perks - effect-apply events + full poll
        MagicEffect     = 1 << 20,
        // Cheap actor flags (sneaking, sitting, dialogue, bleedout, weapon drawn,
        // follower) - checked by the watcher's 1s change detection
        ActorState      = 1 << 21,
    };

    // Total number of event dependency bits (for bucket iteration)
    inline constexpr std::uint32_t kEventDependencyBitCount = 22;

    constexpr EventDependency operator|(EventDependency a, EventDependency b) {
        return static_cast<EventDependency>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }
    constexpr EventDependency operator&(EventDependency a, EventDependency b) {
        return static_cast<EventDependency>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
    }
    constexpr EventDependency& operator|=(EventDependency& a, EventDependency b) {
        a = a | b;
        return a;
    }
    constexpr bool HasDependency(EventDependency deps, EventDependency check) {
        return (static_cast<std::uint32_t>(deps) & static_cast<std::uint32_t>(check)) != 0;
    }

    // Comparison operators
    enum class ComparisonOperator {
        Equal,
        NotEqual,
        GreaterThan,
        LessThan,
        GreaterEqual,
        LessEqual
    };

    // Base condition interface
    class Condition {
    public:
        virtual ~Condition() = default;
        virtual bool Evaluate(const EvaluationContext& context) const = 0;
        virtual std::string GetDescription() const = 0;
        virtual nlohmann::json ToJson() const = 0;
        virtual std::unique_ptr<Condition> Clone() const = 0;

        // Returns the event dependencies for this condition (what events would change its result)
        virtual EventDependency GetEventDependencies() const = 0;

        // Creates a condition from JSON using the type registry (see Registry.h).
        // Returns nullptr for missing/unknown types.
        static std::unique_ptr<Condition> FromJson(const nlohmann::json& json);

        // Validate that this condition's required fields are fully configured.
        // Returns a list of human-readable error strings, empty if valid.
        virtual std::vector<std::string> Validate() const { return {}; }

        // Resolve ${paramName} references in this condition's values using the given arguments.
        // Default implementation: ToJson → substitute string values → FromJson.
        // Returns a new condition with parameters resolved (original is unchanged).
        virtual std::unique_ptr<Condition> ResolveParameters(
            const std::unordered_map<std::string, std::string>& args) const;

        // When true, this condition is re-evaluated every poll tick regardless of
        // its native event dependency.  The Polled event type is ORed into its deps.
        bool forcePolled = false;

        // Returns GetEventDependencies() | Polled (if forcePolled is set).
        // All callers that build event buckets should use this instead of the virtual.
        EventDependency GetEffectiveEventDependencies() const {
            auto deps = GetEventDependencies();
            if (forcePolled) deps |= EventDependency::Polled;
            return deps;
        }

        // Serializes the condition plus cross-cutting fields (forcePolled).
        nlohmann::json ToJsonComplete() const {
            auto j = ToJson();
            if (forcePolled) j["forcePolled"] = true;
            return j;
        }

        // Clone with cross-cutting fields preserved.
        std::unique_ptr<Condition> CloneComplete() const {
            auto c = Clone();
            if (c) c->forcePolled = forcePolled;
            return c;
        }
    };

    // Walk a JSON value tree, replacing ${varName} patterns in any string values.
    // Numeric strings ("${Threshold}") that resolve to a number are converted to JSON numbers
    // so that FromJson can parse them correctly.
    void SubstituteJsonValues(nlohmann::json& json,
                              const std::unordered_map<std::string, std::string>& args);

    // Comparison helpers used by condition implementations
    bool CompareFloat(float lhs, float rhs, ComparisonOperator op);
    bool CompareInt(int lhs, int rhs, ComparisonOperator op);
    std::string ComparisonOpToString(ComparisonOperator op);
    ComparisonOperator ParseComparisonOperator(const nlohmann::json& json, const std::string& key = "operation");

    // Parsing helpers shared by condition factories (built-in and host-registered).
    // Accept either a JSON array or a comma-separated string (the form parameterized
    // values take after ${param} substitution).
    std::vector<std::string> ReadStringList(const nlohmann::json& json, const std::string& key);
    std::vector<int> ReadIntList(const nlohmann::json& json, const std::string& key);
    std::vector<std::uint32_t> ReadUIntList(const nlohmann::json& json, const std::string& key);

} // namespace ConditionEngine
