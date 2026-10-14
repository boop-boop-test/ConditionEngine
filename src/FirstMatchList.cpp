#include "ConditionEngine/FirstMatchList.h"
#include "ConditionEngine/Availability.h"
#include "ConditionEngine/Conditions/PapyrusCallCondition.h"
#include "ConditionEngine/Registry.h"

namespace ConditionEngine {

    namespace {
        using json = nlohmann::json;

        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        void Collect(const json& node, std::set<std::string>& out, std::unordered_set<std::string>& seen, int depth) {
            if (depth > 64) return;
            if (node.is_array()) {
                for (const auto& item : node) Collect(item, out, seen, depth + 1);
                return;
            }
            if (!node.is_object()) return;
            const auto type = Lower(node.value("type", std::string()));
            auto& registry = Registry::Get();

            if (type == "namedvalue") {
                const auto key = "$" + Lower(node.value("name", std::string()));
                out.insert(key);
                if (!seen.insert(key).second) return;
                const auto name = key.substr(1);
                if (auto def = registry.FindNamedValue(name)) {
                    if (def->value) Collect(def->value->ToJson(), out, seen, depth + 1);
                    if (def->enabledWhen) Collect(def->enabledWhen->ToJsonComplete(), out, seen, depth + 1);
                }
                for (const auto& source : registry.ProvidersOf(name)) {
                    Collect(json{{"type", "NamedValue"}, {"name", source->name}}, out, seen, depth + 1);
                }
                return;
            }
            if (auto call = PapyrusCallCondition::CallFromJson(node)) {
                out.insert("call:" + call->key);
                return;
            }
            if (type == "rule") {
                auto key = node.value("ruleUUID", std::string());
                if (key.empty()) key = node.value("ruleName", std::string());
                // Host-resolved rules (DressCode) are opaque; engine-owned ones are followed
                if (registry.IsEngineNamed(key) && seen.insert("@" + key).second) {
                    if (auto named = registry.FindNamed(key)) Collect(named->ToJsonComplete(), out, seen, depth + 1);
                }
                return;
            }
            for (const auto& [field, child] : node.items()) Collect(child, out, seen, depth + 1);
        }

        bool Intersects(const std::set<std::string>& a, const std::set<std::string>& b) {
            auto i = a.begin();
            auto j = b.begin();
            while (i != a.end() && j != b.end()) {
                if (*i == *j) return true;
                if (*i < *j) ++i;
                else ++j;
            }
            return false;
        }
    }

    std::set<std::string> CollectValueKeys(const json& tree) {
        std::set<std::string> out;
        std::unordered_set<std::string> seen;
        Collect(tree, out, seen, 0);
        return out;
    }

    FirstMatchList::FirstMatchList(std::vector<std::shared_ptr<const Condition>> rules)
        : rules_(std::move(rules)), states_(rules_.size()), evaluated_(rules_.size(), false) {
        RefreshDependencies();
    }

    void FirstMatchList::RefreshDependencies() {
        dependencies_.assign(rules_.size(), EventDependency::None);
        keys_.assign(rules_.size(), {});
        for (std::size_t i = 0; i < rules_.size(); ++i) {
            if (!rules_[i]) continue;
            dependencies_[i] = rules_[i]->GetEffectiveEventDependencies();
            keys_[i] = CollectValueKeys(rules_[i]->ToJsonComplete());
        }
    }

    TriResult FirstMatchList::Evaluate(std::size_t index, const EvaluationContext& context) {
        ++evaluations_;
        TriResult result;
        const auto& rule = rules_[index];
        if (!rule) {
            result = TriResult::Of(false);
        } else if (const auto availability = rule->CheckAvailability(); !availability.Ready()) {
            result = availability.state == AvailabilityState::Pending ? TriResult::Pending(availability.reason)
                                                                      : TriResult::Unavailable(availability.reason);
        } else {
            result = rule->EvaluateTriCounted(context);
        }
        states_[index] = result;
        evaluated_[index] = true;
        return result;
    }

    bool ChangeTouches(EventDependency deps, const std::set<std::string>& readKeys, EventDependency changed,
                       const std::set<std::string>* changedKeys) {
        const auto bits = static_cast<std::uint32_t>(changed);
        const auto valueBit = static_cast<std::uint32_t>(EventDependency::Value);
        const auto plain = static_cast<EventDependency>(changedKeys ? bits & ~valueBit : bits);
        if (HasDependency(deps, plain)) return true;
        if (changedKeys && (bits & valueBit)) {
            if (Intersects(readKeys, *changedKeys)) return true;
            // Things that depend on values without readable keys (host types) still wake
            return readKeys.empty() && HasDependency(deps, EventDependency::Value);
        }
        return false;
    }

    bool FirstMatchList::Touched(std::size_t index, EventDependency changed, const std::set<std::string>* keys) const {
        return ChangeTouches(dependencies_[index], keys_[index], changed, keys);
    }

    int FirstMatchList::ScanFrom(std::size_t first, const EvaluationContext& context) {
        for (std::size_t i = first; i < rules_.size(); ++i) {
            const auto r = Evaluate(i, context);
            if (r.state == TriState::True) return static_cast<int>(i);
            if (r.state == TriState::Pending) return -1;  // cannot tell which rule wins yet
        }
        return -1;
    }

    int FirstMatchList::EvaluateAll(const EvaluationContext& context) {
        const int previous = winner_;
        for (std::size_t i = 0; i < rules_.size(); ++i) {
            const auto r = Evaluate(i, context);
            if (r.state == TriState::True) {
                winner_ = static_cast<int>(i);
                return winner_;
            }
            if (r.state == TriState::Pending) {
                // Hold the previous winner below this rule if it still holds
                if (previous > static_cast<int>(i) && Evaluate(static_cast<std::size_t>(previous), context).state == TriState::True) {
                    winner_ = previous;
                } else {
                    winner_ = -1;
                }
                return winner_;
            }
        }
        winner_ = -1;
        return winner_;
    }

    int FirstMatchList::EvaluateChanged(const EvaluationContext& context, EventDependency changed,
                                        const std::set<std::string>* keys) {
        const std::size_t limit = winner_ < 0 ? rules_.size() : static_cast<std::size_t>(winner_);
        // 1. Rules above the winner that the change touches
        for (std::size_t i = 0; i < limit; ++i) {
            if (!Touched(i, changed, keys)) continue;
            const auto r = Evaluate(i, context);
            if (r.state == TriState::True) {
                winner_ = static_cast<int>(i);
                return winner_;
            }
            if (r.state == TriState::Pending) return winner_;  // hold
        }
        if (winner_ < 0) return winner_;

        // 2. The winner itself
        const auto w = static_cast<std::size_t>(winner_);
        if (!Touched(w, changed, keys)) return winner_;
        const auto r = Evaluate(w, context);
        if (r.state == TriState::True || r.state == TriState::Pending) return winner_;

        // 3. It turned false: the next true rule below it
        winner_ = ScanFrom(w + 1, context);
        return winner_;
    }

    EventDependency FirstMatchList::ListenedDependencies() const {
        const std::size_t last = winner_ < 0 ? rules_.size() : static_cast<std::size_t>(winner_) + 1;
        EventDependency deps = EventDependency::None;
        for (std::size_t i = 0; i < last && i < dependencies_.size(); ++i) deps |= dependencies_[i];
        return deps;
    }

    bool FirstMatchList::ListensToKeys(const std::set<std::string>& keys) const {
        const std::size_t last = winner_ < 0 ? rules_.size() : static_cast<std::size_t>(winner_) + 1;
        for (std::size_t i = 0; i < last && i < keys_.size(); ++i) {
            if (Intersects(keys_[i], keys)) return true;
            if (keys_[i].empty() && HasDependency(dependencies_[i], EventDependency::Value)) return true;
        }
        return false;
    }

} // namespace ConditionEngine
