#include "ConditionEngine/Conditions/RandomConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"

namespace ConditionEngine {

    // ============================================================================
    // RandomCondition
    // ============================================================================

    RandomCondition::RandomCondition(RandomSeedType seedType, int chance, const std::string& salt, int intervalHours)
        : seedType(seedType)
        , chance(std::clamp(chance, 0, 100))
        , salt(salt)
        , intervalHours(std::clamp(intervalHours, 1, 24)) {}

    // FNV-1a 32-bit hash — fast, good distribution, deterministic
    std::uint32_t RandomCondition::FNV1aHash(const std::string& input) {
        constexpr std::uint32_t FNV_OFFSET_BASIS = 2166136261u;
        constexpr std::uint32_t FNV_PRIME = 16777619u;

        std::uint32_t hash = FNV_OFFSET_BASIS;
        for (char c : input) {
            hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(c));
            hash *= FNV_PRIME;
        }
        return hash;
    }

    std::string RandomCondition::BuildSeedString(const EvaluationContext& context) const {
        std::string seed;

        switch (seedType) {
            case RandomSeedType::Location: {
                auto* location = context.GetCurrentLocation();
                if (location) {
                    const char* name = location->GetName();
                    if (name && name[0]) {
                        seed = name;
                    } else {
                        // Fallback to formID if location has no name
                        seed = "loc_" + std::to_string(location->GetFormID());
                    }
                } else {
                    // No location — use the actor's current cell name as fallback
                    auto* actor = context.GetActor();
                    if (actor) {
                        auto* cell = actor->GetParentCell();
                        if (cell) {
                            const char* cellName = cell->GetName();
                            if (cellName && cellName[0]) {
                                seed = std::string("cell_") + cellName;
                            } else {
                                seed = "cell_" + std::to_string(cell->GetFormID());
                            }
                        }
                    }
                    if (seed.empty()) {
                        seed = "no_location";
                    }
                }
                break;
            }

            case RandomSeedType::ActorName: {
                auto* actor = context.GetActor();
                if (actor) {
                    // Use the actor's base form name for consistency
                    auto* base = actor->GetActorBase();
                    if (base) {
                        const char* name = base->GetName();
                        if (name && name[0]) {
                            seed = name;
                        } else {
                            seed = "actor_" + std::to_string(base->GetFormID());
                        }
                    } else {
                        seed = "actor_" + std::to_string(actor->GetFormID());
                    }
                } else {
                    seed = "no_actor";
                }
                break;
            }

            case RandomSeedType::Day: {
                auto* calendar = RE::Calendar::GetSingleton();
                if (calendar) {
                    // Use the total day count for a seed that changes daily
                    float totalDays = calendar->GetHoursPassed() / 24.0f;
                    seed = "day_" + std::to_string(static_cast<int>(totalDays));
                } else {
                    seed = "day_0";
                }
                break;
            }

            case RandomSeedType::Time: {
                auto* calendar = RE::Calendar::GetSingleton();
                if (calendar) {
                    // Bucket by intervalHours — e.g., interval=4 means 0-3, 4-7, 8-11, etc.
                    float totalHours = calendar->GetHoursPassed();
                    int bucket = static_cast<int>(totalHours) / intervalHours;
                    seed = "time_" + std::to_string(bucket);
                } else {
                    seed = "time_0";
                }
                break;
            }
        }

        // Append user salt for independence between conditions
        if (!salt.empty()) {
            seed += "|" + salt;
        }

        return seed;
    }

    bool RandomCondition::Evaluate(const EvaluationContext& context) const {
        std::string seed = BuildSeedString(context);
        std::uint32_t hash = FNV1aHash(seed);
        
        // Map hash to 0-99 range and compare against chance
        int roll = static_cast<int>(hash % 100);
        return roll < chance;
    }

    std::string RandomCondition::GetDescription() const {
        std::string desc = "Random (" + SeedTypeToString(seedType) + ") " + std::to_string(chance) + "% chance";
        if (!salt.empty()) {
            desc += " [salt: " + salt + "]";
        }
        if (seedType == RandomSeedType::Time) {
            desc += " [every " + std::to_string(intervalHours) + "h]";
        }
        return desc;
    }

    nlohmann::json RandomCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "Random";
        json["seedType"] = SeedTypeToString(seedType);
        json["chance"] = chance;
        json["salt"] = salt;
        if (seedType == RandomSeedType::Time) {
            json["intervalHours"] = intervalHours;
        }
        return json;
    }

    std::unique_ptr<Condition> RandomCondition::Clone() const {
        return std::make_unique<RandomCondition>(seedType, chance, salt, intervalHours);
    }

    EventDependency RandomCondition::GetEventDependencies() const {
        // Random conditions are passive — they never trigger re-evaluation on
        // their own.  Their hash-based result is deterministic for a given game
        // state, so when any *other* condition triggers a full schedule eval the
        // random will naturally produce the correct value.  Putting them in event
        // buckets would cause spurious forced re-evals every poll cycle.
        return EventDependency::None;
    }

    std::string RandomCondition::SeedTypeToString(RandomSeedType type) {
        switch (type) {
            case RandomSeedType::Location:  return "Location";
            case RandomSeedType::ActorName: return "ActorName";
            case RandomSeedType::Day:       return "Day";
            case RandomSeedType::Time:      return "Time";
            default:                        return "Location";
        }
    }

    RandomSeedType RandomCondition::ParseSeedType(const std::string& str) {
        if (str == "Location")  return RandomSeedType::Location;
        if (str == "ActorName") return RandomSeedType::ActorName;
        if (str == "Day")       return RandomSeedType::Day;
        if (str == "Time")      return RandomSeedType::Time;
        return RandomSeedType::Location; // default
    }

} // namespace ConditionEngine
