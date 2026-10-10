#pragma once

// Random seeded conditions:
// Deterministic true/false based on hashing game state with a user salt.
// The seed type determines WHAT gets hashed and WHEN it re-evaluates.

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    // What game state to use as the random seed
    enum class RandomSeedType {
        Location,   // BGSLocation name — changes when player enters a new location
        ActorName,  // Actor base name — fixed per actor forever
        Day,        // In-game day number — changes each new day
        Time,       // Game hour (truncated) — changes every N hours (configurable interval)
    };

    // Single random condition class with configurable seed type
    class RandomCondition : public Condition {
    public:
        RandomCondition(RandomSeedType seedType = RandomSeedType::Location,
                        int chance = 50,
                        const std::string& salt = "",
                        int intervalHours = 1);
        
        bool Evaluate(const EvaluationContext& context) const override;
        std::string GetDescription() const override;
        nlohmann::json ToJson() const override;
        std::unique_ptr<Condition> Clone() const override;
        EventDependency GetEventDependencies() const override;

        // Accessors
        RandomSeedType GetSeedType() const { return seedType; }
        int GetChance() const { return chance; }
        const std::string& GetSalt() const { return salt; }
        int GetIntervalHours() const { return intervalHours; }

    private:
        RandomSeedType seedType;
        int chance;          // 0-100 percentage threshold
        std::string salt;    // User-provided salt for independence between conditions
        int intervalHours;   // For Time seed type: how many hours per bucket (1-24)

        // FNV-1a hash for deterministic randomness
        static std::uint32_t FNV1aHash(const std::string& input);

        // Build the seed string from current game state
        std::string BuildSeedString(const EvaluationContext& context) const;

    public:
        // Helper to get seed type name
        static std::string SeedTypeToString(RandomSeedType type);
        static RandomSeedType ParseSeedType(const std::string& str);
    };

} // namespace ConditionEngine
