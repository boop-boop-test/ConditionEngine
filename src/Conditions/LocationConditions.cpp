#include "ConditionEngine/Conditions/LocationConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"

namespace ConditionEngine {

    // ============================================================================
    // LocationCondition (legacy) implementation
    // ============================================================================

    LocationCondition::LocationCondition(LocationType type, const std::string& customLocation)
        : locationType(type), customLocationName(customLocation) {}

    bool LocationCondition::Evaluate(const EvaluationContext& context) const {
        auto currentLocation = context.GetCurrentLocation();
        auto actor = context.GetActor();
        
        switch (locationType) {
            case LocationType::Any:
                return true;
            case LocationType::Interior:
                return actor && actor->GetParentCell() && actor->GetParentCell()->IsInteriorCell();
            case LocationType::Exterior:
                return actor && actor->GetParentCell() && !actor->GetParentCell()->IsInteriorCell();
            case LocationType::Town:
                return currentLocation && (HasLocationKeyword(currentLocation, "LocTypeTown") || 
                                          HasLocationKeyword(currentLocation, "LocTypeCity"));
            case LocationType::City:
                return currentLocation && HasLocationKeyword(currentLocation, "LocTypeCity");
            case LocationType::Dungeon:
                return currentLocation && HasLocationKeyword(currentLocation, "LocTypeDungeon");
            case LocationType::Wilderness:
                return !currentLocation || !HasAnyLocationKeywords(currentLocation);
            case LocationType::Custom:
                return currentLocation && currentLocation->GetName() && std::string(currentLocation->GetName()) == customLocationName;
        }
        return false;
    }

    bool LocationCondition::HasLocationKeyword(RE::BGSLocation* location, const std::string& keywordName) const {
        if (!location) return false;
        
        RE::BGSLocation* currentLoc = location;
        while (currentLoc) {
            std::uint32_t max = currentLoc->GetNumKeywords();
            for (std::uint32_t i = 0; i < max; i++) {
                auto keyword = currentLoc->GetKeywordAt(i);
                if (keyword && keyword.value() && keyword.value()->GetFormEditorID() == keywordName) {
                    return true;
                }
            }
            currentLoc = currentLoc->parentLoc;
        }
        return false;
    }

    bool LocationCondition::HasAnyLocationKeywords(RE::BGSLocation* location) const {
        if (!location) return false;
        
        const std::vector<std::string> significantKeywords = {
            "LocTypeCity", "LocTypeTown", "LocTypeDungeon", "LocTypeStore", 
            "LocTypeInn", "LocTypeTemple", "LocTypeGuild", "LocTypeJail",
            "LocTypeFarm", "LocTypeMilitaryCamp", "LocTypeBarracks", "LocTypeMilitaryFort"
        };
        
        for (const auto& keyword : significantKeywords) {
            if (HasLocationKeyword(location, keyword)) {
                return true;
            }
        }
        return false;
    }

    std::string LocationCondition::GetDescription() const {
        switch (locationType) {
            case LocationType::Any: return "Any Location";
            case LocationType::Interior: return "Interior";
            case LocationType::Exterior: return "Exterior";
            case LocationType::Town: return "Town";
            case LocationType::City: return "City";
            case LocationType::Dungeon: return "Dungeon";
            case LocationType::Wilderness: return "Wilderness";
            case LocationType::Custom: return "Custom: " + customLocationName;
        }
        return "Unknown Location Type";
    }

    nlohmann::json LocationCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "Location";
        json["locationType"] = static_cast<int>(locationType);
        json["customLocation"] = customLocationName;
        return json;
    }

    std::unique_ptr<Condition> LocationCondition::Clone() const {
        return std::make_unique<LocationCondition>(locationType, customLocationName);
    }

    // ============================================================================
    // InteriorExteriorCondition implementation
    // ============================================================================

    InteriorExteriorCondition::InteriorExteriorCondition(bool shouldBeInterior)
        : expectInterior(shouldBeInterior) {}

    bool InteriorExteriorCondition::Evaluate(const EvaluationContext& context) const {
        auto actor = context.GetActor();
        if (!actor) return false;
        
        auto cell = actor->GetParentCell();
        if (!cell) return false;
        
        bool isInterior = cell->IsInteriorCell();
        return isInterior == expectInterior;
    }

    std::string InteriorExteriorCondition::GetDescription() const {
        return expectInterior ? "Interior Cell" : "Exterior Cell";
    }

    nlohmann::json InteriorExteriorCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "InteriorExterior";
        json["expectInterior"] = expectInterior;
        return json;
    }

    std::unique_ptr<Condition> InteriorExteriorCondition::Clone() const {
        return std::make_unique<InteriorExteriorCondition>(expectInterior);
    }

    // ============================================================================
    // LocationTypeCondition implementation
    // ============================================================================

    LocationTypeCondition::LocationTypeCondition(LocationCategory cat)
        : category(cat) {}

    bool LocationTypeCondition::HasLocationKeyword(RE::BGSLocation* location, const std::string& keywordName) {
        if (!location) return false;
        
        RE::BGSLocation* currentLoc = location;
        while (currentLoc) {
            std::uint32_t max = currentLoc->GetNumKeywords();
            for (std::uint32_t i = 0; i < max; i++) {
                auto keyword = currentLoc->GetKeywordAt(i);
                if (keyword && keyword.value() && keyword.value()->GetFormEditorID() == keywordName) {
                    return true;
                }
            }
            currentLoc = currentLoc->parentLoc;
        }
        return false;
    }

    bool LocationTypeCondition::HasAnySignificantKeyword(RE::BGSLocation* location) {
        if (!location) return false;
        
        const std::vector<std::string> significantKeywords = {
            "LocTypeCity", "LocTypeTown", "LocTypeDungeon", "LocTypeStore", 
            "LocTypeInn", "LocTypeTemple", "LocTypeGuild", "LocTypeJail",
            "LocTypeFarm", "LocTypeLumberMill", "LocTypeMilitaryCamp", "LocTypeBarracks", "LocTypeMilitaryFort",
            "LocTypeDwelling", "LocTypePlayerHouse", "LocTypeCastle"
        };
        
        for (const auto& keyword : significantKeywords) {
            if (HasLocationKeyword(location, keyword)) {
                return true;
            }
        }
        return false;
    }

    bool LocationTypeCondition::Evaluate(const EvaluationContext& context) const {
        auto currentLocation = context.GetCurrentLocation();
        
        switch (category) {
            case LocationCategory::Town:
                return currentLocation && HasLocationKeyword(currentLocation, "LocTypeTown");
            case LocationCategory::City:
                return currentLocation && HasLocationKeyword(currentLocation, "LocTypeCity");
            case LocationCategory::Dungeon:
                return currentLocation && HasLocationKeyword(currentLocation, "LocTypeDungeon");
            case LocationCategory::Inn:
                return currentLocation && HasLocationKeyword(currentLocation, "LocTypeInn");
            case LocationCategory::Store:
                return currentLocation && HasLocationKeyword(currentLocation, "LocTypeStore");
            case LocationCategory::Temple:
                return currentLocation && HasLocationKeyword(currentLocation, "LocTypeTemple");
            case LocationCategory::Guild:
                return currentLocation && HasLocationKeyword(currentLocation, "LocTypeGuild");
            case LocationCategory::Dwelling:
                return currentLocation && HasLocationKeyword(currentLocation, "LocTypeDwelling");
            case LocationCategory::Farm:
                return currentLocation && (
                    HasLocationKeyword(currentLocation, "LocTypeFarm") ||
                    HasLocationKeyword(currentLocation, "LocTypeLumberMill"));
            case LocationCategory::Jail:
                return currentLocation && HasLocationKeyword(currentLocation, "LocTypeJail");
            case LocationCategory::PlayerHome:
                return currentLocation && HasLocationKeyword(currentLocation, "LocTypePlayerHouse");
            case LocationCategory::Castle:
                return currentLocation && HasLocationKeyword(currentLocation, "LocTypeCastle");
            case LocationCategory::Military:
                return currentLocation && (
                    HasLocationKeyword(currentLocation, "LocTypeMilitaryCamp") ||
                    HasLocationKeyword(currentLocation, "LocTypeMilitaryFort") ||
                    HasLocationKeyword(currentLocation, "LocTypeBarracks"));
            case LocationCategory::Wilderness:
                return !currentLocation || !HasAnySignificantKeyword(currentLocation);
        }
        return false;
    }

    std::string LocationTypeCondition::GetDescription() const {
        switch (category) {
            case LocationCategory::Town: return "Town";
            case LocationCategory::City: return "City";
            case LocationCategory::Dungeon: return "Dungeon";
            case LocationCategory::Inn: return "Inn";
            case LocationCategory::Store: return "Store";
            case LocationCategory::Temple: return "Temple";
            case LocationCategory::Guild: return "Guild";
            case LocationCategory::Dwelling: return "Dwelling";
            case LocationCategory::Farm: return "Farm";
            case LocationCategory::Jail: return "Jail";
            case LocationCategory::PlayerHome: return "Player Home";
            case LocationCategory::Castle: return "Castle";
            case LocationCategory::Military: return "Military";
            case LocationCategory::Wilderness: return "Wilderness";
        }
        return "Unknown Location Type";
    }

    nlohmann::json LocationTypeCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "LocationType";
        switch (category) {
            case LocationCategory::Town:       json["category"] = "Town"; break;
            case LocationCategory::City:       json["category"] = "City"; break;
            case LocationCategory::Dungeon:    json["category"] = "Dungeon"; break;
            case LocationCategory::Inn:        json["category"] = "Inn"; break;
            case LocationCategory::Store:      json["category"] = "Store"; break;
            case LocationCategory::Temple:     json["category"] = "Temple"; break;
            case LocationCategory::Guild:      json["category"] = "Guild"; break;
            case LocationCategory::Dwelling:   json["category"] = "Dwelling"; break;
            case LocationCategory::Farm:       json["category"] = "Farm"; break;
            case LocationCategory::Jail:       json["category"] = "Jail"; break;
            case LocationCategory::PlayerHome: json["category"] = "PlayerHome"; break;
            case LocationCategory::Castle:     json["category"] = "Castle"; break;
            case LocationCategory::Military:   json["category"] = "Military"; break;
            case LocationCategory::Wilderness: json["category"] = "Wilderness"; break;
        }
        return json;
    }

    std::unique_ptr<Condition> LocationTypeCondition::Clone() const {
        return std::make_unique<LocationTypeCondition>(category);
    }

    // ============================================================================
    // LocationKeywordCondition implementation
    // ============================================================================

    LocationKeywordCondition::LocationKeywordCondition(const std::string& keyword)
        : keywords({keyword}), matchAny(true) {}

    LocationKeywordCondition::LocationKeywordCondition(const std::vector<std::string>& kws, bool any)
        : keywords(kws), matchAny(any) {}

    bool LocationKeywordCondition::HasLocationKeyword(RE::BGSLocation* location, const std::string& keywordName) {
        if (!location) return false;
        
        RE::BGSLocation* currentLoc = location;
        while (currentLoc) {
            std::uint32_t max = currentLoc->GetNumKeywords();
            for (std::uint32_t i = 0; i < max; i++) {
                auto keyword = currentLoc->GetKeywordAt(i);
                if (keyword && keyword.value() && keyword.value()->GetFormEditorID() == keywordName) {
                    return true;
                }
            }
            currentLoc = currentLoc->parentLoc;
        }
        return false;
    }

    bool LocationKeywordCondition::Evaluate(const EvaluationContext& context) const {
        auto currentLocation = context.GetCurrentLocation();
        if (!currentLocation || keywords.empty()) return false;
        
        if (matchAny) {
            for (const auto& kw : keywords) {
                if (HasLocationKeyword(currentLocation, kw)) {
                    return true;
                }
            }
            return false;
        } else {
            for (const auto& kw : keywords) {
                if (!HasLocationKeyword(currentLocation, kw)) {
                    return false;
                }
            }
            return true;
        }
    }

    std::string LocationKeywordCondition::GetDescription() const {
        if (keywords.empty()) return "No keywords";
        if (keywords.size() == 1) return "Keyword: " + keywords[0];
        
        std::string desc = matchAny ? "Any of: " : "All of: ";
        for (size_t i = 0; i < keywords.size(); ++i) {
            if (i > 0) desc += ", ";
            desc += keywords[i];
        }
        return desc;
    }

    nlohmann::json LocationKeywordCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "LocationKeyword";
        json["keywords"] = keywords;
        json["matchAny"] = matchAny;
        return json;
    }

    std::unique_ptr<Condition> LocationKeywordCondition::Clone() const {
        return std::make_unique<LocationKeywordCondition>(keywords, matchAny);
    }

    std::vector<std::string> LocationKeywordCondition::Validate() const {
        std::vector<std::string> errors;
        if (keywords.empty()) {
            errors.push_back("Location Keyword condition: no keywords specified");
        }
        return errors;
    }

} // namespace ConditionEngine
