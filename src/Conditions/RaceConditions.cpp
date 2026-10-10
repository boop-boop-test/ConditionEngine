#include "ConditionEngine/Conditions/RaceConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Forms.h"

namespace ConditionEngine {

    RaceCondition::RaceCondition(const std::string& raceFormId)
        : raceFormId(raceFormId) {}

    bool RaceCondition::Evaluate(const EvaluationContext& context) const {
        auto* actor = context.GetActor();
        if (!actor) return false;

        // Lazy-cache the race pointer on first eval
        if (!cacheAttempted) {
            cacheAttempted = true;
            if (!raceFormId.empty()) {
                if (Forms::IsFormString(raceFormId)) {
                    auto* form = Forms::ParseFormString(raceFormId);
                    if (form) {
                        cachedRace = form->As<RE::TESRace>();
                    }
                }
                // Fallback to editor ID lookup
                if (!cachedRace) {
                    auto* form = RE::TESForm::LookupByEditorID(raceFormId);
                    if (form) {
                        cachedRace = form->As<RE::TESRace>();
                    }
                }
            }
            if (!cachedRace) {
                SKSE::log::warn("RaceCondition: Could not resolve race '{}'", raceFormId);
            }
        }

        if (!cachedRace) return false;

        bool result = actor->GetRace() == cachedRace;

        CE_TRACE(info, "RaceCondition: actor='{}' race='{}' (formId={:08X}) == target '{}' => {}",
                 actor->GetName() ? actor->GetName() : "???",
                 cachedRace->GetFullName() ? cachedRace->GetFullName() : "",
                 cachedRace->formID,
                 raceFormId,
                 result ? "TRUE" : "FALSE");

        return result;
    }

    std::string RaceCondition::GetDescription() const {
        return "Race: " + raceFormId;
    }

    nlohmann::json RaceCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "Race";
        json["raceId"] = raceFormId;
        return json;
    }

    std::unique_ptr<Condition> RaceCondition::Clone() const {
        return std::make_unique<RaceCondition>(raceFormId);
    }

    std::vector<std::string> RaceCondition::Validate() const {
        std::vector<std::string> errors;
        if (raceFormId.empty()) {
            errors.push_back("Race condition: no race specified");
        }
        return errors;
    }

} // namespace ConditionEngine
