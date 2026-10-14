#include "ConditionEngine/Schema.h"

namespace ConditionEngine {

    namespace {
        using json = nlohmann::json;

        FieldSchema Make(std::string key, std::string label, FieldType type, json def = nullptr) {
            FieldSchema f;
            f.key = std::move(key);
            f.label = std::move(label);
            f.type = type;
            f.defaultValue = std::move(def);
            return f;
        }

        constexpr std::pair<FieldType, const char*> kTypeNames[] = {
            {FieldType::Int, "int"},
            {FieldType::Float, "float"},
            {FieldType::String, "string"},
            {FieldType::Bool, "bool"},
            {FieldType::Enum, "enum"},
            {FieldType::EnumList, "enumList"},
            {FieldType::Form, "form"},
            {FieldType::FormList, "formList"},
            {FieldType::StringList, "stringList"},
            {FieldType::IntList, "intList"},
            {FieldType::Comparison, "comparison"},
            {FieldType::NamedCondition, "namedCondition"},
            {FieldType::Conditions, "conditions"},
            {FieldType::Custom, "custom"},
        };

        constexpr const char* kDependencyNames[kEventDependencyBitCount] = {
            "Time",       "Date",           "Weather",  "Location", "Combat",    "Equipment",   "Faction",  "Quest",
            "ActorValue", "GlobalVariable", "Script",   "Temperature", "Swimming", "Sleeping", "Mounting", "InWater",
            "LoveScene",  "Polled",         "Host",     "Inventory",   "MagicEffect", "ActorState", "Availability",
        };

        // ${param} references are valid wherever a value is
        const json kParamRef = {{"type", "string"}, {"pattern", "^\\$\\{[^}]+\\}$"}};
    }

    namespace Field {
        FieldSchema Int(std::string key, std::string label, json def) {
            return Make(std::move(key), std::move(label), FieldType::Int, std::move(def)).Param("Int");
        }
        FieldSchema Float(std::string key, std::string label, json def) {
            return Make(std::move(key), std::move(label), FieldType::Float, std::move(def)).Param("Float");
        }
        FieldSchema String(std::string key, std::string label, json def) {
            return Make(std::move(key), std::move(label), FieldType::String, std::move(def)).Param("String");
        }
        FieldSchema Bool(std::string key, std::string label, bool def) {
            return Make(std::move(key), std::move(label), FieldType::Bool, def).Param("Boolean");
        }
        FieldSchema Enum(std::string key, std::string label, std::vector<FieldOption> options, json def) {
            auto f = Make(std::move(key), std::move(label), FieldType::Enum, std::move(def)).Param("String");
            f.options = std::move(options);
            return f;
        }
        FieldSchema EnumList(std::string key, std::string label, std::vector<FieldOption> options) {
            auto f = Make(std::move(key), std::move(label), FieldType::EnumList);
            f.options = std::move(options);
            return f;
        }
        FieldSchema Form(std::string key, std::string label, std::vector<std::string> formTypes) {
            auto f = Make(std::move(key), std::move(label), FieldType::Form).Param("String");
            f.formTypes = std::move(formTypes);
            return f;
        }
        FieldSchema FormList(std::string key, std::string label, std::vector<std::string> formTypes) {
            auto f = Make(std::move(key), std::move(label), FieldType::FormList);
            f.formTypes = std::move(formTypes);
            return f;
        }
        FieldSchema StringList(std::string key, std::string label) {
            return Make(std::move(key), std::move(label), FieldType::StringList);
        }
        FieldSchema IntList(std::string key, std::string label) {
            return Make(std::move(key), std::move(label), FieldType::IntList);
        }
        FieldSchema Comparison(std::string key, std::string label, std::string def) {
            return Make(std::move(key), std::move(label), FieldType::Comparison, std::move(def));
        }
        FieldSchema Named(std::string key, std::string label) {
            return Make(std::move(key), std::move(label), FieldType::NamedCondition).Param("String");
        }
        FieldSchema Children(std::string key, std::string label) {
            return Make(std::move(key), std::move(label), FieldType::Conditions);
        }
        FieldSchema Widget(std::string key, std::string label, std::string widget) {
            auto f = Make(std::move(key), std::move(label), FieldType::Custom);
            f.widget = std::move(widget);
            return f;
        }
        std::vector<FieldOption> Options(std::initializer_list<std::string_view> values) {
            std::vector<FieldOption> result;
            for (auto v : values) result.push_back({std::string(v), std::string(v), {}});
            return result;
        }
    }

    const char* FieldTypeName(FieldType type) {
        for (const auto& [t, name] : kTypeNames) {
            if (t == type) return name;
        }
        return "string";
    }

    std::optional<FieldType> ParseFieldType(std::string_view name) {
        for (const auto& [t, n] : kTypeNames) {
            if (name == n) return t;
        }
        return std::nullopt;
    }

    json FieldToJson(const FieldSchema& field) {
        json j{{"key", field.key}, {"label", field.label.empty() ? field.key : field.label},
               {"type", FieldTypeName(field.type)}};
        if (!field.description.empty()) j["description"] = field.description;
        if (!field.defaultValue.is_null()) j["default"] = field.defaultValue;
        if (field.required) j["required"] = true;
        if (field.min) j["min"] = *field.min;
        if (field.max) j["max"] = *field.max;
        if (!field.paramType.empty()) j["paramType"] = field.paramType;
        if (!field.formTypes.empty()) j["formTypes"] = field.formTypes;
        if (!field.widget.empty()) j["widget"] = field.widget;
        if (field.allowCustom) j["allowCustom"] = true;
        if (!field.options.empty()) {
            json options = json::array();
            for (const auto& o : field.options) {
                json opt{{"value", o.value}, {"label", o.label.empty() ? o.value : o.label}};
                if (!o.description.empty()) opt["description"] = o.description;
                options.push_back(std::move(opt));
            }
            j["options"] = std::move(options);
        }
        return j;
    }

    FieldSchema FieldFromJson(const json& j) {
        FieldSchema f;
        f.key = j.value("key", "");
        f.label = j.value("label", f.key);
        f.type = ParseFieldType(j.value("type", "string")).value_or(FieldType::String);
        f.description = j.value("description", "");
        if (j.contains("default")) f.defaultValue = j["default"];
        f.required = j.value("required", false);
        if (j.contains("min") && j["min"].is_number()) f.min = j["min"].get<double>();
        if (j.contains("max") && j["max"].is_number()) f.max = j["max"].get<double>();
        f.paramType = j.value("paramType", "");
        f.widget = j.value("widget", "");
        f.allowCustom = j.value("allowCustom", false);
        if (j.contains("formTypes") && j["formTypes"].is_array()) {
            for (const auto& t : j["formTypes"]) {
                if (t.is_string()) f.formTypes.push_back(t.get<std::string>());
            }
        }
        if (j.contains("options") && j["options"].is_array()) {
            for (const auto& o : j["options"]) {
                if (o.is_string()) {
                    f.options.push_back({o.get<std::string>(), o.get<std::string>(), {}});
                } else if (o.is_object()) {
                    f.options.push_back({o.value("value", ""), o.value("label", o.value("value", "")),
                                         o.value("description", "")});
                }
            }
        }
        return f;
    }

    json FieldToJsonSchema(const FieldSchema& field) {
        json schema;
        auto withParam = [](json base) { return json{{"anyOf", json::array({std::move(base), kParamRef})}}; };
        auto enumValues = [&field]() {
            json values = json::array();
            for (const auto& o : field.options) values.push_back(o.value);
            return values;
        };

        switch (field.type) {
            case FieldType::Int: {
                json base{{"type", "integer"}};
                if (field.min) base["minimum"] = *field.min;
                if (field.max) base["maximum"] = *field.max;
                schema = withParam(std::move(base));
                break;
            }
            case FieldType::Float: {
                json base{{"type", "number"}};
                if (field.min) base["minimum"] = *field.min;
                if (field.max) base["maximum"] = *field.max;
                schema = withParam(std::move(base));
                break;
            }
            case FieldType::Bool:
                schema = withParam(json{{"type", "boolean"}});
                break;
            case FieldType::Enum:
                schema = field.allowCustom || field.options.empty() ? json{{"type", "string"}}
                                                                    : withParam(json{{"enum", enumValues()}});
                break;
            case FieldType::EnumList:
            case FieldType::FormList:
            case FieldType::StringList:
            case FieldType::IntList: {
                json item = field.type == FieldType::IntList ? json{{"type", "integer"}} : json{{"type", "string"}};
                if (field.type == FieldType::EnumList && !field.options.empty()) item = json{{"enum", enumValues()}};
                // Parameterized lists arrive as comma-separated strings
                schema = json{{"anyOf", json::array({json{{"type", "array"}, {"items", item}}, json{{"type", "string"}}})}};
                break;
            }
            case FieldType::Comparison:
                schema = json{{"anyOf", json::array({json{{"enum", {"==", "!=", ">", ">=", "<", "<=", "Equal", "NotEqual",
                                                                    "GreaterThan", "GreaterEqual", "GreaterOrEqual",
                                                                    "LessThan", "LessEqual", "LessOrEqual"}}},
                                                     json{{"type", "integer"}, {"minimum", 0}, {"maximum", 5}}})}};
                break;
            case FieldType::Conditions:
                schema = json{{"type", "array"}, {"items", {{"$ref", "#"}}}};
                break;
            case FieldType::Custom:
                schema = json::object();  // host-defined
                break;
            default:
                schema = json{{"type", "string"}};
                break;
        }
        if (!field.description.empty()) schema["description"] = field.description;
        if (!field.label.empty()) schema["title"] = field.label;
        if (!field.defaultValue.is_null()) schema["default"] = field.defaultValue;
        return schema;
    }

    std::vector<std::string> DependencyNames(EventDependency deps) {
        std::vector<std::string> names;
        const auto bits = static_cast<std::uint32_t>(deps);
        for (std::uint32_t i = 0; i < kEventDependencyBitCount; ++i) {
            if (bits & (1u << i)) names.emplace_back(kDependencyNames[i]);
        }
        return names;
    }

} // namespace ConditionEngine
