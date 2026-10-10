// BuiltinTypes.cpp - Registers every built-in condition type with the Registry.
// Each factory accepts the legacy field names and type aliases DressCode has written over time.

#include "ConditionEngine/AllConditions.h"
#include "ConditionEngine/Registry.h"

namespace ConditionEngine {

    namespace {
        using json = nlohmann::json;

        LogicalOperator ParseLogicalOperator(const json& j) {
            for (const char* key : {"operation", "operator"}) {
                if (!j.contains(key)) continue;
                const auto& v = j[key];
                if (v.is_string()) {
                    const auto s = v.get<std::string>();
                    if (s == "And" || s == "and" || s == "&&") return LogicalOperator::And;
                    if (s == "Or" || s == "or" || s == "||") return LogicalOperator::Or;
                    if (s == "Not" || s == "not" || s == "!") return LogicalOperator::Not;
                } else if (v.is_number()) {
                    return static_cast<LogicalOperator>(v.get<int>());
                }
                break;
            }
            return LogicalOperator::And;
        }

        LocationTypeCondition::LocationCategory ParseLocationCategory(const json& j) {
            using Cat = LocationTypeCondition::LocationCategory;
            if (!j.contains("category")) return Cat::Town;
            const auto& v = j["category"];
            if (v.is_number()) return static_cast<Cat>(v.get<int>());
            if (!v.is_string()) return Cat::Town;

            static const std::unordered_map<std::string, Cat> kNames = {
                {"Town", Cat::Town},           {"City", Cat::City},         {"Dungeon", Cat::Dungeon},
                {"Inn", Cat::Inn},             {"Store", Cat::Store},       {"Temple", Cat::Temple},
                {"Guild", Cat::Guild},         {"Dwelling", Cat::Dwelling}, {"Wilderness", Cat::Wilderness},
                {"Military", Cat::Military},   {"Farm", Cat::Farm},         {"Jail", Cat::Jail},
                {"PlayerHome", Cat::PlayerHome}, {"Castle", Cat::Castle},
            };
            auto it = kNames.find(v.get<std::string>());
            return it != kNames.end() ? it->second : Cat::Town;
        }

        // Value fields that may arrive as either a string or a number
        std::string ReadScalarAsString(const json& j, const std::string& key) {
            if (!j.contains(key)) return {};
            const auto& v = j[key];
            if (v.is_string()) return v.get<std::string>();
            if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
            if (v.is_null()) return {};
            return v.dump();
        }

        template <class T>
        ConditionFactory ActorState() {
            return [](const json& j) { return std::make_unique<T>(j.value("expectedState", true)); };
        }

        void Add(Registry& r, std::string name, std::vector<std::string> aliases, ConditionFactory factory,
                 std::vector<std::string> positional = {}, std::string compareField = {},
                 std::string description = {}, std::vector<std::string> derived = {}) {
            TypeInfo info;
            info.name = std::move(name);
            info.aliases = std::move(aliases);
            info.factory = std::move(factory);
            info.positional = std::move(positional);
            info.compareField = std::move(compareField);
            info.description = std::move(description);
            info.derived = std::move(derived);
            r.RegisterType(std::move(info));
        }
    }

    void RegisterBuiltinTypes(Registry& r) {
        // ── Composition ──────────────────────────────────────────────────────
        Add(r, "Logical", {"LogicalCondition"},
            [](const json& j) {
                std::vector<std::unique_ptr<Condition>> children;
                if (j.contains("children") && j["children"].is_array()) {
                    for (const auto& childJson : j["children"]) {
                        if (auto child = Condition::FromJson(childJson)) children.push_back(std::move(child));
                    }
                }
                return std::make_unique<LogicalCondition>(ParseLogicalOperator(j), std::move(children));
            },
            {}, {}, "And/Or/Not over child conditions");

        Add(r, "Constant", {"ConstantCondition"},
            [](const json& j) { return std::make_unique<ConstantCondition>(j.value("value", true)); },
            {"value"}, {}, "Always true or always false");

        Add(r, "Rule", {"RuleReferenceCondition"},
            [](const json& j) {
                return std::make_unique<RuleReferenceCondition>(
                    j.value("ruleName", ""), j.value("inverted", false), j.value("ruleUUID", ""));
            },
            {"ruleName"}, {}, "Evaluates a named condition");

        // ── Time ─────────────────────────────────────────────────────────────
        Add(r, "Time", {"TimeCondition", "TimeOfDay"},
            [](const json& j) {
                return std::make_unique<TimeCondition>(j.value("startHour", 0.0f), j.value("endHour", 0.0f),
                                                       ParseComparisonOperator(j));
            },
            {"startHour", "endHour"}, {}, "Game hour within [startHour, endHour)", {"isRange"});

        Add(r, "DayOfWeek", {"DayOfWeekCondition"},
            [](const json& j) {
                return std::make_unique<DayOfWeekCondition>(static_cast<std::uint8_t>(j.value("allowedDays", 0x7F)));
            },
            {"allowedDays"}, {}, "Bitmask of allowed weekdays (bit 0 = Sundas)", {"dayNames"});

        Add(r, "Month", {"MonthCondition"},
            [](const json& j) {
                return std::make_unique<MonthCondition>(static_cast<std::uint16_t>(j.value("allowedMonths", 0x0FFF)));
            },
            {"allowedMonths"}, {}, "Bitmask of allowed months (bit 0 = Morning Star)", {"monthNames"});

        Add(r, "DayOfMonth", {"DayOfMonthCondition"},
            [](const json& j) { return std::make_unique<DayOfMonthCondition>(j.value("allowedDays", 0x7FFFFFFFu)); },
            {"allowedDays"}, {}, "Bitmask of allowed days of the month (bit 0 = day 1)");

        // ── Location / weather ───────────────────────────────────────────────
        Add(r, "Location", {"LocationCondition"},
            [](const json& j) {
                return std::make_unique<LocationCondition>(
                    static_cast<LocationCondition::LocationType>(j.value("locationType", 0)),
                    j.value("customLocation", ""));
            },
            {"locationType", "customLocation"}, {}, "Legacy location check");

        Add(r, "InteriorExterior", {"InteriorExteriorCondition"},
            [](const json& j) { return std::make_unique<InteriorExteriorCondition>(j.value("expectInterior", true)); },
            {"expectInterior"}, {}, "Actor is in an interior (true) or exterior (false) cell");

        Add(r, "LocationType", {"LocationTypeCondition"},
            [](const json& j) { return std::make_unique<LocationTypeCondition>(ParseLocationCategory(j)); },
            {"category"}, {}, "Current location has the category's LocType keywords");

        Add(r, "LocationKeyword", {"LocationKeywordCondition"},
            [](const json& j) {
                return std::make_unique<LocationKeywordCondition>(ReadStringList(j, "keywords"), j.value("matchAny", true));
            },
            {"keywords", "matchAny"}, {}, "Current location has any/all of the keywords");

        Add(r, "Weather", {"WeatherCondition"},
            [](const json& j) {
                return std::make_unique<WeatherCondition>(j.value("weatherType", j.value("weatherName", "")),
                                                          ParseComparisonOperator(j));
            },
            {"weatherType"}, {}, "Weather classification (Pleasant/Cloudy/Rainy/Snow) or name");

        Add(r, "Temperature", {"TemperatureCondition"},
            [](const json& j) {
                float temp = 0.0f;
                auto op = ParseComparisonOperator(j);
                if (j.contains("temperatureRange") && j["temperatureRange"].is_string()) {
                    const auto range = j["temperatureRange"].get<std::string>();
                    if (range == "Freezing") { temp = 0.0f; op = ComparisonOperator::LessThan; }
                    else if (range == "Cold") { temp = 40.0f; op = ComparisonOperator::LessThan; }
                    else if (range == "Comfortable") { temp = 55.0f; op = ComparisonOperator::GreaterEqual; }
                    else if (range == "Warm") { temp = 70.0f; op = ComparisonOperator::GreaterEqual; }
                    else if (range == "Hot") { temp = 90.0f; op = ComparisonOperator::GreaterEqual; }
                } else {
                    temp = j.value("temperature", j.value("targetTemperature", 0.0f));
                }
                return std::make_unique<TemperatureCondition>(temp, op);
            },
            {"temperatureRange"}, "targetTemperature", "Temperature (needs a temperature provider)");

        // ── Actor ────────────────────────────────────────────────────────────
        Add(r, "Faction", {"FactionCondition"},
            [](const json& j) {
                return std::make_unique<FactionCondition>(j.value("factionId", j.value("factionName", "")),
                                                          j.value("minRank", j.value("minimumRank", 0)),
                                                          ParseComparisonOperator(j));
            },
            {"factionId", "minRank"}, "minRank", "Faction rank comparison");

        Add(r, "ActorValue", {"ActorValueCondition"},
            [](const json& j) {
                return std::make_unique<ActorValueCondition>(j.value("actorValue", j.value("actorValueName", "")),
                                                             j.value("value", j.value("targetValue", 0.0f)),
                                                             ParseComparisonOperator(j),
                                                             j.value("usePercentage", false));
            },
            {"actorValue", "value"}, "value", "Actor value comparison (optionally as % of max)");

        Add(r, "Race", {"RaceCondition"},
            [](const json& j) { return std::make_unique<RaceCondition>(j.value("raceId", "")); },
            {"raceId"}, {}, "Actor race (form string or editor ID)");

        Add(r, "Swimming", {"SwimmingCondition"}, ActorState<SwimmingCondition>(), {"expectedState"});
        Add(r, "Sleeping", {"SleepingCondition"}, ActorState<SleepingCondition>(), {"expectedState"});
        Add(r, "Mounting", {"MountingCondition", "Mounted"}, ActorState<MountingCondition>(), {"expectedState"});
        Add(r, "InWater", {"InWaterCondition"}, ActorState<InWaterCondition>(), {"expectedState"});
        Add(r, "LoveScene", {"LoveSceneCondition"}, ActorState<LoveSceneCondition>(), {"expectedState"});
        Add(r, "Combat", {"CombatCondition"}, ActorState<CombatCondition>(), {"expectedState"});

        Add(r, "HasSlotsEquipped", {"HasSlotsEquippedCondition"},
            [](const json& j) {
                return std::make_unique<HasSlotsEquippedCondition>(ReadUIntList(j, "slots"),
                                                                   j.value("matchMode", "any") == "all");
            },
            {"slots", "matchMode"}, {}, "Biped slots (30-61) worn by the actor");

        // ── World / scripts ──────────────────────────────────────────────────
        Add(r, "GlobalVariable", {"GlobalVariableCondition"},
            [](const json& j) {
                return std::make_unique<GlobalVariableCondition>(
                    j.value("globalId", j.value("globalName", j.value("variableName", ""))),
                    j.value("value", j.value("targetValue", 0.0f)), ParseComparisonOperator(j));
            },
            {"globalName", "value"}, "value", "Global variable comparison");

        Add(r, "QuestStage", {"QuestStageCondition"},
            [](const json& j) {
                return std::make_unique<QuestStageCondition>(j.value("questId", j.value("questName", "")),
                                                             j.value("stage", j.value("targetStage", 0)),
                                                             ParseComparisonOperator(j));
            },
            {"questId", "stage"}, "stage", "Quest stage comparison");

        Add(r, "QuestCategory", {"QuestCategoryCondition"},
            [](const json& j) -> std::unique_ptr<Condition> {
                // Multi-select "categories" (array or comma-separated), or legacy single "category"
                if (j.contains("categories")) {
                    return std::make_unique<QuestCategoryCondition>(ReadStringList(j, "categories"));
                }
                return std::make_unique<QuestCategoryCondition>(j.value("category", ""));
            },
            {"categories"}, {}, "An active quest of the category has displayed objectives");

        Add(r, "ScriptValue", {"ScriptValueCondition"},
            [](const json& j) {
                return std::make_unique<ScriptValueCondition>(j.value("scriptName", ""), j.value("variableName", ""),
                                                              j.value("valueType", "float"),
                                                              ReadScalarAsString(j, "targetValue"),
                                                              ParseComparisonOperator(j));
            },
            {"scriptName", "variableName", "valueType", "targetValue"}, "targetValue",
            "Papyrus script property on the actor");

        Add(r, "Random", {"RandomCondition"},
            [](const json& j) {
                return std::make_unique<RandomCondition>(RandomCondition::ParseSeedType(j.value("seedType", "Location")),
                                                         j.value("chance", 50), j.value("salt", ""),
                                                         j.value("intervalHours", 1));
            },
            {"chance", "seedType", "salt", "intervalHours"}, {}, "Deterministic seeded chance");
    }

} // namespace ConditionEngine
