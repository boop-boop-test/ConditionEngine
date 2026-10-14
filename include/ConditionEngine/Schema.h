#pragma once

// Schema.h - Self-description of condition types.
//
// Every registered type declares its fields (key, value type, label, default,
// allowed values, form types...) and a category. The registry publishes these as
// a catalog (Registry::GetCatalog) and as a JSON Schema (Registry::GetJsonSchema),
// so UIs and other mods can build editors and validate condition JSON without
// knowing how any particular condition works.
//
// Catalog field JSON:
//   { "key": "minRank", "label": "Rank", "type": "int", "default": 0, "min": 0,
//     "description": "...", "required": false, "paramType": "Int",
//     "options": [{"value": "City", "label": "City"}],   // enum / enumList
//     "allowCustom": false,                               // enum: free text allowed
//     "formTypes": ["Faction"],                           // form / formList
//     "widget": "dial" }                                  // custom: host-specific editor

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

namespace ConditionEngine {

    enum class FieldType {
        Int,
        Float,
        String,
        Bool,
        Enum,            // one of `options` (or free text with allowCustom)
        EnumList,        // several of `options`
        Form,            // form string ("0x123|Plugin.esp"), editor ID or name
        FormList,
        StringList,
        IntList,
        Comparison,      // ==, !=, >, >=, <, <=
        NamedCondition,  // reference to a named condition / rule
        Conditions,      // nested child conditions (Logical)
        Custom,          // host-specific editor named by `widget`; value is opaque JSON
        Value,           // value expression node (see Value.h): number, ${param} or {"type": ...}
    };

    struct FieldOption {
        std::string value;
        std::string label;
        std::string description;
    };

    struct FieldSchema {
        std::string key;
        std::string label;
        FieldType type = FieldType::String;
        std::string description;
        nlohmann::json defaultValue;          // null = no default
        std::vector<FieldOption> options;     // Enum / EnumList
        bool allowCustom = false;             // Enum: values outside `options` are valid
        std::vector<std::string> formTypes;   // Form / FormList: record types ("Keyword", "Perk")
        std::optional<double> min;
        std::optional<double> max;
        bool required = false;
        std::string paramType;                // module-parameter compatibility: Int, Float, String, Boolean
        std::string widget;                   // Custom: editor id ("dial", "slots")

        // Fluent modifiers (return copies so they chain on builder temporaries)
        FieldSchema Describe(std::string text) const { auto c = *this; c.description = std::move(text); return c; }
        FieldSchema Required() const { auto c = *this; c.required = true; return c; }
        FieldSchema Param(std::string type) const { auto c = *this; c.paramType = std::move(type); return c; }
        FieldSchema Range(double lo, double hi) const { auto c = *this; c.min = lo; c.max = hi; return c; }
        FieldSchema Default(nlohmann::json value) const { auto c = *this; c.defaultValue = std::move(value); return c; }
        FieldSchema Custom() const { auto c = *this; c.allowCustom = true; return c; }
    };

    // A picker group. Categories form a tree through `parent` (empty = top level);
    // ids are flat strings, dotted ids ("ntjh.face") are only a convention.
    struct CategoryInfo {
        std::string id;
        std::string label;
        int order = 100;
        std::string parent;
        std::string icon;
        std::string description;
        std::string source;  // owner; built-in categories are "ConditionEngine"

        // {"id", "label", "order", "parent", "icon", "description", "source"}
        nlohmann::json ToJson() const;
    };

    // Field builders
    namespace Field {
        FieldSchema Int(std::string key, std::string label, nlohmann::json def = nullptr);
        FieldSchema Float(std::string key, std::string label, nlohmann::json def = nullptr);
        FieldSchema String(std::string key, std::string label, nlohmann::json def = nullptr);
        FieldSchema Bool(std::string key, std::string label, bool def);
        FieldSchema Enum(std::string key, std::string label, std::vector<FieldOption> options, nlohmann::json def = nullptr);
        FieldSchema EnumList(std::string key, std::string label, std::vector<FieldOption> options);
        FieldSchema Form(std::string key, std::string label, std::vector<std::string> formTypes);
        FieldSchema FormList(std::string key, std::string label, std::vector<std::string> formTypes);
        FieldSchema StringList(std::string key, std::string label);
        FieldSchema IntList(std::string key, std::string label);
        FieldSchema Comparison(std::string key = "operator", std::string label = "Comparison", std::string def = ">=");
        FieldSchema Named(std::string key, std::string label);
        FieldSchema Children(std::string key, std::string label);
        FieldSchema ValueExpr(std::string key, std::string label);
        FieldSchema Widget(std::string key, std::string label, std::string widget);

        // Options from plain values (label = value)
        std::vector<FieldOption> Options(std::initializer_list<std::string_view> values);
    }

    const char* FieldTypeName(FieldType type);
    std::optional<FieldType> ParseFieldType(std::string_view name);

    nlohmann::json FieldToJson(const FieldSchema& field);
    FieldSchema FieldFromJson(const nlohmann::json& json);

    // JSON Schema (draft-07) fragment validating one field's value
    nlohmann::json FieldToJsonSchema(const FieldSchema& field);

    // Names of the dependency bits ("Time", "Location", ...)
    std::vector<std::string> DependencyNames(EventDependency deps);

} // namespace ConditionEngine
