#include "ConditionEngine/Conditions/LogicalConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"

namespace ConditionEngine {

    LogicalCondition::LogicalCondition(LogicalOperator op) : operation(op) {}

    LogicalCondition::LogicalCondition(LogicalOperator op, std::vector<std::unique_ptr<Condition>> children)
        : operation(op), children(std::move(children)) {}

    bool LogicalCondition::Evaluate(const EvaluationContext& context) const {
        switch (operation) {
            case LogicalOperator::And:
                for (const auto& child : children) {
                    if (!child->EvaluateCounted(context)) return false;
                }
                return true;
                
            case LogicalOperator::Or:
                for (const auto& child : children) {
                    if (child->EvaluateCounted(context)) return true;
                }
                return false;
                
            case LogicalOperator::Not:
                if (children.size() > 0) {
                    return !children[0]->EvaluateCounted(context);
                }
                return true;
        }
        return false;
    }

    TriResult LogicalCondition::EvaluateTri(const EvaluationContext& context) const {
        if (operation == LogicalOperator::Not) {
            if (children.empty()) return TriResult::Of(true);
            auto r = children[0]->EvaluateTriCounted(context);
            if (!r.Decided()) return r;
            return TriResult::Of(r.state == TriState::False);
        }
        // And: any false decides; Or: any true decides; otherwise an undecided child leaves
        // it undecided, Pending before Unavailable
        const TriState decisive = operation == LogicalOperator::And ? TriState::False : TriState::True;
        std::optional<TriResult> pending, unavailable;
        for (const auto& child : children) {
            auto r = child->EvaluateTriCounted(context);
            if (r.state == decisive) return r;
            if (r.state == TriState::Pending && !pending) pending = std::move(r);
            else if (r.state == TriState::Unavailable && !unavailable) unavailable = std::move(r);
        }
        if (pending) return *pending;
        if (unavailable) return *unavailable;
        return TriResult::Of(operation == LogicalOperator::And);
    }

    std::string LogicalCondition::GetDescription() const {
        std::string desc;
        switch (operation) {
            case LogicalOperator::And: desc = "AND"; break;
            case LogicalOperator::Or: desc = "OR"; break;
            case LogicalOperator::Not: desc = "NOT"; break;
        }
        return desc + " (" + std::to_string(children.size()) + " conditions)";
    }

    nlohmann::json LogicalCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "Logical";
        switch (operation) {
            case LogicalOperator::And: json["operator"] = "And"; break;
            case LogicalOperator::Or:  json["operator"] = "Or"; break;
            case LogicalOperator::Not: json["operator"] = "Not"; break;
        }
        json["children"] = nlohmann::json::array();
        
        for (const auto& child : children) {
            json["children"].push_back(child->ToJsonComplete());
        }
        
        return json;
    }

    std::unique_ptr<Condition> LogicalCondition::Clone() const {
        std::vector<std::unique_ptr<Condition>> clonedChildren;
        for (const auto& child : children) {
            clonedChildren.push_back(child->CloneComplete());
        }
        return std::make_unique<LogicalCondition>(operation, std::move(clonedChildren));
    }

    std::vector<std::string> LogicalCondition::Validate() const {
        std::vector<std::string> errors;
        if (children.empty()) {
            errors.push_back("Logical condition: no child conditions specified");
        }
        for (const auto& child : children) {
            auto childErrors = child->Validate();
            errors.insert(errors.end(), childErrors.begin(), childErrors.end());
        }
        return errors;
    }

    EventDependency LogicalCondition::GetEventDependencies() const {
        EventDependency deps = EventDependency::None;
        for (const auto& child : children) {
            deps |= child->GetEffectiveEventDependencies();
        }
        return deps;
    }

    void LogicalCondition::AddChild(std::unique_ptr<Condition> condition) {
        children.push_back(std::move(condition));
        InvalidateAvailability();
    }

    void LogicalCondition::RemoveChild(std::size_t index) {
        if (index < children.size()) {
            children.erase(children.begin() + index);
            InvalidateAvailability();
        }
    }

} // namespace ConditionEngine
