#pragma once

// ValueNodes.h - Built-in value nodes (see Value.h for the JSON forms).

#include "ConditionEngine/Value.h"

#include <mutex>

namespace ConditionEngine {

    struct TypeInfo;
    struct NamedValueDef;

    // 3.5
    class LiteralValue : public Value {
    public:
        explicit LiteralValue(double value) : value_(value) { statsSlot = 1; }  // Stats::kLiteral
        ValueResult Read(const EvaluationContext&) const override { return ValueResult::Of(value_); }
        EventDependency GetEventDependencies() const override { return EventDependency::None; }
        bool ReadsActor() const override { return false; }
        std::optional<double> ConstantValue() const override { return value_; }
        int Cost() const override { return 0; }
        nlohmann::json ToJson() const override;
        double Get() const { return value_; }

    private:
        double value_;
    };

    // + - * / neg min max clamp abs lerp
    class MathValue : public Value {
    public:
        enum class Op { Add, Sub, Mul, Div, Neg, Min, Max, Clamp, Abs, Lerp };

        MathValue(Op op, std::vector<std::shared_ptr<const Value>> args);

        ValueResult Read(const EvaluationContext& context) const override;
        EventDependency GetEventDependencies() const override;
        nlohmann::json ToJson() const override;
        std::vector<std::string> Validate() const override;
        bool ReadsActor() const override;
        std::optional<double> ConstantValue() const override { return folded_; }
        int Cost() const override;

        static std::optional<Op> ParseOp(std::string_view name);  // "+", "neg", "clamp", ...
        static const char* OpName(Op op);
        // Exact argument count, or nullopt for min / max (two or more)
        static std::optional<std::size_t> Arity(Op op);

    private:
        double Apply(const double* v, std::size_t count) const;

        Op op_;
        std::vector<std::shared_ptr<const Value>> args_;
        std::optional<double> folded_;  // all arguments constant: computed once
    };

    // $name - bound to its definition until the availability revision moves (every
    // registration, removal or preset reload does), so redefinitions take effect at once
    class NamedValueRef : public Value {
    public:
        explicit NamedValueRef(std::string name);

        ValueResult Read(const EvaluationContext& context) const override;
        EventDependency GetEventDependencies() const override;
        nlohmann::json ToJson() const override { return {{"type", "NamedValue"}, {"name", name_}}; }
        std::vector<std::string> Validate() const override;
        bool ReadsActor() const override;
        int Cost() const override;
        const std::string& GetName() const { return name_; }

    private:
        // The definition and its sources (highest priority first), one object per revision
        struct Binding {
            std::shared_ptr<const NamedValueDef> def;
            std::vector<std::shared_ptr<const NamedValueDef>> providers;
        };
        std::shared_ptr<const Binding> Bind() const;

        std::string name_;
        std::string key_;  // lowercase
        mutable std::mutex bindMutex_;
        mutable std::uint64_t boundRevision_ = ~0ull;
        mutable std::shared_ptr<const Binding> bound_;
    };

    // Any condition read as 1 (true) / 0 (false)
    class TruthValue : public Value {
    public:
        explicit TruthValue(std::shared_ptr<const Condition> condition) : condition_(std::move(condition)) {
            statsSlot = 4;  // Stats::kTruth
        }

        ValueResult Read(const EvaluationContext& context) const override;
        EventDependency GetEventDependencies() const override;
        nlohmann::json ToJson() const override;
        std::vector<std::string> Validate() const override;
        std::optional<double> ConstantValue() const override {
            auto c = condition_ ? condition_->ConstantResult() : std::nullopt;
            return c ? std::optional<double>(*c ? 1.0 : 0.0) : std::nullopt;
        }
        int Cost() const override { return condition_ ? condition_->Cost() : 0; }

    private:
        std::shared_ptr<const Condition> condition_;
    };

    // A numeric condition type read as its number (Condition::ReadValue)
    class TypeValue : public Value {
    public:
        TypeValue(const TypeInfo* type, std::shared_ptr<const Condition> condition);

        ValueResult Read(const EvaluationContext& context) const override;
        EventDependency GetEventDependencies() const override;
        nlohmann::json ToJson() const override;  // the condition's JSON without its comparison
        std::vector<std::string> Validate() const override;
        int Cost() const override { return condition_ ? condition_->Cost() : 0; }

    private:
        const TypeInfo* type_;
        std::shared_ptr<const Condition> condition_;
    };

} // namespace ConditionEngine
