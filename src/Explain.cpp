#include "ConditionEngine/Explain.h"
#include "ConditionEngine/Availability.h"
#include "ConditionEngine/Conditions/CompareCondition.h"
#include "ConditionEngine/Conditions/LogicalConditions.h"
#include "ConditionEngine/Conditions/RuleReferenceCondition.h"
#include "ConditionEngine/Expression.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Values/ValueNodes.h"

#include <format>

namespace ConditionEngine::Explain {

    namespace {
        using json = nlohmann::json;
        constexpr int kMaxDepth = 32;

        const char* TriName(TriState state) {
            switch (state) {
                case TriState::True: return "true";
                case TriState::Pending: return "pending";
                case TriState::Unavailable: return "unavailable";
                default: return "false";
            }
        }

        json Skipped(const Condition& condition, const char* note) {
            return {{"expression", Expression::ToExpression(condition)}, {"state", "skipped"}, {"note", note}};
        }

        void SetResult(json& node, const TriResult& r) {
            node["state"] = TriName(r.state);
            if (!r.reason.empty()) node["reason"] = r.reason;
        }

        void SetValue(json& node, const ValueResult& r) {
            node["state"] = ValueStatusName(r.status);
            node["value"] = r.value;
            if (!r.reason.empty() && !r.Ok()) node["reason"] = r.reason;
            if (r.from) node["from"] = r.FromText();
        }

        json Condition_(const Condition& condition, const EvaluationContext& context, int depth);
        json Value_(const Value& value, const EvaluationContext& context, int depth);

        json Logical(const LogicalCondition& group, const EvaluationContext& context, int depth) {
            json node{{"expression", Expression::ToExpression(group)}};
            SetResult(node, group.EvaluateTri(context));
            const auto& children = group.GetChildren();
            json list = json::array();
            for (std::size_t i = 0; i < children.size(); ++i) list.push_back(nullptr);

            if (group.ConstantResult()) {
                node["note"] = "decided when compiled (a constant part settles it)";
                for (std::size_t i = 0; i < children.size(); ++i) {
                    if (children[i]) list[i] = Skipped(*children[i], "not read: the group is constant");
                }
                node["children"] = std::move(list);
                return node;
            }

            // Walk in evaluation order, stopping where evaluation stops
            const auto op = group.GetOperator();
            for (auto i : group.GetEvaluationOrder()) {
                auto child = Condition_(*children[i], context, depth + 1);
                const std::string state = child.value("state", "");
                list[i] = std::move(child);
                if ((op == LogicalOperator::And && state == "false") || (op == LogicalOperator::Or && state == "true")) {
                    break;  // decided: the rest are not read
                }
            }
            for (std::size_t i = 0; i < children.size(); ++i) {
                if (!list[i].is_null() || !children[i]) continue;
                const auto constant = children[i]->ConstantResult();
                const bool listed = std::find(group.GetEvaluationOrder().begin(), group.GetEvaluationOrder().end(), i) !=
                                    group.GetEvaluationOrder().end();
                list[i] = Skipped(*children[i], constant ? "constant: does not change the group"
                                                : listed ? "not read: a cheaper part decided the group"
                                                         : "not read: repeats an earlier part");
            }
            node["children"] = std::move(list);
            return node;
        }

        json Condition_(const Condition& condition, const EvaluationContext& context, int depth) {
            if (depth > kMaxDepth) return {{"expression", "..."}, {"state", "skipped"}, {"note", "nested too deep"}};

            if (const auto* group = dynamic_cast<const LogicalCondition*>(&condition)) return Logical(*group, context, depth);

            json node{{"expression", Expression::ToExpression(condition)}};
            SetResult(node, condition.EvaluateTri(context));

            if (const auto* compare = dynamic_cast<const CompareCondition*>(&condition)) {
                json children = json::array();
                if (compare->Left()) children.push_back(Value_(*compare->Left(), context, depth + 1));
                if (compare->Right()) children.push_back(Value_(*compare->Right(), context, depth + 1));
                node["children"] = std::move(children);
            } else if (const auto* rule = dynamic_cast<const RuleReferenceCondition*>(&condition)) {
                if (auto named = rule->Resolve()) {
                    node["children"] = json::array({Condition_(*named, context, depth + 1)});
                } else {
                    node["reason"] = "no rule or named condition '" + rule->GetKey() + "'";
                }
            } else if (condition.typeInfo && condition.typeInfo->value) {
                // A numeric condition: show the number it compared
                const auto r = condition.ReadValue(context);
                if (r.Ok()) node["value"] = r.value;
                else if (!node.contains("reason") && !r.reason.empty()) node["reason"] = r.reason;
            }
            return node;
        }

        json Value_(const Value& value, const EvaluationContext& context, int depth) {
            if (depth > kMaxDepth) return {{"expression", "..."}, {"state", "skipped"}, {"note", "nested too deep"}};
            json node{{"expression", Expression::ToExpression(value)}};
            SetValue(node, value.Read(context));

            if (const auto* math = dynamic_cast<const MathValue*>(&value)) {
                if (math->ConstantValue()) {
                    node["note"] = "computed once when compiled";
                } else {
                    json children = json::array();
                    for (const auto& arg : math->GetArgs()) {
                        if (arg) children.push_back(Value_(*arg, context, depth + 1));
                    }
                    node["children"] = std::move(children);
                }
            } else if (const auto* named = dynamic_cast<const NamedValueRef*>(&value)) {
                auto def = named->Definition();
                if (!def) return node;
                if (!def->label.empty()) node["label"] = def->label;
                if (!def->info.unit.empty()) node["unit"] = def->info.unit;
                json children = json::array();
                if (def->enabledWhen) {
                    auto gate = Condition_(*def->enabledWhen, context, depth + 1);
                    gate["role"] = "enabledWhen";
                    children.push_back(std::move(gate));
                }
                auto providers = Registry::Get().ProvidersOf(def->name);
                for (const auto& source : providers) {
                    auto child = Value_(NamedValueRef(source->name), context, depth + 1);
                    child["role"] = "source, priority " + std::format("{}", source->priority);
                    children.push_back(std::move(child));
                }
                if (def->value) {
                    auto own = Value_(*def->value, context, depth + 1);
                    own["role"] = providers.empty() ? "definition" : "own expression (last source)";
                    children.push_back(std::move(own));
                }
                if (def->pushed) node["note"] = "pushed value";
                if (!children.empty()) node["children"] = std::move(children);
            } else if (const auto* truth = dynamic_cast<const TruthValue*>(&value)) {
                if (truth->GetCondition()) node["children"] = json::array({Condition_(*truth->GetCondition(), context, depth + 1)});
            }
            return node;
        }

        void Text(const json& node, int indent, std::string& out) {
            const std::string state = node.value("state", "");
            const char* mark = state == "true" ? "[T]" : state == "false" ? "[F]" : state == "pending" ? "[P]"
                             : state == "unavailable" ? "[U]" : state == "skipped" ? "[-]" : state == "ok" ? "[ok]"
                             : state == "error" ? "[E]" : "[?]";
            out.append(static_cast<std::size_t>(indent) * 2, ' ');
            out += mark;
            out += ' ';
            if (node.contains("role")) out += node["role"].get<std::string>() + ": ";
            out += node.value("expression", "");
            if (node.contains("value") && node["value"].is_number()) {
                out += std::format(" = {}", node["value"].get<double>());
                if (node.contains("unit")) out += node["unit"].get<std::string>();
            }
            if (node.contains("from")) out += "  (from " + node["from"].get<std::string>() + ")";
            if (node.contains("reason")) out += "  - " + node["reason"].get<std::string>();
            if (node.contains("note")) out += "  [" + node["note"].get<std::string>() + "]";
            out += '\n';
            if (node.contains("children")) {
                for (const auto& child : node["children"]) Text(child, indent + 1, out);
            }
        }
    }

    json ExplainCondition(const Condition& condition, const EvaluationContext& context) {
        auto node = Condition_(condition, context, 0);
        // Evaluation suspends a whole expression whose plugins or host types are not ready
        const auto availability = condition.CheckAvailability();
        if (!availability.Ready()) {
            node["state"] = availability.state == AvailabilityState::Pending ? "pending" : "unavailable";
            node["reason"] = "suspended: " + availability.reason;
        }
        return node;
    }

    json ExplainValue(const Value& value, const EvaluationContext& context) {
        auto node = Value_(value, context, 0);
        const auto availability = value.CheckAvailability();
        if (!availability.Ready()) {
            node["state"] = availability.state == AvailabilityState::Pending ? "pending" : "unavailable";
            node["reason"] = "suspended: " + availability.reason;
        }
        return node;
    }

    std::string ToText(const json& explanation) {
        std::string out;
        Text(explanation, 0, out);
        return out;
    }

} // namespace ConditionEngine::Explain
