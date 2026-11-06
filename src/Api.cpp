/// @file Api.cpp
/// @brief extern "C" API exported by ConditionEngine.dll (see ConditionEngineAPI.h).

#include "ConditionEngine/ConditionEngineAPI.h"
#include "ConditionEngine/AllConditions.h"
#include "ConditionEngine/Availability.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Explain.h"
#include "ConditionEngine/Expression.h"
#include "ConditionEngine/People.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Stats.h"
#include "ConditionEngine/Value.h"
#include "ConditionEngine/Values/PushedValues.h"
#include "ConditionEngine/Watcher.h"

#include <cstddef>
#include <intrin.h>
#include <mutex>
#include <sstream>

namespace {
    using namespace ConditionEngine;
    using json = nlohmann::json;

    std::mutex g_handleMutex;
    std::unordered_map<CE_Handle, std::shared_ptr<const Condition>> g_handles;
    CE_Handle g_nextHandle = 1;

    CE_Handle Store(std::shared_ptr<const Condition> condition) {
        std::lock_guard lock(g_handleMutex);
        CE_Handle handle = g_nextHandle++;
        if (g_nextHandle == 0) g_nextHandle = 1;
        g_handles.emplace(handle, std::move(condition));
        return handle;
    }

    std::shared_ptr<const Condition> Lookup(CE_Handle handle) {
        std::lock_guard lock(g_handleMutex);
        auto it = g_handles.find(handle);
        return it != g_handles.end() ? it->second : nullptr;
    }

    RE::Actor* ActorOrPlayer(void* actor) {
        return actor ? static_cast<RE::Actor*>(actor) : RE::PlayerCharacter::GetSingleton();
    }

    // Writes `text` into a caller buffer; returns the size needed including the null.
    uint32_t WriteString(const std::string& text, char* buf, uint32_t bufSize) {
        const auto needed = static_cast<uint32_t>(text.size() + 1);
        if (buf && bufSize > 0) {
            const auto count = std::min<std::size_t>(text.size(), bufSize - 1);
            std::memcpy(buf, text.data(), count);
            buf[count] = '\0';
        }
        return needed;
    }


    bool Evaluate(const Condition& condition, RE::Actor* actor, void* userData = nullptr) {
        EvaluationContext context(actor);
        context.userData = userData;
        return condition.EvaluateAvailable(context);
    }

    // ── Value handles ────────────────────────────────────────────────────────

    std::mutex g_valueMutex;
    std::unordered_map<CE_ValueHandle, std::shared_ptr<const Value>> g_values;
    CE_ValueHandle g_nextValue = 1;

    CE_ValueHandle StoreValue(std::shared_ptr<const Value> value) {
        std::lock_guard lock(g_valueMutex);
        CE_ValueHandle handle = g_nextValue++;
        if (g_nextValue == 0) g_nextValue = 1;
        g_values.emplace(handle, std::move(value));
        return handle;
    }

    std::shared_ptr<const Value> LookupValue(CE_ValueHandle handle) {
        std::lock_guard lock(g_valueMutex);
        auto it = g_values.find(handle);
        return it != g_values.end() ? it->second : nullptr;
    }

    // Writes the fields that fit in the caller's struct
    void WriteSample(const ValueSample& sample, CE_ValueSample* out) {
        if (!out) return;
        const auto size = out->structSize;
        auto fits = [size](std::size_t end) { return size >= end; };
        if (fits(offsetof(CE_ValueSample, status) + sizeof(int32_t))) out->status = static_cast<int32_t>(sample.status);
        if (fits(offsetof(CE_ValueSample, value) + sizeof(double))) out->value = sample.value;
        if (fits(offsetof(CE_ValueSample, ageMs) + sizeof(uint32_t))) out->ageMs = sample.AgeMs();
        if (fits(offsetof(CE_ValueSample, availabilityRevision) + sizeof(uint64_t))) {
            out->availabilityRevision = sample.availabilityRevision;
        }
    }

    // CE_WatchOptions -> WatchOptions; fields are read only when the caller's struct holds them
    WatchOptions ReadWatchOptions(const CE_WatchOptions* options) {
        WatchOptions watchOptions;
        auto has = [options](std::size_t end) { return options && options->structSize >= end; };
        if (has(offsetof(CE_WatchOptions, fireImmediately) + sizeof(int32_t))) {
            watchOptions.fireImmediately = options->fireImmediately != 0;
        }
        if (has(offsetof(CE_WatchOptions, pollIntervalMs) + sizeof(uint32_t))) {
            watchOptions.pollInterval = options->pollIntervalMs == CE_POLL_NEVER ? WatchOptions::kNever
                                                                                 : std::chrono::milliseconds(options->pollIntervalMs);
        }
        if (has(offsetof(CE_WatchOptions, extraDependencies) + sizeof(uint32_t))) {
            watchOptions.extraDependencies = static_cast<EventDependency>(options->extraDependencies);
        }
        return watchOptions;
    }

    CE_ValueSample ToCSample(const ValueSample& sample) {
        CE_ValueSample out{};
        out.structSize = sizeof(CE_ValueSample);
        WriteSample(sample, &out);
        return out;
    }

    // File name (no extension) of the module containing `address`: the plugin that called us
    std::string ModuleNameAt(void* address) {
        HMODULE module = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                static_cast<LPCSTR>(address), &module) ||
            !module) {
            return "plugin";
        }
        char path[MAX_PATH] = {};
        if (!GetModuleFileNameA(module, path, MAX_PATH)) return "plugin";
        return std::filesystem::path(path).stem().string();
    }

    // Builds a TypeBuilder from a RegisterTypeEx schema. Returns nullopt and writes the error.
    // `caller` is the registering module, used as the source when the schema names none.
    std::optional<TypeBuilder> BuilderFromSchema(const char* schemaJson, json& schema, CE_Result& result,
                                                 char* errorBuf, uint32_t errorBufSize, const std::string& caller) {
        try {
            schema = json::parse(schemaJson);
        } catch (const std::exception& e) {
            WriteString(std::string("invalid schema JSON: ") + e.what(), errorBuf, errorBufSize);
            result = CE_ERR_PARSE;
            return std::nullopt;
        }
        const std::string name = schema.value("name", "");
        if (name.empty()) {
            WriteString("schema needs a \"name\"", errorBuf, errorBufSize);
            result = CE_ERR_INVALID_ARG;
            return std::nullopt;
        }

        TypeBuilder builder(name, schema.value("label", name), schema.value("category", "other"));
        std::string source = schema.value("source", "");
        if (source.empty()) {
            source = caller;
            SKSE::log::warn("ConditionEngine: type '{}' registered without a \"source\"; using the calling module '{}'",
                            name, source);
        }
        builder.Describe(schema.value("description", "")).Icon(schema.value("icon", "")).Source(source);
        if (schema.contains("offer")) {
            auto offer = schema["offer"].is_string() ? ParseOffer(schema["offer"].get<std::string>()) : std::nullopt;
            if (!offer) {
                WriteString("\"offer\" must be everywhere, owner or nowhere", errorBuf, errorBufSize);
                result = CE_ERR_INVALID_ARG;
                return std::nullopt;
            }
            builder.Offer(*offer);
        }
        if (schema.contains("hidden") && schema["hidden"].is_boolean()) builder.Hidden(schema["hidden"].get<bool>());
        if (schema.contains("cost") && schema["cost"].is_number_integer()) builder.Cost(schema["cost"].get<int>());
        builder.Tags(ReadStringList(schema, "tags"));
        if (schema.contains("aliases") && schema["aliases"].is_array()) {
            builder.Aliases(schema["aliases"].get<std::vector<std::string>>());
        }
        if (schema.contains("positional") && schema["positional"].is_array()) {
            builder.Positional(schema["positional"].get<std::vector<std::string>>());
        }
        if (schema.contains("compareField") && schema["compareField"].is_string()) {
            builder.Compare(schema["compareField"].get<std::string>());
        }
        if (schema.contains("requiresPlugins")) {
            if (!schema["requiresPlugins"].is_array() || std::any_of(schema["requiresPlugins"].begin(), schema["requiresPlugins"].end(),
                    [](const json& value) { return !value.is_string() || value.get<std::string>().empty(); })) {
                WriteString("requiresPlugins must be an array of plugin filenames", errorBuf, errorBufSize);
                result = CE_ERR_INVALID_ARG;
                return std::nullopt;
            }
            builder.RequiresPlugins(schema["requiresPlugins"].get<std::vector<std::string>>());
        }
        std::vector<FieldSchema> fields;
        if (schema.contains("fields") && schema["fields"].is_array()) {
            for (const auto& f : schema["fields"]) fields.push_back(FieldFromJson(f));
        }
        // Field keys share the condition's JSON object with the engine's own keys
        std::unordered_set<std::string> seen;
        for (const auto& f : fields) {
            std::string problem;
            if (f.key.empty()) {
                problem = "a field has no \"key\"";
            } else if (f.key == "type" || f.key == "forcePolled") {
                problem = "field key \"" + f.key + "\" is reserved by the engine (it is the condition's own JSON key)";
            } else if ((f.key == "operator" || f.key == "operation") && f.type != FieldType::Comparison) {
                problem = "field key \"" + f.key + "\" is reserved for the comparison operator";
            } else if (!seen.insert(f.key).second) {
                problem = "field key \"" + f.key + "\" is declared twice";
            }
            if (!problem.empty()) {
                WriteString("type '" + name + "': " + problem, errorBuf, errorBufSize);
                result = CE_ERR_INVALID_ARG;
                return std::nullopt;
            }
        }
        builder.Fields(std::move(fields));
        return builder;
    }
}

namespace {
    // Reads the host's number: (actor, params) -> ValueResult
    using HostValueReader = std::function<ValueResult(void* actor, const char* params)>;

    CE_Result RegisterValueType(const char* schemaJson, HostValueReader reader, uint32_t dependencies, char* errorBuf,
                                uint32_t errorBufSize, const std::string& caller) {
        json schema;
        CE_Result result = CE_OK;
        auto built = BuilderFromSchema(schemaJson, schema, result, errorBuf, errorBufSize, caller);
        if (!built) return result;
        auto& builder = *built;
        const std::string name = schema.value("name", "");

        ValueInfo info;
        if (schema.contains("value") && schema["value"].is_object()) {
            const auto& v = schema["value"];
            info.unit = v.value("unit", "");
            info.integer = v.value("integer", false);
            if (v.contains("range") && v["range"].is_array() && v["range"].size() == 2) {
                if (v["range"][0].is_number()) info.min = v["range"][0].get<double>();
                if (v["range"][1].is_number()) info.max = v["range"][1].get<double>();
            }
        }
        builder.Value(info);

        // Comparable by default: MyType(...) > 0.5 sets "value"
        std::string compareField = schema.value("compareField", "value");
        if (!schema.contains("compareField")) builder.Compare(compareField);

        // Generic editors need a number field for the compared value; add one when the
        // schema does not declare it (labelled with the unit, limited to the range)
        std::vector<FieldSchema> fields;
        if (schema.contains("fields") && schema["fields"].is_array()) {
            for (const auto& f : schema["fields"]) fields.push_back(FieldFromJson(f));
        }
        if (std::none_of(fields.begin(), fields.end(), [&](const FieldSchema& f) { return f.key == compareField; })) {
            auto field = Field::Float(compareField, info.unit.empty() ? "Value" : "Value (" + info.unit + ")", 0);
            if (info.min) field.min = info.min;
            if (info.max) field.max = info.max;
            fields.push_back(std::move(field));
            builder.Fields(std::move(fields));
        }

        const auto deps = static_cast<EventDependency>(dependencies);
        builder.Factory([reader, deps, name, compareField](const json& j) -> std::unique_ptr<Condition> {
            // The callback sees the parameters without the comparison
            json params = j;
            params.erase(compareField);
            params.erase("operator");
            params.erase("operation");
            auto paramsText = std::make_shared<const std::string>(params.dump());
            return std::make_unique<CallbackValueCondition>(
                name,
                [reader, paramsText](const EvaluationContext& context) {
                    return reader(context.GetActor(), paramsText->c_str());
                },
                deps, j, compareField);
        });

        if (!builder.Register(Registry::Get())) {
            WriteString("type '" + name + "' is already registered", errorBuf, errorBufSize);
            return CE_ERR_ALREADY_EXISTS;
        }
        WriteString({}, errorBuf, errorBufSize);
        return CE_OK;
    }

    ValueResult FromHostStatus(int32_t status, double value, std::string reason) {
        if (status == CE_VALUE_OK) return ValueResult::Of(value);
        const auto clamped = status >= CE_VALUE_PENDING && status <= CE_VALUE_ERROR ? status : CE_VALUE_ERROR;
        return ValueResult::Fail(static_cast<ValueStatus>(clamped),
                                 reason.empty() ? std::string("reported by the type's plugin") : std::move(reason));
    }
}

extern "C" {

CONDITIONENGINE_API uint32_t ConditionEngine_GetAPIVersion(void) {
    return CONDITIONENGINE_API_VERSION;
}

// ── Compile / evaluate ───────────────────────────────────────────────────────

CONDITIONENGINE_API CE_Handle ConditionEngine_Compile(const char* source, char* errorBuf, uint32_t errorBufSize) {
    if (!source) {
        WriteString("source is null", errorBuf, errorBufSize);
        return 0;
    }
    std::string error;
    auto condition = Expression::CompileAny(source, &error);
    if (!condition) {
        WriteString(error, errorBuf, errorBufSize);
        return 0;
    }
    WriteString({}, errorBuf, errorBufSize);
    return Store(std::shared_ptr<const Condition>(std::move(condition)));
}

CONDITIONENGINE_API void ConditionEngine_Release(CE_Handle handle) {
    std::lock_guard lock(g_handleMutex);
    g_handles.erase(handle);
}

CONDITIONENGINE_API int32_t ConditionEngine_Evaluate(CE_Handle handle, void* actor) {
    auto condition = Lookup(handle);
    if (!condition) return -1;
    return Evaluate(*condition, ActorOrPlayer(actor)) ? 1 : 0;
}

CONDITIONENGINE_API int32_t ConditionEngine_EvaluateFormID(CE_Handle handle, uint32_t actorFormId) {
    auto* actor = actorFormId ? RE::TESForm::LookupByID<RE::Actor>(actorFormId) : nullptr;
    if (actorFormId && !actor) return -1;
    return ConditionEngine_Evaluate(handle, actor);
}

CONDITIONENGINE_API uint32_t ConditionEngine_Explain(CE_Handle handle, void* actor, int32_t asText, char* buf,
                                                     uint32_t bufSize) {
    auto condition = Lookup(handle);
    if (!condition) return WriteString(asText ? "invalid condition handle" : "", buf, bufSize);
    EvaluationContext context(ActorOrPlayer(actor));
    const auto explanation = Explain::ExplainCondition(*condition, context);
    return WriteString(asText ? Explain::ToText(explanation) : explanation.dump(), buf, bufSize);
}

CONDITIONENGINE_API uint32_t ConditionEngine_GetDependencies(CE_Handle handle) {
    auto condition = Lookup(handle);
    return condition ? static_cast<uint32_t>(condition->GetEffectiveEventDependencies()) : 0;
}

CONDITIONENGINE_API uint32_t ConditionEngine_ToJson(CE_Handle handle, char* buf, uint32_t bufSize) {
    auto condition = Lookup(handle);
    return WriteString(condition ? condition->ToJsonComplete().dump() : std::string{}, buf, bufSize);
}

CONDITIONENGINE_API uint32_t ConditionEngine_ToExpression(CE_Handle handle, char* buf, uint32_t bufSize) {
    auto condition = Lookup(handle);
    return WriteString(condition ? Expression::ToExpression(*condition) : std::string{}, buf, bufSize);
}

CONDITIONENGINE_API uint32_t ConditionEngine_Describe(CE_Handle handle, char* buf, uint32_t bufSize) {
    auto condition = Lookup(handle);
    return WriteString(condition ? condition->GetDescription() : std::string{}, buf, bufSize);
}

CONDITIONENGINE_API uint32_t ConditionEngine_Validate(CE_Handle handle, char* buf, uint32_t bufSize) {
    auto condition = Lookup(handle);
    if (!condition) return WriteString("invalid handle", buf, bufSize);
    std::string joined;
    for (const auto& error : condition->Validate()) {
        if (!joined.empty()) joined += '\n';
        joined += error;
    }
    return WriteString(joined, buf, bufSize);
}

// ── Named conditions ─────────────────────────────────────────────────────────

CONDITIONENGINE_API CE_Result ConditionEngine_RegisterNamed(const char* id, const char* definition, const char* owner,
                                                              char* errorBuf, uint32_t errorBufSize) {
    if (!id || !*id || !definition) return CE_ERR_INVALID_ARG;
    const std::string text = definition;
    NamedConditionInfo info;
    info.source = owner && *owner ? owner : ModuleNameAt(_ReturnAddress());
    std::string expression = text;
    const auto first = text.find_first_not_of(" \t\r\n");
    std::unique_ptr<Condition> condition;
    std::string error;
    if (first != std::string::npos && text[first] == '{') {
        json def;
        try {
            def = json::parse(text);
        } catch (const std::exception& e) {
            WriteString(std::string("invalid JSON: ") + e.what(), errorBuf, errorBufSize);
            return CE_ERR_PARSE;
        }
        if (def.contains("type")) {
            expression.clear();
            condition = Condition::FromJson(def);  // a raw condition tree
        } else {
            if (!info.ReadMetadata(def, &error)) {
                WriteString(error, errorBuf, errorBufSize);
                return CE_ERR_INVALID_ARG;
            }
            if (def.contains("expr") && def["expr"].is_string()) {
                expression = def["expr"].get<std::string>();
                condition = Expression::CompileAny(expression, &error);
            } else if (def.contains("condition") && def["condition"].is_object()) {
                expression.clear();
                condition = Condition::FromJson(def["condition"]);
            } else {
                error = "needs \"expr\" or \"condition\"";
            }
        }
    } else {
        condition = Expression::CompileAny(text, &error);
    }
    if (!condition) {
        WriteString(error.empty() ? "not a valid condition" : error, errorBuf, errorBufSize);
        return CE_ERR_PARSE;
    }
    info.expression = expression;
    Registry::Get().RegisterNamed(id, std::shared_ptr<const Condition>(std::move(condition)), std::move(info));
    WriteString({}, errorBuf, errorBufSize);
    return CE_OK;
}

CONDITIONENGINE_API CE_Result ConditionEngine_RegisterNamedCallback(const char* id, CE_NamedEvalFn fn, void* userData,
                                                                    uint32_t dependencies, const char* owner) {
    if (!id || !*id || !fn) return CE_ERR_INVALID_ARG;
    const std::string name = id;
    auto condition = std::make_shared<CallbackCondition>(
        name,
        [fn, userData](const EvaluationContext& context) { return fn(context.GetActor(), userData) != 0; },
        static_cast<EventDependency>(dependencies), json{{"type", "Rule"}, {"ruleName", name}});
    Registry::Get().RegisterNamed(name, std::move(condition), owner ? owner : "");
    return CE_OK;
}

CONDITIONENGINE_API CE_Result ConditionEngine_UnregisterNamed(const char* id) {
    if (!id) return CE_ERR_INVALID_ARG;
    return Registry::Get().UnregisterNamed(id) ? CE_OK : CE_ERR_NOT_FOUND;
}

CONDITIONENGINE_API uint32_t ConditionEngine_UnregisterOwner(const char* owner) {
    return owner ? static_cast<uint32_t>(Registry::Get().UnregisterSource(owner)) : 0;
}

CONDITIONENGINE_API int32_t ConditionEngine_LookupNamed(const char* key, uint32_t* outDependencies) {
    if (!key) return 0;
    auto condition = Registry::Get().FindNamed(key);
    if (!condition) return 0;
    if (outDependencies) *outDependencies = static_cast<uint32_t>(condition->GetEffectiveEventDependencies());
    return 1;
}

CONDITIONENGINE_API int32_t ConditionEngine_EvaluateNamed(const char* key, void* actor) {
    if (!key) return -1;
    auto condition = Registry::Get().FindNamed(key);
    if (!condition) return -1;
    return Evaluate(*condition, ActorOrPlayer(actor)) ? 1 : 0;
}

CONDITIONENGINE_API CE_Result ConditionEngine_AddNamedResolver(CE_NamedLookupFn lookup, CE_NamedKeyEvalFn eval,
                                                               void* userData) {
    if (!lookup || !eval) return CE_ERR_INVALID_ARG;
    Registry::Get().AddNamedResolver([lookup, eval, userData](const std::string& key) -> std::shared_ptr<const Condition> {
        uint32_t deps = 0;
        if (lookup(key.c_str(), &deps, userData) == 0) return nullptr;
        return std::make_shared<CallbackCondition>(
            key,
            [eval, userData, key](const EvaluationContext& context) {
                return eval(key.c_str(), context.GetActor(), userData) != 0;
            },
            static_cast<EventDependency>(deps), json{{"type", "Rule"}, {"ruleName", key}});
    });
    return CE_OK;
}

// ── Custom condition types ───────────────────────────────────────────────────

CONDITIONENGINE_API CE_Result ConditionEngine_RegisterType(const char* schemaJson, CE_CustomEvalFn fn, void* userData,
                                                             uint32_t dependencies, char* errorBuf,
                                                             uint32_t errorBufSize) {
    if (!schemaJson || !fn) return CE_ERR_INVALID_ARG;

    json schema;
    CE_Result result = CE_OK;
    auto built = BuilderFromSchema(schemaJson, schema, result, errorBuf, errorBufSize, ModuleNameAt(_ReturnAddress()));
    if (!built) return result;
    auto& builder = *built;
    const std::string name = schema.value("name", "");

    const auto deps = static_cast<EventDependency>(dependencies);
    builder.Factory([fn, userData, deps, name](const json& j) -> std::unique_ptr<Condition> {
        auto params = std::make_shared<const std::string>(j.dump());
        return std::make_unique<CallbackCondition>(
            name,
            [fn, userData, params](const EvaluationContext& context) {
                return fn(context.GetActor(), params->c_str(), userData) != 0;
            },
            deps, j);
    });

    if (!builder.Register(Registry::Get())) {
        WriteString("type '" + name + "' is already registered", errorBuf, errorBufSize);
        return CE_ERR_ALREADY_EXISTS;
    }
    WriteString({}, errorBuf, errorBufSize);
    return CE_OK;
}

// ── Catalog ──────────────────────────────────────────────────────────────────

CONDITIONENGINE_API uint32_t ConditionEngine_GetCatalog(const char* filterJson, char* buf, uint32_t bufSize) {
    json filter = json{{"offered", false}};
    if (filterJson && *filterJson) {
        try {
            filter = json::parse(filterJson);
        } catch (const std::exception& e) {
            return WriteString(json{{"error", std::string("invalid filter JSON: ") + e.what()}}.dump(), buf, bufSize);
        }
    }
    return WriteString(Registry::Get().GetCatalog(filter).dump(), buf, bufSize);
}

CONDITIONENGINE_API CE_Result ConditionEngine_RegisterCategory(const char* categoryJson, const char* owner, char* errorBuf,
                                                               uint32_t errorBufSize) {
    if (!categoryJson) return CE_ERR_INVALID_ARG;
    json j;
    try {
        j = json::parse(categoryJson);
    } catch (const std::exception& e) {
        WriteString(std::string("invalid JSON: ") + e.what(), errorBuf, errorBufSize);
        return CE_ERR_PARSE;
    }
    if (!j.is_object()) {
        WriteString("a category is a JSON object", errorBuf, errorBufSize);
        return CE_ERR_INVALID_ARG;
    }
    CategoryInfo category;
    category.id = j.value("id", "");
    category.label = j.value("label", "");
    category.parent = j.value("parent", "");
    category.order = j.value("order", 100);
    category.icon = j.value("icon", "");
    category.description = j.value("description", "");
    std::string error;
    const std::string who = owner && *owner ? owner : ModuleNameAt(_ReturnAddress());
    if (!Registry::Get().RegisterCategory(std::move(category), who, &error)) {
        WriteString(error, errorBuf, errorBufSize);
        return CE_ERR_INVALID_ARG;
    }
    WriteString({}, errorBuf, errorBufSize);
    return CE_OK;
}

CONDITIONENGINE_API void ConditionEngine_RegisterSource(const char* id, const char* label) {
    if (id && *id) Registry::Get().RegisterSource(id, label ? label : "");
}

CONDITIONENGINE_API uint32_t ConditionEngine_GetJsonSchema(char* buf, uint32_t bufSize) {
    return WriteString(Registry::Get().GetJsonSchema().dump(), buf, bufSize);
}

// Availability reads use snapshots and never invoke game APIs or evaluation callbacks.
CONDITIONENGINE_API uint32_t ConditionEngine_GetAvailability(CE_Handle handle, char* buf, uint32_t bufSize) {
    const auto condition = Lookup(handle);
    const auto status = condition ? condition->CheckAvailability() :
        AvailabilityStatus{AvailabilityState::Unavailable, "Invalid condition handle"};
    return WriteString(status.ToJson().dump(), buf, bufSize);
}
CONDITIONENGINE_API CE_Result ConditionEngine_SetTypeAvailability(const char* type, int32_t state, const char* reason) {
    if (!type || state < 0 || state > 2) return CE_ERR_INVALID_ARG;
    return Availability::Get().SetTypeStatus(type, {static_cast<AvailabilityState>(state), reason ? reason : ""}) ?
        CE_OK : CE_ERR_NOT_FOUND;
}
CONDITIONENGINE_API uint32_t ConditionEngine_SubscribeAvailability(CE_AvailabilityChangedFn fn, void* userData) {
    if (!fn) return 0;
    return Availability::Get().Subscribe([fn, userData](uint64_t revision) { fn(revision, userData); });
}
CONDITIONENGINE_API void ConditionEngine_UnsubscribeAvailability(uint32_t subscription) {
    Availability::Get().Unsubscribe(subscription);
}
CONDITIONENGINE_API uint64_t ConditionEngine_GetAvailabilityRevision(void) { return Availability::Get().Revision(); }

// ── Watches ──────────────────────────────────────────────────────────────────

CONDITIONENGINE_API void ConditionEngine_Unwatch(CE_WatchHandle watch) {
    Watcher::Get().Unwatch(watch);
}

CONDITIONENGINE_API CE_WatchHandle ConditionEngine_Watch(CE_Handle handle, void* actor, CE_WatchFn fn, void* userData,
                                                           const CE_WatchOptions* options) {
    auto condition = Lookup(handle);
    if (!condition || !fn) return 0;
    const auto watchOptions = ReadWatchOptions(options);
    return Watcher::Get().Watch(
        condition, static_cast<RE::Actor*>(actor),
        [fn, userData](WatchId id, RE::Actor* target, bool result) { fn(id, target, result ? 1 : 0, userData); },
        watchOptions);
}

CONDITIONENGINE_API int32_t ConditionEngine_Refresh(CE_WatchHandle watch) {
    auto result = Watcher::Get().Refresh(watch);
    return result.has_value() ? (*result ? 1 : 0) : -1;
}

CONDITIONENGINE_API CE_WatchHandle ConditionEngine_WatchList(const CE_Handle* conditions, uint32_t count, void* actor,
                                                             CE_ListWatchFn fn, void* userData,
                                                             const CE_WatchOptions* options) {
    if (!conditions || count == 0) return 0;
    std::vector<std::shared_ptr<const Condition>> rules;
    rules.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        auto condition = Lookup(conditions[i]);
        if (!condition) return 0;
        rules.push_back(std::move(condition));  // copied: the host may release its handles
    }
    ListWatchCallback callback;
    if (fn) {
        callback = [fn, userData](WatchId id, RE::Actor* target, int winner) { fn(id, target, winner, userData); };
    }
    return Watcher::Get().WatchList(std::move(rules), static_cast<RE::Actor*>(actor), std::move(callback),
                                    ReadWatchOptions(options));
}

CONDITIONENGINE_API int32_t ConditionEngine_ReadListWatch(CE_WatchHandle watch) { return Watcher::Get().ReadListWatch(watch); }

CONDITIONENGINE_API int32_t ConditionEngine_RefreshListWatch(CE_WatchHandle watch) {
    return Watcher::Get().RefreshListWatch(watch);
}

CONDITIONENGINE_API uint32_t ConditionEngine_ReadListWatchState(CE_WatchHandle watch, char* buf, uint32_t bufSize) {
    const auto state = Watcher::Get().ReadListWatchState(watch);
    return WriteString(state.is_null() ? std::string{} : state.dump(), buf, bufSize);
}

CONDITIONENGINE_API void ConditionEngine_SetPollInterval(uint32_t milliseconds) {
    Watcher::Get().SetPollInterval(std::chrono::milliseconds(milliseconds));
}

CONDITIONENGINE_API uint32_t ConditionEngine_GetPollInterval(void) {
    return static_cast<uint32_t>(Watcher::Get().GetPollInterval().count());
}

CONDITIONENGINE_API uint32_t ConditionEngine_GetStats(char* buf, uint32_t bufSize) {
    return WriteString(Stats::Snapshot().dump(), buf, bufSize);
}

CONDITIONENGINE_API void ConditionEngine_ResetStats(void) { Stats::Reset(); }

CONDITIONENGINE_API void ConditionEngine_SetStatsEnabled(int32_t enabled) { Stats::SetEnabled(enabled != 0); }

CONDITIONENGINE_API void ConditionEngine_NotifyEvent(uint32_t dependencies, void* actor) {
    // May be called from any thread; queued for the main thread and merged per actor
    auto* target = static_cast<RE::Actor*>(actor);
    Watcher::Get().PostEvent(static_cast<EventDependency>(dependencies),
                             target ? target->GetFormID() : Watcher::kAllActors);
}

// ── Values (v5) ──────────────────────────────────────────────────────────────

CONDITIONENGINE_API CE_ValueHandle ConditionEngine_CompileValue(const char* source, char* errorBuf, uint32_t errorBufSize) {
    if (!source) {
        WriteString("source is null", errorBuf, errorBufSize);
        return 0;
    }
    std::string error;
    auto value = Expression::CompileValueAny(source, &error);
    if (!value) {
        WriteString(error, errorBuf, errorBufSize);
        return 0;
    }
    WriteString({}, errorBuf, errorBufSize);
    return StoreValue(std::shared_ptr<const Value>(std::move(value)));
}

CONDITIONENGINE_API void ConditionEngine_ReleaseValue(CE_ValueHandle value) {
    std::lock_guard lock(g_valueMutex);
    g_values.erase(value);
}

CONDITIONENGINE_API CE_Result ConditionEngine_EvaluateValue(CE_ValueHandle handle, void* actor, CE_ValueSample* out,
                                                              char* reasonBuf, uint32_t reasonBufSize) {
    auto value = LookupValue(handle);
    if (!value) {
        WriteString("invalid value handle", reasonBuf, reasonBufSize);
        return CE_ERR_INVALID_HANDLE;
    }
    EvaluationContext context(ActorOrPlayer(actor));
    const auto result = ReadValueAvailable(*value, context);
    ValueSample sample;
    sample.value = result.value;
    sample.status = result.status;
    sample.sampledAt = result.sampledAt.value_or(ValueSample::Clock::now());
    sample.availabilityRevision = Availability::Get().Revision();
    WriteSample(sample, out);
    WriteString(result.Ok() ? (result.from ? "from " + result.FromText() : std::string{}) : result.reason, reasonBuf,
                reasonBufSize);
    return CE_OK;
}

CONDITIONENGINE_API uint32_t ConditionEngine_ExplainValue(CE_ValueHandle handle, void* actor, int32_t asText, char* buf,
                                                          uint32_t bufSize) {
    auto value = LookupValue(handle);
    if (!value) return WriteString(asText ? "invalid value handle" : "", buf, bufSize);
    EvaluationContext context(ActorOrPlayer(actor));
    const auto explanation = Explain::ExplainValue(*value, context);
    return WriteString(asText ? Explain::ToText(explanation) : explanation.dump(), buf, bufSize);
}

CONDITIONENGINE_API uint32_t ConditionEngine_GetValueDependencies(CE_ValueHandle handle) {
    auto value = LookupValue(handle);
    return value ? static_cast<uint32_t>(value->GetEventDependencies()) : 0;
}

CONDITIONENGINE_API uint32_t ConditionEngine_GetValueAvailability(CE_ValueHandle handle, char* buf, uint32_t bufSize) {
    const auto value = LookupValue(handle);
    const auto status = value ? value->CheckAvailability()
                              : AvailabilityStatus{AvailabilityState::Unavailable, "Invalid value handle"};
    return WriteString(status.ToJson().dump(), buf, bufSize);
}

CONDITIONENGINE_API uint32_t ConditionEngine_ValueToExpression(CE_ValueHandle handle, char* buf, uint32_t bufSize) {
    auto value = LookupValue(handle);
    return WriteString(value ? Expression::ToExpression(*value) : std::string{}, buf, bufSize);
}

CONDITIONENGINE_API uint32_t ConditionEngine_ValueToJson(CE_ValueHandle handle, char* buf, uint32_t bufSize) {
    auto value = LookupValue(handle);
    return WriteString(value ? value->ToJson().dump() : std::string{}, buf, bufSize);
}

CONDITIONENGINE_API uint32_t ConditionEngine_ValidateValue(CE_ValueHandle handle, char* buf, uint32_t bufSize) {
    auto value = LookupValue(handle);
    if (!value) return WriteString("invalid handle", buf, bufSize);
    std::string joined;
    for (const auto& error : value->Validate()) {
        if (!joined.empty()) joined += '\n';
        joined += error;
    }
    return WriteString(joined, buf, bufSize);
}

CONDITIONENGINE_API CE_Result ConditionEngine_RegisterNamedValue(const char* name, const char* definition, const char* owner,
                                                                 char* errorBuf, uint32_t errorBufSize) {
    if (!name || !*name || !definition) return CE_ERR_INVALID_ARG;
    json parsed;
    const std::string text = definition;
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && text[first] == '{') {
        try {
            parsed = json::parse(text);
        } catch (const std::exception& e) {
            WriteString(std::string("invalid JSON: ") + e.what(), errorBuf, errorBufSize);
            return CE_ERR_PARSE;
        }
    } else {
        parsed = text;
    }
    std::string error;
    auto def = NamedValueDef::Parse(name, parsed, owner ? owner : "", &error);
    if (!def) {
        WriteString(error, errorBuf, errorBufSize);
        return CE_ERR_PARSE;
    }
    Registry::Get().RegisterNamedValue(std::move(def));
    Watcher::Get().PostEvent(EventDependency::Value, Watcher::kAllActors);
    WriteString({}, errorBuf, errorBufSize);
    return CE_OK;
}

CONDITIONENGINE_API CE_Result ConditionEngine_UnregisterNamedValue(const char* name) {
    if (!name) return CE_ERR_INVALID_ARG;
    if (!Registry::Get().UnregisterNamedValue(name)) return CE_ERR_NOT_FOUND;
    Watcher::Get().PostEvent(EventDependency::Value, Watcher::kAllActors);
    return CE_OK;
}

CONDITIONENGINE_API uint32_t ConditionEngine_ListNamedValues(char* buf, uint32_t bufSize) {
    json list = json::array();
    for (const auto& def : Registry::Get().ListNamedValues()) list.push_back(def->ToJson());
    return WriteString(list.dump(), buf, bufSize);
}

CONDITIONENGINE_API CE_Result ConditionEngine_SetValue(void* actor, const char* name, double value, float timeoutSeconds) {
    if (!name) return CE_ERR_INVALID_ARG;
    return PushedValues::Get().Set(static_cast<RE::Actor*>(actor), name, value, timeoutSeconds) ? CE_OK : CE_ERR_INVALID_ARG;
}

CONDITIONENGINE_API CE_Result ConditionEngine_SetGlobalValue(const char* name, double value, float timeoutSeconds) {
    if (!name) return CE_ERR_INVALID_ARG;
    return PushedValues::Get().SetGlobal(name, value, timeoutSeconds) ? CE_OK : CE_ERR_INVALID_ARG;
}

CONDITIONENGINE_API CE_Result ConditionEngine_ClearValue(void* actor, const char* name) {
    if (!name) return CE_ERR_INVALID_ARG;
    return PushedValues::Get().Clear(static_cast<RE::Actor*>(actor), name) ? CE_OK : CE_ERR_NOT_FOUND;
}

CONDITIONENGINE_API CE_Result ConditionEngine_ClearGlobalValue(const char* name) {
    if (!name) return CE_ERR_INVALID_ARG;
    return PushedValues::Get().ClearGlobal(name) ? CE_OK : CE_ERR_NOT_FOUND;
}

CONDITIONENGINE_API CE_WatchHandle ConditionEngine_WatchValue(CE_ValueHandle handle, void* actor, CE_ValueWatchFn fn,
                                                              void* userData, const CE_ValueWatchOptions* options) {
    auto value = LookupValue(handle);
    if (!value) return 0;

    ValueWatchOptions watchOptions;
    auto has = [options](std::size_t end) { return options && options->structSize >= end; };
    if (has(offsetof(CE_ValueWatchOptions, fireImmediately) + sizeof(int32_t))) {
        watchOptions.fireImmediately = options->fireImmediately != 0;
    }
    if (has(offsetof(CE_ValueWatchOptions, pollIntervalMs) + sizeof(uint32_t))) {
        watchOptions.pollInterval = options->pollIntervalMs == CE_POLL_NEVER ? WatchOptions::kNever
                                                                             : std::chrono::milliseconds(options->pollIntervalMs);
    }
    if (has(offsetof(CE_ValueWatchOptions, extraDependencies) + sizeof(uint32_t))) {
        watchOptions.extraDependencies = static_cast<EventDependency>(options->extraDependencies);
    }
    if (has(offsetof(CE_ValueWatchOptions, minStep) + sizeof(double))) watchOptions.minStep = options->minStep;
    if (has(offsetof(CE_ValueWatchOptions, thresholdCount) + sizeof(uint32_t)) && options->thresholds &&
        options->thresholdCount > 0) {
        watchOptions.thresholds.assign(options->thresholds, options->thresholds + options->thresholdCount);
    }
    if (has(offsetof(CE_ValueWatchOptions, hysteresis) + sizeof(double))) watchOptions.hysteresis = options->hysteresis;

    ValueWatchCallback callback;
    if (fn) {
        callback = [fn, userData](WatchId id, RE::Actor* target, const ValueSample& sample, int crossed) {
            const auto cSample = ToCSample(sample);
            fn(id, target, &cSample, crossed, userData);
        };
    }
    return Watcher::Get().WatchValue(std::move(value), static_cast<RE::Actor*>(actor), std::move(callback), watchOptions);
}

CONDITIONENGINE_API int32_t ConditionEngine_ReadValueWatch(CE_WatchHandle watch, CE_ValueSample* out, char* reasonBuf,
                                                           uint32_t reasonBufSize) {
    ValueSample sample;
    if (!Watcher::Get().ReadValueWatch(watch, sample)) {
        WriteString({}, reasonBuf, reasonBufSize);
        return 0;
    }
    WriteSample(sample, out);
    WriteString(sample.status == ValueStatus::Ok ? (sample.from.empty() ? std::string{} : "from " + sample.from)
                                                 : sample.reason,
                reasonBuf, reasonBufSize);
    return 1;
}

CONDITIONENGINE_API int32_t ConditionEngine_RefreshValueWatch(CE_WatchHandle watch, CE_ValueSample* out) {
    auto sample = Watcher::Get().RefreshValue(watch);
    if (!sample) return 0;
    WriteSample(*sample, out);
    return 1;
}

CONDITIONENGINE_API CE_Result ConditionEngine_RegisterValueType(const char* schemaJson, CE_CustomValueFn fn, void* userData,
                                                                uint32_t dependencies, char* errorBuf,
                                                                uint32_t errorBufSize) {
    if (!schemaJson || !fn) return CE_ERR_INVALID_ARG;
    return RegisterValueType(
        schemaJson,
        [fn, userData](void* actor, const char* params) {
            double out = 0.0;
            char reason[256] = {};
            const auto status = fn(actor, params, &out, reason, sizeof reason, userData);
            reason[sizeof reason - 1] = '\0';
            return FromHostStatus(status, out, reason);
        },
        dependencies, errorBuf, errorBufSize, ModuleNameAt(_ReturnAddress()));
}

// ── Important people ─────────────────────────────────────────────────────────

CONDITIONENGINE_API CE_Result ConditionEngine_DefinePeopleGroup(const char* id, const char* definition, const char* owner,
                                                                char* errorBuf, uint32_t errorBufSize) {
    if (!id || !*id) return CE_ERR_INVALID_ARG;
    json j = json::object();
    if (definition && *definition) {
        try {
            j = json::parse(definition, nullptr, true, /*ignore_comments=*/true);
        } catch (const std::exception& e) {
            WriteString(std::string("invalid JSON: ") + e.what(), errorBuf, errorBufSize);
            return CE_ERR_PARSE;
        }
    }
    if (!j.is_object() && !j.is_array()) {
        WriteString("a people group is a JSON object or a list of members", errorBuf, errorBufSize);
        return CE_ERR_INVALID_ARG;
    }
    std::vector<std::string> warnings;
    const std::string who = owner && *owner ? owner : ModuleNameAt(_ReturnAddress());
    People::AddFromJson(json{{id, j}}, who, &warnings);
    std::string text;
    for (const auto& w : warnings) text += (text.empty() ? "" : "; ") + w;
    WriteString(text, errorBuf, errorBufSize);
    return warnings.empty() ? CE_OK : CE_ERR_INVALID_ARG;
}

CONDITIONENGINE_API CE_Result ConditionEngine_AddImportantPerson(const char* group, const char* member, const char* owner) {
    if (!member || !*member) return CE_ERR_INVALID_ARG;
    const std::string who = owner && *owner ? owner : ModuleNameAt(_ReturnAddress());
    return People::AddMember(group ? group : "", member, who) ? CE_OK : CE_ERR_ALREADY_EXISTS;
}

CONDITIONENGINE_API CE_Result ConditionEngine_RemoveImportantPerson(const char* group, const char* member) {
    if (!group || !member) return CE_ERR_INVALID_ARG;
    return People::RemoveMember(group, member) ? CE_OK : CE_ERR_NOT_FOUND;
}

CONDITIONENGINE_API uint32_t ConditionEngine_GetImportantGroup(void* actor, const char* groups, char* buf,
                                                               uint32_t bufSize) {
    std::vector<std::string> filter;
    if (groups && *groups) filter = ReadStringList(json{{"groups", groups}}, "groups");
    return WriteString(People::MatchGroup(static_cast<RE::Actor*>(actor), filter), buf, bufSize);
}

CONDITIONENGINE_API uint32_t ConditionEngine_ListPeopleGroups(char* buf, uint32_t bufSize) {
    json list = json::array();
    for (const auto& g : People::ListGroups()) list.push_back(g.ToJson());
    return WriteString(list.dump(), buf, bufSize);
}

// ── Providers ────────────────────────────────────────────────────────────────

CONDITIONENGINE_API void ConditionEngine_SetLoveSceneProvider(CE_ActorFlagFn fn, void* userData) {
    if (!fn) {
        Registry::Get().SetLoveSceneProvider(nullptr);
        return;
    }
    Registry::Get().SetLoveSceneProvider([fn, userData](RE::Actor* actor) { return fn(actor, userData) != 0; });
}

} // extern "C"
