#include "ConditionEngine/Conditions/WorldConditions.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Forms.h"
#include "ConditionEngine/Registry.h"

namespace ConditionEngine {

    namespace {
        using json = nlohmann::json;

        bool IEquals(std::string_view a, std::string_view b) {
            return a.size() == b.size() && _strnicmp(a.data(), b.data(), a.size()) == 0;
        }

        const char* KindName(PlaceCondition::Kind kind) {
            switch (kind) {
                case PlaceCondition::Kind::Worldspace: return "Worldspace";
                case PlaceCondition::Kind::Hold: return "Hold";
                case PlaceCondition::Kind::Location: return "InLocation";
                default: return "InCell";
            }
        }

        const char* KindField(PlaceCondition::Kind kind) {
            switch (kind) {
                case PlaceCondition::Kind::Worldspace: return "worldspaces";
                case PlaceCondition::Kind::Hold: return "holds";
                case PlaceCondition::Kind::Location: return "locations";
                default: return "cells";
            }
        }

        // Tamriel-wide holidays observed in Skyrim's calendar (month 0 = Morning Star)
        struct HolidayDate {
            std::string_view name;
            int month;
            int day;
        };
        constexpr HolidayDate kHolidays[] = {
            {"NewLifeFestival", 0, 1},     {"SouthWindsPrayer", 0, 15},   {"HeartsDay", 1, 16},
            {"FirstPlanting", 2, 7},       {"SecondPlanting", 4, 7},      {"MidYearCelebration", 5, 16},
            {"MerchantsFestival", 6, 10},  {"SunsRest", 6, 20},           {"HarvestsEnd", 7, 27},
            {"TalesAndTallows", 8, 3},     {"WitchesFestival", 9, 13},    {"EmperorsDay", 9, 30},
            {"WarriorsFestival", 10, 20},  {"NorthWindsPrayer", 11, 15},  {"Saturalia", 11, 25},
            {"OldLifeFestival", 11, 31},
        };

        // "Harvest's End" / "harvests end" / "HarvestsEnd" all compare equal
        std::string Normalize(std::string_view text) {
            std::string out;
            for (char c : text) {
                if (std::isalnum(static_cast<unsigned char>(c))) {
                    out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
            }
            return out;
        }
    }

    // ============================================================================
    // PlaceCondition
    // ============================================================================

    PlaceCondition::PlaceCondition(Kind kind, std::vector<std::string> places, bool includeParents)
        : kind_(kind), places_(std::move(places)), includeParents_(includeParents) {}

    const std::vector<PlaceCondition::Target>& PlaceCondition::Resolve() const {
        std::call_once(resolveOnce_, [this]() {
            for (const auto& place : places_) {
                Target target{place, nullptr};
                // Only form strings / raw IDs resolve to forms; plain words are matched by name
                if (Forms::IsFormString(place) || place.rfind("0x", 0) == 0) target.form = Forms::LookupAny(place);
                targets_.push_back(std::move(target));
            }
        });
        return targets_;
    }

    bool PlaceCondition::Matches(RE::TESForm* form) const {
        if (!form) return false;
        const char* editorId = form->GetFormEditorID();
        const char* name = form->GetName();
        for (const auto& target : Resolve()) {
            if (target.form) {
                if (target.form == form) return true;
                continue;
            }
            if (editorId && *editorId && IEquals(editorId, target.text)) return true;
            if (name && *name) {
                if (IEquals(name, target.text)) return true;
                // Hold(Whiterun) matches "Whiterun Hold"
                if (kind_ == Kind::Hold && IEquals(name, target.text + " Hold")) return true;
            }
        }
        return false;
    }

    bool PlaceCondition::Evaluate(const EvaluationContext& context) const {
        auto* actor = context.GetActor();
        if (!actor || places_.empty()) return false;

        switch (kind_) {
            case Kind::Worldspace:
                return Matches(actor->GetWorldspace());
            case Kind::Cell:
                return Matches(actor->GetParentCell());
            case Kind::Hold:
                for (auto* loc = context.GetCurrentLocation(); loc; loc = loc->parentLoc) {
                    if (loc->HasKeywordString("LocTypeHold")) return Matches(loc);
                }
                return false;
            case Kind::Location:
                for (auto* loc = context.GetCurrentLocation(); loc; loc = includeParents_ ? loc->parentLoc : nullptr) {
                    if (Matches(loc)) return true;
                }
                return false;
        }
        return false;
    }

    std::string PlaceCondition::GetDescription() const {
        std::string list;
        for (const auto& p : places_) {
            if (!list.empty()) list += " or ";
            list += p;
        }
        return std::string(KindName(kind_)) + " is " + list;
    }

    nlohmann::json PlaceCondition::ToJson() const {
        json j{{"type", KindName(kind_)}};
        if (places_.size() == 1) {
            j[KindField(kind_)] = places_.front();
        } else {
            j[KindField(kind_)] = places_;
        }
        if (kind_ == Kind::Location && !includeParents_) j["includeParents"] = false;
        return j;
    }

    std::unique_ptr<Condition> PlaceCondition::Clone() const {
        return std::make_unique<PlaceCondition>(kind_, places_, includeParents_);
    }

    std::vector<std::string> PlaceCondition::Validate() const {
        if (places_.empty()) return {std::string(KindName(kind_)) + " condition: nothing specified"};
        std::vector<std::string> errors;
        for (const auto& t : Resolve()) {
            if ((Forms::IsFormString(t.text) || t.text.rfind("0x", 0) == 0) && !t.form) {
                errors.push_back(std::string(KindName(kind_)) + " condition: form '" + t.text + "' not found");
            }
        }
        return errors;
    }

    // ============================================================================
    // PluginLoadedCondition
    // ============================================================================

    PluginLoadedCondition::PluginLoadedCondition(std::string plugin) : plugin_(std::move(plugin)) {}

    bool PluginLoadedCondition::Evaluate(const EvaluationContext&) const {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data || plugin_.empty()) return false;
        return data->LookupLoadedModByName(plugin_) || data->LookupLoadedLightModByName(plugin_);
    }

    // ============================================================================
    // HolidayCondition
    // ============================================================================

    HolidayCondition::HolidayCondition(std::vector<std::string> holidays) : holidays_(std::move(holidays)) {}

    std::string_view HolidayCondition::HolidayOn(int month, int day) {
        for (const auto& h : kHolidays) {
            if (h.month == month && h.day == day) return h.name;
        }
        return {};
    }

    bool HolidayCondition::Evaluate(const EvaluationContext&) const {
        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar) return false;
        const auto today = HolidayOn(static_cast<int>(calendar->GetMonth()), static_cast<int>(calendar->GetDay()));
        if (today.empty()) return false;
        const auto todayKey = Normalize(today);
        for (const auto& h : holidays_) {
            if (IEquals(h, "Any") || Normalize(h) == todayKey) return true;
        }
        return false;
    }

    std::string HolidayCondition::GetDescription() const {
        std::string list;
        for (const auto& h : holidays_) {
            if (!list.empty()) list += " or ";
            list += h;
        }
        return "Today is " + list;
    }

    nlohmann::json HolidayCondition::ToJson() const {
        json j{{"type", "Holiday"}};
        if (holidays_.size() == 1) {
            j["holidays"] = holidays_.front();
        } else {
            j["holidays"] = holidays_;
        }
        return j;
    }

    std::vector<std::string> HolidayCondition::Validate() const {
        if (holidays_.empty()) return {"Holiday condition: no holiday specified"};
        std::vector<std::string> errors;
        for (const auto& h : holidays_) {
            if (IEquals(h, "Any") || h.find("${") != std::string::npos) continue;
            const auto key = Normalize(h);
            const bool known = std::any_of(std::begin(kHolidays), std::end(kHolidays),
                                           [&](const HolidayDate& d) { return Normalize(d.name) == key; });
            if (!known) errors.push_back("Holiday condition: unknown holiday '" + h + "'");
        }
        return errors;
    }

    // ============================================================================
    // Registration
    // ============================================================================

    void RegisterWorldConditionTypes(Registry& r) {
        auto place = [&r](PlaceCondition::Kind kind, std::vector<std::string> aliases, std::string description) {
            TypeInfo info;
            info.name = KindName(kind);
            info.aliases = std::move(aliases);
            const std::string field = KindField(kind);
            info.positional = {field};
            if (kind == PlaceCondition::Kind::Location) info.positional.push_back("includeParents");
            info.description = std::move(description);
            info.factory = [kind, field](const json& j) {
                return std::make_unique<PlaceCondition>(kind, ReadStringList(j, field), j.value("includeParents", true));
            };
            r.RegisterType(std::move(info));
        };

        place(PlaceCondition::Kind::Worldspace, {"InWorldspace"}, "Actor's worldspace (exteriors only)");
        place(PlaceCondition::Kind::Hold, {"InHold"}, "Hold containing the actor's location (e.g. Whiterun)");
        place(PlaceCondition::Kind::Location, {"AtLocation"},
              "Actor's location, or one of its parents unless includeParents=false");
        place(PlaceCondition::Kind::Cell, {"Cell"}, "Actor's parent cell (editor ID or name)");

        TypeInfo plugin;
        plugin.name = "IsPluginLoaded";
        plugin.aliases = {"PluginLoaded", "HasPlugin"};
        plugin.positional = {"plugin"};
        plugin.description = "A plugin (.esp/.esm/.esl) is active in the load order";
        plugin.factory = [](const json& j) {
            return std::make_unique<PluginLoadedCondition>(
                j.contains("plugin") && j["plugin"].is_string() ? j["plugin"].get<std::string>() : "");
        };
        r.RegisterType(std::move(plugin));

        TypeInfo holiday;
        holiday.name = "Holiday";
        holiday.positional = {"holidays"};
        holiday.description = "Today is a Tamriel holiday (e.g. Saturalia, NewLifeFestival) or Any";
        holiday.factory = [](const json& j) { return std::make_unique<HolidayCondition>(ReadStringList(j, "holidays")); };
        r.RegisterType(std::move(holiday));
    }

} // namespace ConditionEngine
