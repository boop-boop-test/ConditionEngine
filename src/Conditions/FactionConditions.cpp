#include "ConditionEngine/Conditions/FactionConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Forms.h"

namespace ConditionEngine {

    static std::string ComparisonOpToSymbol(ComparisonOperator op) {
        switch (op) {
            case ComparisonOperator::Equal:        return "==";
            case ComparisonOperator::NotEqual:     return "!=";
            case ComparisonOperator::GreaterThan:  return ">";
            case ComparisonOperator::LessThan:     return "<";
            case ComparisonOperator::GreaterEqual: return ">=";
            case ComparisonOperator::LessEqual:    return "<=";
        }
        return ">=";
    }

    static bool CompareIntLocal(int lhs, int rhs, ComparisonOperator op) {
        switch (op) {
            case ComparisonOperator::Equal:        return lhs == rhs;
            case ComparisonOperator::NotEqual:     return lhs != rhs;
            case ComparisonOperator::GreaterThan:  return lhs > rhs;
            case ComparisonOperator::LessThan:     return lhs < rhs;
            case ComparisonOperator::GreaterEqual: return lhs >= rhs;
            case ComparisonOperator::LessEqual:    return lhs <= rhs;
        }
        return false;
    }

    FactionCondition::FactionCondition(const std::string& factionName, int rank, ComparisonOperator op)
        : factionName(factionName), rank(rank), operation(op) {}

    bool FactionCondition::Evaluate(const EvaluationContext& context) const {
        auto actor = context.GetActor();
        if (!actor) return false;

        // Lazy-cache the faction pointer on first eval
        if (!cacheAttempted) {
            cacheAttempted = true;
            if (!factionName.empty()) {
                // Try ParseFormString first (canonical "0xHEX|Plugin.esp" format)
                if (Forms::IsFormString(factionName)) {
                    auto* form = Forms::ParseFormString(factionName);
                    if (form) {
                        cachedFaction = form->As<RE::TESFaction>();
                    }
                }
                // Fallback to editor ID lookup (legacy / backward compat)
                if (!cachedFaction) {
                    auto* form = RE::TESForm::LookupByEditorID(factionName);
                    if (form) {
                        cachedFaction = form->As<RE::TESFaction>();
                    }
                }
            }
            if (!cachedFaction) {
                SKSE::log::warn("FactionCondition: Could not resolve faction '{}'", factionName);
            }
        }

        if (!cachedFaction) {
            CE_TRACE(info, "FactionCondition: cachedFaction is null for '{}', returning false", factionName);
            return false;
        }

        // GetFactionRank returns -1 when actor is not in the faction, so the
        // comparison operator alone handles all cases:
        //   rank >= 0  → is a member
        //   rank == -1 → not in faction
        //   rank >= 2  → rank 2 or higher, etc.
        int currentRank = actor->GetFactionRank(cachedFaction, false);
        bool result = CompareIntLocal(currentRank, rank, operation);

        CE_TRACE(info, "FactionCondition: actor='{}' faction='{}' (formId={:08X}, editorId='{}') "
                        "currentRank={} {} targetRank={} => {}",
                        actor->GetName() ? actor->GetName() : "???",
                        factionName,
                        cachedFaction->formID,
                        cachedFaction->GetFormEditorID() ? cachedFaction->GetFormEditorID() : "",
                        currentRank,
                        ComparisonOpToSymbol(operation),
                        rank,
                        result ? "TRUE" : "FALSE");

        return result;
    }

    std::string FactionCondition::GetDescription() const {
        std::string desc = "Faction: " + factionName;
        desc += " (rank " + ComparisonOpToSymbol(operation) + " " + std::to_string(rank) + ")";
        return desc;
    }

    nlohmann::json FactionCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "Faction";
        json["factionId"] = factionName;
        json["minRank"] = rank;
        json["operation"] = ComparisonOpToSymbol(operation);  // ParseComparisonOperator checks "operation" first
        return json;
    }

    std::unique_ptr<Condition> FactionCondition::Clone() const {
        return std::make_unique<FactionCondition>(factionName, rank, operation);
    }

    std::vector<std::string> FactionCondition::Validate() const {
        std::vector<std::string> errors;
        if (factionName.empty()) {
            errors.push_back("Faction condition: no faction specified");
        }
        return errors;
    }

} // namespace ConditionEngine
