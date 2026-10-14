#pragma once

// ValueNodes.h - Built-in value nodes (see Value.h for the JSON forms).

#include "ConditionEngine/Value.h"

namespace ConditionEngine {

    struct TypeInfo;

    // 3.5
    class LiteralValue : public Value {
    public:
        explicit LiteralValue(double value) : value_(value) {}
        ValueResult Read(const EvaluationContext&) const override { return ValueResult::Of(value_); }
        EventDependency GetEventDependencies() const override { return EventDependency::None; }
        bool ReadsActor() const override { return false; }
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

        static std::optional<Op> ParseOp(std::string_view name);  // "+", "neg", "clamp", ...
        static const char* OpName(Op op);
        // Exact argument count, or nullopt for min / max (two or more)
        static std::optional<std::size_t> Arity(Op op);

    private:
        Op op_;
        std::vector<std::shared_ptr<const Value>> args_;
    };

    // $name - resolved by name on every read, so redefinitions take effect immediately
    class NamedValueRef : public Value {
    public:
        explicit NamedValueRef(std::string name) : name_(std::move(name)) {}

        ValueResult Read(const EvaluationContext& context) const override;
        EventDependency GetEventDependencies() const override;
        nlohmann::json ToJson() const override { return {{"type", "NamedValue"}, {"name", name_}}; }
        std::vector<std::string> Validate() const override;
        bool ReadsActor() const override;
        const std::string& GetName() const { return name_; }

    private:
        std::string name_;
    };

    // Any condition read as 1 (true) / 0 (false)
    class TruthValue : public Value {
    public:
        explicit TruthValue(std::shared_ptr<const Condition> condition) : condition_(std::move(condition)) {}

        ValueResult Read(const EvaluationContext& context) const override;
        EventDependency GetEventDependencies() const override;
        nlohmann::json ToJson() const override;
        std::vector<std::string> Validate() const override;

    private:
        std::shared_ptr<const Condition> condition_;
    };

    // A numeric condition type read as its number (Condition::ReadValue)
    class TypeValue : public Value {
    public:
        TypeValue(const TypeInfo* type, std::shared_ptr<const Condition> condition)
            : type_(type), condition_(std::move(condition)) {}

        ValueResult Read(const EvaluationContext& context) const override;
        EventDependency GetEventDependencies() const override;
        nlohmann::json ToJson() const override;  // the condition's JSON without its comparison
        std::vector<std::string> Validate() const override;

    private:
        const TypeInfo* type_;
        std::shared_ptr<const Condition> condition_;
    };

} // namespace ConditionEngine
