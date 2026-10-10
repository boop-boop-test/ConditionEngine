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
                    if (!child->Evaluate(context)) return false;
                }
                return true;
                
            case LogicalOperator::Or:
                for (const auto& child : children) {
                    if (child->Evaluate(context)) return true;
                }
                return false;
                
            case LogicalOperator::Not:
                if (children.size() > 0) {
                    return !children[0]->Evaluate(context);
                }
                return true;
        }
        return false;
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
    }

    void LogicalCondition::RemoveChild(std::size_t index) {
        if (index < children.size()) {
            children.erase(children.begin() + index);
        }
    }

} // namespace ConditionEngine
