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

    }

    void RegisterBuiltinTypes(Registry& r) {
        using namespace Field;

        // ── Categories ───────────────────────────────────────────────────────
        r.RegisterCategory({"logic", "Logic", 0});
        r.RegisterCategory({"time", "Time & Date", 10});
        r.RegisterCategory({"location", "Location", 20});
        r.RegisterCategory({"weather", "Weather & Climate", 30});
        r.RegisterCategory({"actor", "Actor", 40});
        r.RegisterCategory({"actorState", "Actor State", 50});
        r.RegisterCategory({"gear", "Gear & Inventory", 60});
        r.RegisterCategory({"quest", "Quests", 70});
        r.RegisterCategory({"world", "World & Mods", 80});
        r.RegisterCategory({"script", "Scripts & Variables", 90});
        r.RegisterCategory({"game", "Vanilla Conditions", 95});
        r.RegisterCategory({"other", "Other", 100});
        // Defaults for named values / conditions that name no category
        r.RegisterCategory({"values", "Named Values", 110});
        r.RegisterCategory({"named", "Named Conditions", 120});
        r.RegisterSource("ConditionEngine", "ConditionEngine");

        // ── Logic ────────────────────────────────────────────────────────────
        TypeBuilder("Logical", "Logic Group", "logic")
            .Aliases({"LogicalCondition"})
            .Icon("∧")
            .Describe("And/Or/Not over child conditions")
            .Fields({Enum("operator", "Operator", Options({"And", "Or", "Not"}), "And"),
                     Children("children", "Conditions")})
            .Positional({})
            .Hidden()  // composed with && / || / ! rather than picked as a type
            .Factory([](const json& j) {
                std::vector<std::unique_ptr<Condition>> children;
                if (j.contains("children") && j["children"].is_array()) {
                    for (const auto& childJson : j["children"]) {
                        if (auto child = Condition::FromJson(childJson)) children.push_back(std::move(child));
                    }
                }
                return std::make_unique<LogicalCondition>(ParseLogicalOperator(j), std::move(children));
            })
            .Register(r);

        TypeBuilder("Constant", "Always / Never", "logic")
            .Aliases({"ConstantCondition"})
            .Icon("⊤")
            .Describe("Always true or always false")
            .Fields({Bool("value", "Value", true)})
            .Factory([](const json& j) { return std::make_unique<ConstantCondition>(j.value("value", true)); })
            .Register(r);

        TypeBuilder("Rule", "Rule Reference", "logic")
            .Aliases({"RuleReferenceCondition"})
            .Icon("📋")
            .Describe("Use another rule or named condition")
            .Fields({Named("ruleName", "Rule").Describe("Name of the rule or named condition"),
                     String("ruleUUID", "Rule UUID").Describe("Set by editors that pick rules by id"),
                     Bool("inverted", "Invert result (NOT)", false)})
            .Positional({"ruleName"})
            .Factory([](const json& j) {
                return std::make_unique<RuleReferenceCondition>(
                    j.value("ruleName", ""), j.value("inverted", false), j.value("ruleUUID", ""));
            })
            .Register(r);

        TypeBuilder("Compare", "Compare Values", "logic")
            .Aliases({"CompareCondition"})
            .Icon("⚖")
            .Describe("Compares two values, e.g. $hunger > 80 or ActorValue(Health) * 2 < ActorValue(Magicka)")
            .Fields({ValueExpr("left", "Left"), Comparison("operator", "Operator", "GreaterOrEqual"),
                     ValueExpr("right", "Right")})
            .Positional({})
            .Hidden()  // written as a comparison between values rather than picked as a type
            .Factory([](const json& j) {
                const json left = j.value("left", json());
                const json right = j.value("right", json());
                std::shared_ptr<const Value> lv = Value::FromJson(left);
                std::shared_ptr<const Value> rv = Value::FromJson(right);
                return std::make_unique<CompareCondition>(lv, ParseComparisonOperator(j), rv,
                                                          lv ? json() : left, rv ? json() : right);
            })
            .Register(r);

        // ── Time & date ──────────────────────────────────────────────────────
        TypeBuilder("Time", "Time of Day", "time")
            .Aliases({"TimeCondition", "TimeOfDay"})
            .Icon("🕐")
            .Describe("Game hour within [start, end); wraps past midnight when start > end")
            .Fields({Int("startHour", "Start Hour", 6).Range(0, 23), Int("endHour", "End Hour", 18).Range(0, 23)})
            .Positional({"startHour", "endHour"})
            .Derived({"isRange"})
            .Value({.unit = "h", .min = 0, .max = 24})
            .Factory([](const json& j) {
                return std::make_unique<TimeCondition>(j.value("startHour", 0.0f), j.value("endHour", 0.0f),
                                                       ParseComparisonOperator(j));
            })
            .Register(r);

        TypeBuilder("DayOfWeek", "Day of Week", "time")
            .Aliases({"DayOfWeekCondition"})
            .Icon("📅")
            .Describe("Bitmask of allowed weekdays (bit 0 = Sundas ... bit 6 = Loredas)")
            .Fields({Int("allowedDays", "Allowed Days (bitmask)", 0x7F).Range(0, 0x7F)})
            .Derived({"dayNames"})
            .Value({.min = 0, .max = 6, .integer = true})
            .Factory([](const json& j) {
                return std::make_unique<DayOfWeekCondition>(static_cast<std::uint8_t>(j.value("allowedDays", 0x7F)));
            })
            .Register(r);

        TypeBuilder("Month", "Month", "time")
            .Aliases({"MonthCondition"})
            .Icon("📅")
            .Describe("Bitmask of allowed months (bit 0 = Morning Star ... bit 11 = Evening Star)")
            .Fields({Int("allowedMonths", "Allowed Months (bitmask)", 0x0FFF).Range(0, 0x0FFF)})
            .Derived({"monthNames"})
            .Value({.min = 0, .max = 11, .integer = true})
            .Factory([](const json& j) {
                return std::make_unique<MonthCondition>(static_cast<std::uint16_t>(j.value("allowedMonths", 0x0FFF)));
            })
            .Register(r);

        TypeBuilder("DayOfMonth", "Day of Month", "time")
            .Aliases({"DayOfMonthCondition"})
            .Icon("📅")
            .Describe("Bitmask of allowed days of the month (bit 0 = day 1)")
            .Fields({Int("allowedDays", "Allowed Days (bitmask)", 0x7FFFFFFF)})
            .Value({.min = 1, .max = 31, .integer = true})
            .Factory([](const json& j) { return std::make_unique<DayOfMonthCondition>(j.value("allowedDays", 0x7FFFFFFFu)); })
            .Register(r);

        // ── Location ─────────────────────────────────────────────────────────
        TypeBuilder("Location", "Location (legacy)", "location")
            .Aliases({"LocationCondition"})
            .Describe("Legacy location check - use InteriorExterior / LocationType / InLocation")
            .Fields({Int("locationType", "Location Type", 0), String("customLocation", "Location Name", "")})
            .Hidden()
            .Factory([](const json& j) {
                return std::make_unique<LocationCondition>(
                    static_cast<LocationCondition::LocationType>(j.value("locationType", 0)),
                    j.value("customLocation", ""));
            })
            .Register(r);

        TypeBuilder("InteriorExterior", "Interior/Exterior", "location")
            .Aliases({"InteriorExteriorCondition"})
            .Icon("🏠")
            .Describe("Actor is in an interior or exterior cell")
            .Fields({Bool("expectInterior", "Interior (unchecked = exterior)", true)})
            .Factory([](const json& j) { return std::make_unique<InteriorExteriorCondition>(j.value("expectInterior", true)); })
            .Register(r);

        TypeBuilder("LocationType", "Location Type", "location")
            .Aliases({"LocationTypeCondition"})
            .Icon("🏰")
            .Describe("Current location (or a parent) has the category's LocType keywords")
            .Fields({Enum("category", "Location Category",
                          {{"Town", "Town", "LocTypeTown"},
                           {"City", "City", "LocTypeCity"},
                           {"Dungeon", "Dungeon", "LocTypeDungeon"},
                           {"Inn", "Inn", "LocTypeInn"},
                           {"Store", "Store", "LocTypeStore"},
                           {"Temple", "Temple", "LocTypeTemple"},
                           {"Guild", "Guild Hall", "LocTypeGuild"},
                           {"Dwelling", "Dwelling/Home", "LocTypeDwelling"},
                           {"Farm", "Farm", "LocTypeFarm / LumberMill"},
                           {"Military", "Military", "Camp/Fort/Barracks"},
                           {"Jail", "Jail", "LocTypeJail"},
                           {"PlayerHome", "Player Home", "LocTypePlayerHouse"},
                           {"Castle", "Castle", "LocTypeCastle"},
                           {"Wilderness", "Wilderness", "No location type"}},
                          "Town")})
            .Factory([](const json& j) { return std::make_unique<LocationTypeCondition>(ParseLocationCategory(j)); })
            .Register(r);

        TypeBuilder("LocationKeyword", "Location Keyword", "location")
            .Aliases({"LocationKeywordCondition"})
            .Icon("🏷️")
            .Describe("Current location has any/all of the keywords")
            .Fields({FormList("keywords", "Location Keywords", {"Keyword"}).Describe("e.g. LocTypeCity, LocTypeDwelling"),
                     Bool("matchAny", "Match ANY keyword (unchecked = ALL)", true)})
            .Factory([](const json& j) {
                return std::make_unique<LocationKeywordCondition>(ReadStringList(j, "keywords"), j.value("matchAny", true));
            })
            .Register(r);

        // ── Weather & climate ────────────────────────────────────────────────
        TypeBuilder("Weather", "Weather", "weather")
            .Aliases({"WeatherCondition"})
            .Icon("🌤️")
            .Describe("Weather classification, or a weather's exact name")
            .Fields({Enum("weatherType", "Weather Type", Options({"Clear", "Cloudy", "Rainy", "Snowy"}), "Clear")
                         .Custom()
                         .Describe("Clear/Cloudy/Rainy/Snowy classification, or a weather name")})
            .Factory([](const json& j) {
                return std::make_unique<WeatherCondition>(j.value("weatherType", j.value("weatherName", "")),
                                                          ParseComparisonOperator(j));
            })
            .Register(r);

        TypeBuilder("Temperature", "Temperature", "weather")
            .Aliases({"TemperatureCondition"})
            .Icon("🌡️")
            .Describe("Temperature from the configured provider (ConditionEngine.ini [Temperature])")
            .Fields({Enum("temperatureRange", "Temperature Range",
                          {{"Freezing", "Freezing", "< 0"},
                           {"Cold", "Cold", "0-40"},
                           {"Comfortable", "Comfortable", "40-70"},
                           {"Warm", "Warm", "70-90"},
                           {"Hot", "Hot", "> 90"}},
                          "Comfortable")
                         .Describe("Preset range; overrides the exact temperature"),
                     Float("targetTemperature", "Exact Temperature")})
            .Positional({"temperatureRange"})
            .Compare("targetTemperature")
            .Value({})
            .Factory([](const json& j) {
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
            })
            .Register(r);

        // ── Actor ────────────────────────────────────────────────────────────
        TypeBuilder("Faction", "Faction", "actor")
            .Aliases({"FactionCondition"})
            .Icon("⚔️")
            .Describe("Faction rank comparison (-1 = not in faction, 0 = member, 1+ = rank)")
            .Fields({Form("factionId", "Faction", {"Faction"}).Required(),
                     Comparison("operator", "Operator", "GreaterOrEqual"),
                     Int("minRank", "Rank", 0).Range(-1, 127)})
            .Positional({"factionId", "minRank"})
            .Compare("minRank")
            .Value({.min = -1, .max = 127, .integer = true})
            .Factory([](const json& j) {
                return std::make_unique<FactionCondition>(j.value("factionId", j.value("factionName", "")),
                                                          j.value("minRank", j.value("minimumRank", 0)),
                                                          ParseComparisonOperator(j));
            })
            .Register(r);

        TypeBuilder("ActorValue", "Actor Value", "actor")
            .Aliases({"ActorValueCondition"})
            .Icon("💪")
            .Describe("Actor value comparison, optionally as a percentage of the maximum")
            .Fields({Enum("actorValue", "Actor Value",
                          {{"Health", "Health"}, {"Magicka", "Magicka"}, {"Stamina", "Stamina"},
                           {"CarryWeight", "Carry Weight"}, {"Level", "Level"}, {"Sneak", "Sneak Skill"}},
                          "Health")
                         .Custom()
                         .Describe("Any actor value name"),
                     Comparison("operator", "Operator", "GreaterOrEqual"),
                     Float("value", "Value", 50),
                     Bool("usePercentage", "Use percentage (vs absolute value)", true)})
            .Positional({"actorValue", "value"})
            .Compare("value")
            .Value({})
            .Factory([](const json& j) {
                return std::make_unique<ActorValueCondition>(j.value("actorValue", j.value("actorValueName", "")),
                                                             j.value("value", j.value("targetValue", 0.0f)),
                                                             ParseComparisonOperator(j),
                                                             j.value("usePercentage", false));
            })
            .Register(r);

        TypeBuilder("Race", "Race", "actor")
            .Aliases({"RaceCondition"})
            .Icon("🧬")
            .Describe("Actor race")
            .Fields({Form("raceId", "Race", {"Race"}).Required()})
            .Factory([](const json& j) { return std::make_unique<RaceCondition>(j.value("raceId", "")); })
            .Register(r);

        auto actorState = [&r](const char* name, const char* label, const char* icon, const char* description,
                               std::vector<std::string> aliases, ConditionFactory factory) {
            TypeBuilder(name, label, "actorState")
                .Aliases(std::move(aliases))
                .Icon(icon)
                .Describe(description)
                .Fields({Bool("expectedState", "Expected", true)})
                .Factory(std::move(factory))
                .Register(r);
        };
        actorState("Swimming", "Swimming", "🏊", "Actor is swimming", {"SwimmingCondition"}, ActorState<SwimmingCondition>());
        actorState("Sleeping", "Sleeping", "😴", "Actor is sleeping", {"SleepingCondition"}, ActorState<SleepingCondition>());
        actorState("Mounting", "Mounted", "🐎", "Actor is riding a mount", {"MountingCondition", "Mounted"},
                   ActorState<MountingCondition>());
        actorState("InWater", "In Water", "💧", "Actor is standing or wading in water", {"InWaterCondition"},
                   ActorState<InWaterCondition>());
        actorState("LoveScene", "In Love Scene", "💞", "Actor is in an OStim/SexLab scene (needs a provider)",
                   {"LoveSceneCondition"}, ActorState<LoveSceneCondition>());
        actorState("Combat", "In Combat", "🗡️", "Actor is in combat", {"CombatCondition"}, ActorState<CombatCondition>());

        TypeBuilder("HasSlotsEquipped", "Slots Equipped", "gear")
            .Aliases({"HasSlotsEquippedCondition"})
            .Icon("🛡️")
            .Describe("Biped slots (30-61) the actor is wearing armor in")
            .Fields({IntList("slots", "Armor Slots").Describe("Biped slot numbers 30-61"),
                     Enum("matchMode", "Match Mode",
                          {{"any", "Any - at least one slot equipped"}, {"all", "All - every slot must be equipped"}},
                          "any")})
            .Factory([](const json& j) {
                return std::make_unique<HasSlotsEquippedCondition>(ReadUIntList(j, "slots"),
                                                                   j.value("matchMode", "any") == "all");
            })
            .Register(r);

        // ── Quests ───────────────────────────────────────────────────────────
        TypeBuilder("QuestStage", "Quest Stage", "quest")
            .Aliases({"QuestStageCondition"})
            .Icon("📜")
            .Describe("Quest's current stage comparison")
            .Fields({Form("questId", "Quest", {"Quest"}).Required(),
                     Comparison("operator", "Operator", "GreaterOrEqual"),
                     Int("stage", "Stage", 0).Range(0, 65535)})
            .Positional({"questId", "stage"})
            .Compare("stage")
            .Value({.min = 0, .max = 65535, .integer = true})
            .Factory([](const json& j) {
                return std::make_unique<QuestStageCondition>(j.value("questId", j.value("questName", "")),
                                                             j.value("stage", j.value("targetStage", 0)),
                                                             ParseComparisonOperator(j));
            })
            .Register(r);

        TypeBuilder("QuestCategory", "Quest Category", "quest")
            .Aliases({"QuestCategoryCondition"})
            .Icon("📖")
            .Describe("Any quest in the selected categories is active with displayed objectives")
            .Fields({EnumList("categories", "Quest Categories",
                              {{"MainQuest", "Main Quest", "Dragonborn prophecy questline"},
                               {"Companions", "Companions"},
                               {"CollegeOfWinterhold", "College of Winterhold"},
                               {"ThievesGuild", "Thieves Guild"},
                               {"DarkBrotherhood", "Dark Brotherhood"},
                               {"CivilWar", "Civil War"},
                               {"Daedric", "Daedric"},
                               {"Dawnguard", "Dawnguard (DLC)"},
                               {"Dragonborn", "Dragonborn (DLC)"},
                               {"SideQuest", "Side Quest"},
                               {"Miscellaneous", "Miscellaneous"}})
                         .Default(json::array({"MainQuest"}))})
            .Factory([](const json& j) -> std::unique_ptr<Condition> {
                // Multi-select "categories" (array or comma-separated), or legacy single "category"
                if (j.contains("categories")) {
                    return std::make_unique<QuestCategoryCondition>(ReadStringList(j, "categories"));
                }
                return std::make_unique<QuestCategoryCondition>(j.value("category", ""));
            })
            .Register(r);

        // ── Scripts & variables ──────────────────────────────────────────────
        TypeBuilder("GlobalVariable", "Global Variable", "script")
            .Aliases({"GlobalVariableCondition"})
            .Icon("🌐")
            .Describe("Global variable comparison")
            .Fields({Form("globalId", "Global Variable", {"Global"}).Required(),
                     Comparison("operator", "Operator", "Equal"),
                     Float("value", "Value", 0)})
            .Positional({"globalId", "value"})
            .Compare("value")
            .Value({})
            .Factory([](const json& j) {
                return std::make_unique<GlobalVariableCondition>(
                    j.value("globalId", j.value("globalName", j.value("variableName", ""))),
                    j.value("value", j.value("targetValue", 0.0f)), ParseComparisonOperator(j));
            })
            .Register(r);

        TypeBuilder("ScriptValue", "Script Property", "script")
            .Aliases({"ScriptValueCondition"})
            .Icon("📝")
            .Describe("A Papyrus script property on the actor, or on a form such as a quest")
            .Fields({String("scriptName", "Script").Required(),
                     String("variableName", "Property").Required(),
                     Enum("valueType", "Value Type", Options({"int", "float", "bool", "string"}), "float"),
                     Comparison("operator", "Operator", "Equal"),
                     String("targetValue", "Value", ""),
                     Form("form", "Bound To", {"Quest"})
                         .Describe("Form the script is attached to, e.g. a quest; empty = the actor")})
            .Positional({"scriptName", "variableName", "valueType", "targetValue"})
            .Compare("targetValue")
            .Value({})
            .Factory([](const json& j) {
                return std::make_unique<ScriptValueCondition>(j.value("scriptName", ""), j.value("variableName", ""),
                                                              j.value("valueType", "float"),
                                                              ReadScalarAsString(j, "targetValue"),
                                                              ParseComparisonOperator(j), j.value("form", ""));
            })
            .Register(r);

        TypeBuilder("PapyrusCall", "Papyrus Function", "script")
            .Icon("📞")
            .Describe("Number returned by a Papyrus function called with the actor (asynchronous, cached)")
            .Fields({String("script", "Script").Required().Describe("Script name, e.g. SeverActionsNative"),
                     String("function", "Function").Required().Describe("Function returning Int, Float or Bool"),
                     Form("form", "Bound To", {"Quest"})
                         .Describe("Form whose script has the method; empty = a global (static) function"),
                     Bool("passActor", "Pass the actor as the argument", true),
                     Widget("args", "Extra Arguments", "json")
                         .Describe("Arguments after the actor: strings, ints, floats or bools, e.g. [\"hunger\", 0.0]"),
                     Float("interval", "Refresh (seconds)", 1.0).Range(0.05, 3600),
                     Comparison("operator", "Operator", "NotEqual"),
                     Float("value", "Value", 0)})
            .Positional({"script", "function"})
            .Compare("value")
            .Value({})
            .Factory([](const json& j) {
                PapyrusCallCondition::Call call;
                call.script = j.value("script", "");
                call.function = j.value("function", "");
                call.form = j.value("form", "");
                call.passActor = j.value("passActor", true);
                call.intervalSeconds = j.value("interval", 1.0);
                if (j.contains("args") && j["args"].is_array()) call.args = j["args"].get<std::vector<json>>();
                // Bare PapyrusCall(...) means "returned non-zero"
                const bool hasOp = j.contains("operator") || j.contains("operation");
                const bool hasValue = j.contains("value");
                auto op = hasOp ? ParseComparisonOperator(j)
                                : (hasValue ? ComparisonOperator::Equal : ComparisonOperator::NotEqual);
                return std::make_unique<PapyrusCallCondition>(std::move(call), op, j.value("value", 0.0));
            })
            .Register(r);

        TypeBuilder("StorageUtil", "StorageUtil Value", "script")
            .Icon("🗃️")
            .Describe("A PapyrusUtil StorageUtil number stored on the actor (or globally); read through Papyrus, "
                      "asynchronous and cached. Needs PapyrusUtil.")
            .Fields({String("key", "Key").Required(),
                     Enum("kind", "Kind", Options({"float", "int"}), "float"),
                     Bool("global", "Global (not per actor)", false),
                     Float("interval", "Refresh (seconds)", 1.0).Range(0.05, 3600),
                     Comparison("operator", "Operator", "NotEqual"),
                     Float("value", "Value", 0)})
            .Positional({"key"})
            .Compare("value")
            .Value({})
            .Factory([](const json& j) {
                const bool hasOp = j.contains("operator") || j.contains("operation");
                const bool hasValue = j.contains("value");
                auto op = hasOp ? ParseComparisonOperator(j)
                                : (hasValue ? ComparisonOperator::Equal : ComparisonOperator::NotEqual);
                return std::make_unique<StorageUtilCondition>(j.value("key", ""), j.value("kind", "float") == "int",
                                                              j.value("global", false), j.value("interval", 1.0), op,
                                                              j.value("value", 0.0));
            })
            .Register(r);

        TypeBuilder("Random", "Random (Seeded)", "other")
            .Aliases({"RandomCondition"})
            .Icon("🎲")
            .Describe("Deterministic chance seeded by game state - stable until the seed changes")
            .Fields({Int("chance", "Chance (%)", 50).Range(0, 100),
                     Enum("seedType", "Seed Type",
                          {{"Location", "Per Location"}, {"ActorName", "Per Actor Name"}, {"Day", "Per Day"},
                           {"Time", "Per Time Interval"}},
                          "Location"),
                     String("salt", "Salt (optional)", "").Describe("Unique identifier for independent rolls"),
                     Int("intervalHours", "Interval (hours)", 1).Range(1, 24).Describe("For the Time seed")})
            .Positional({"chance", "seedType", "salt", "intervalHours"})
            .Value({.min = 0, .max = 99, .integer = true})
            .Factory([](const json& j) {
                return std::make_unique<RandomCondition>(RandomCondition::ParseSeedType(j.value("seedType", "Location")),
                                                         j.value("chance", 50), j.value("salt", ""),
                                                         j.value("intervalHours", 1));
            })
            .Register(r);

        // ── Vanilla Creation Kit condition functions ─────────────────────────
        TypeBuilder("Game", "Vanilla Condition Function", "game")
            .Aliases({"GameCondition", "CK"})
            .Icon("⚙️")
            .Describe("Any Creation Kit condition function, e.g. GetLevel >= 20, WornHasKeyword(ArmorHeavy)")
            .Fields({String("function", "Function").Required().Describe("Condition function name, e.g. IsSneaking"),
                     String("param1", "Parameter 1").Describe("Form string, editor ID, actor value or number"),
                     String("param2", "Parameter 2"),
                     Comparison("operator", "Operator", "NotEqual"),
                     Float("value", "Value", 0),
                     Enum("runOn", "Run On",
                          {{"Subject", "Subject (the actor)"}, {"Target", "Target (the player)"}, {"Player", "Player"}},
                          "Subject")})
            .Positional({"function", "param1", "param2"})
            .Compare("value")
            .Value({})
            .Factory([](const json& j) {
                std::vector<std::string> params;
                if (j.contains("params") && j["params"].is_array()) {
                    for (const auto& p : j["params"]) params.push_back(p.is_string() ? p.get<std::string>() : p.dump());
                } else {
                    for (const char* key : {"param1", "param2"}) {
                        if (!j.contains(key)) break;
                        auto value = ReadScalarAsString(j, key);
                        if (value.empty()) break;
                        params.push_back(std::move(value));
                    }
                }
                // Bare Game(IsSneaking) means "function returned non-zero"
                const bool hasOp = j.contains("operator") || j.contains("operation");
                const bool hasValue = j.contains("value");
                auto op = hasOp ? ParseComparisonOperator(j)
                                : (hasValue ? ComparisonOperator::Equal : ComparisonOperator::NotEqual);
                return std::make_unique<GameCondition>(
                    j.value("function", ""), std::move(params), op, j.value("value", 0.0f),
                    GameCondition::ParseRunOn(j.contains("runOn") && j["runOn"].is_string() ? j["runOn"].get<std::string>() : ""));
            })
            .Register(r);

        RegisterActorConditionTypes(r);
        RegisterGearConditionTypes(r);
        RegisterWorldConditionTypes(r);

        // Cost classes (TypeInfo::cost); unlisted types keep 3. Composites (Logical, Rule,
        // Compare) compute theirs from their parts.
        const std::pair<int, std::initializer_list<const char*>> costs[] = {
            {0, {"Constant"}},
            {1, {"Time", "DayOfWeek", "Month", "DayOfMonth", "Holiday", "Weather", "InteriorExterior", "LocationType",
                 "LocationKeyword", "Location", "Swimming", "Sleeping", "Mounting", "InWater", "Combat", "IsPlayer",
                 "IsActor", "Sex", "IsPluginLoaded"}},
            {2, {"Temperature", "LoveScene", "Faction", "ActorValue", "Race", "HasSlotsEquipped", "QuestStage",
                 "QuestCategory", "GlobalVariable", "Random", "Sneaking", "Sitting", "InDialogue", "Bleedout", "WeaponDrawn",
                 "IsFollower", "HasKeyword", "HasMagicEffect", "HasPerk", "HasSpell", "WornHasKeyword", "Level", "ItemCount",
                 "HasItem", "EquippedWeaponType", "Worldspace", "Hold", "InLocation", "InCell"}},
            {3, {"Game"}},
            {4, {"ScriptValue"}},
            {5, {"PapyrusCall", "StorageUtil"}},
        };
        for (const auto& [cost, names] : costs) {
            for (const char* name : names) r.SetTypeCost(name, cost);
        }
    }

} // namespace ConditionEngine
