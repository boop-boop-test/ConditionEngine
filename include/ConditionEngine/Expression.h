#pragma once

// Expression.h - Text syntax for condition trees.
//
// Expressions compile to the same JSON trees Condition::FromJson reads, so
// anything written as text can be stored, edited in a UI, and printed back.
//
//   expr     := or
//   or       := and  ( ("||" | "or")  and )*
//   and      := not  ( ("&&" | "and") not )*
//   not      := ("!" | "not") not | primary
//   primary  := "(" expr ")" | "true" | "false" | "@" name | call
//   call     := Type [ "(" [ arg ("," arg)* ] ")" ] [ cmp value ]
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
// named arguments set fields directly. A trailing comparison sets the type's
// compareField and "operator". `@Name` is shorthand for Rule("Name").
// Type names are case-insensitive and accept registry aliases.

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

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

} // namespace ConditionEngine::Expression
