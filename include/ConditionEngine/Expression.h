#pragma once

// Expression.h - Text syntax for condition trees.
//
// Expressions compile to the same JSON trees Condition::FromJson reads, so
// anything written as text can be stored, edited in a UI, and printed back.
//
//   expr     := or
//   or       := and  ( ("||" | "or")  and )*
//   and      := not  ( ("&&" | "and") not )*
//   not      := ("!" | "not") not | compare
//   compare  := sum [ cmp sum ]
//   sum      := term ( ("+" | "-") term )*
//   term     := unary ( ("*" | "/") unary )*
//   unary    := "-" unary | atom
//   atom     := "(" expr ")" | "true" | "false" | "@" name | "$" name | number
//             | func "(" expr ("," expr)* ")" | call
//   call     := Type [ "(" [ arg ("," arg)* ] ")" ]
//   func     := min | max | clamp | abs | lerp | piecewise | steps
//   arg      := [ field "=" ] value
//   value    := number | "string" | 'string' | ident | [ value, ... ] | ${param}
//   cmp      := "==" | "!=" | "<" | "<=" | ">" | ">="
//
// Examples
//   Time(20, 6) && !InteriorExterior(true)
//   Weather(Rainy) || Weather(Snow)
//   ActorValue(Health, usePercentage=true) < 30
//   Faction("0x00019809|Skyrim.esm") >= 1
//   @IsNight and not Combat
//   Rule("Combat Ready")
//
// Positional arguments fill the type's TypeInfo::positional fields in order;
// named arguments set fields directly. `Type(...) op literal` sets the type's
// compareField and "operator" (the classic form). Any other comparison - with
// arithmetic, $name, a numeric type on the right - becomes a Compare condition
// over two values (Value.h). `@Name` is shorthand for Rule("Name"), `$name` reads
// a named value. Where a condition is expected a bare value means "!= 0"; where a
// value is expected a condition reads 1 / 0.
// Type names are case-insensitive and accept registry aliases.
//
//   $vampire_hunger > 80
//   ActorValue(Health) * 2 < ActorValue(Magicka)
//   clamp($hunger * (1 - $light), 0, 100)

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"
#include "ConditionEngine/Value.h"

namespace ConditionEngine::Expression {

    struct ParseError {
        std::string message;
        std::size_t position = 0;  // Byte offset into the source
    };

    // Expression text → JSON condition tree. Returns nullopt and fills `error` on failure.
    std::optional<nlohmann::json> ParseToJson(std::string_view source, ParseError* error = nullptr);

    // Expression text → condition. Returns nullptr and fills `error` on failure.
    std::unique_ptr<Condition> Compile(std::string_view source, ParseError* error = nullptr);

    // Accepts either expression text or a JSON object string (leading '{').
    std::unique_ptr<Condition> CompileAny(std::string_view source, std::string* error = nullptr);

    // JSON condition tree → expression text (best effort; unknown fields become named args).
    std::string FromJson(const nlohmann::json& json);

    // Condition → expression text.
    std::string ToExpression(const Condition& condition);

    // ── Values ──────────────────────────────────────────────────────────────

    // Value expression text → value JSON (conditions become {"type": "Truth"})
    std::optional<nlohmann::json> ParseValueToJson(std::string_view source, ParseError* error = nullptr);

    // Value expression text → value
    std::unique_ptr<Value> CompileValue(std::string_view source, ParseError* error = nullptr);

    // Accepts value expression text, a JSON value node (leading '{'), or a bare
    // named-value name ("vampire_hunger", same as $vampire_hunger)
    std::unique_ptr<Value> CompileValueAny(std::string_view source, std::string* error = nullptr);

    // Value JSON → expression text
    std::string ValueFromJson(const nlohmann::json& json);

    // Value → expression text
    std::string ToExpression(const Value& value);

} // namespace ConditionEngine::Expression
