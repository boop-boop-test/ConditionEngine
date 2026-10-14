#include "ConditionEngine/Conditions/LogicalConditions.h"
#include "ConditionEngine/Condition.h"
#include "ConditionEngine/EvaluationContext.h"

#include <unordered_set>

namespace ConditionEngine {

    LogicalCondition::LogicalCondition(LogicalOperator op) : operation(op) { Plan(); }

    LogicalCondition::LogicalCondition(LogicalOperator op, std::vector<std::unique_ptr<Condition>> children)
        : operation(op), children(std::move(children)) {
        Plan();
    }

    void LogicalCondition::Plan() {
        order_.clear();
        constant_.reset();
        cost_ = 0;
        InvalidateAvailability();

        if (operation == LogicalOperator::Not) {
            if (children.empty() || !children[0]) {
                constant_ = true;  // Not of nothing: true, as before
                return;
            }
            if (auto c = children[0]->ConstantResult()) constant_ = !*c;
            order_.push_back(0);
            cost_ = children[0]->Cost();
            return;
        }

        const bool decisive = operation == LogicalOperator::Or;  // And: false decides, Or: true decides
        std::unordered_set<std::string> seen;
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (!children[i]) continue;
            if (auto c = children[i]->ConstantResult()) {
                if (*c == decisive) {
                    constant_ = decisive;
                    order_.clear();
                    cost_ = 0;
                    return;
                }
                continue;  // neutral: true in And, false in Or
            }
            if (!seen.insert(children[i]->ToJsonComplete().dump()).second) continue;  // duplicate
            order_.push_back(i);
        }
        if (order_.empty()) {
            constant_ = !decisive;  // And of only true, Or of only false
            return;
        }
        std::stable_sort(order_.begin(), order_.end(),
                         [this](std::size_t a, std::size_t b) { return children[a]->Cost() < children[b]->Cost(); });
        for (auto i : order_) cost_ += children[i]->Cost();
    }

    bool LogicalCondition::Evaluate(const EvaluationContext& context) const {
        if (constant_) return *constant_;
        switch (operation) {
            case LogicalOperator::And:
                for (auto i : order_) {
                    if (!children[i]->EvaluateCounted(context)) return false;
                }
                return true;

            case LogicalOperator::Or:
                for (auto i : order_) {
                    if (children[i]->EvaluateCounted(context)) return true;
                }
                return false;

            case LogicalOperator::Not:
                return !children[0]->EvaluateCounted(context);
        }
        return false;
    }

    TriResult LogicalCondition::EvaluateTri(const EvaluationContext& context) const {
        if (constant_) return TriResult::Of(*constant_);
        if (operation == LogicalOperator::Not) {
            auto r = children[0]->EvaluateTriCounted(context);
            if (!r.Decided()) return r;
            return TriResult::Of(r.state == TriState::False);
        }
        // And: any false decides; Or: any true decides; otherwise an undecided child leaves
        // it undecided, Pending before Unavailable
        const TriState decisive = operation == LogicalOperator::And ? TriState::False : TriState::True;
        std::optional<TriResult> pending, unavailable;
        for (auto i : order_) {
            auto r = children[i]->EvaluateTriCounted(context);
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
        Plan();
    }

    void LogicalCondition::RemoveChild(std::size_t index) {
        if (index < children.size()) {
            children.erase(children.begin() + index);
            Plan();
        }
    }

} // namespace ConditionEngine
