#pragma once

// Registry.h - Condition type registry, named conditions, and host providers.
//
// Types:      every condition type (built-in or host-supplied) registers a factory
//             under a canonical name plus aliases. Condition::FromJson dispatches here.
// Named:      reusable conditions referenced by id/name from a "Rule" condition or the
//             expression syntax (Rule("Name") / @Name). The engine stores its own
//             (presets, C API, Papyrus) and consults host resolvers (e.g. DressCode rules).
// Providers:  hooks for game state the engine cannot know by itself (love scenes,
//             survival-mod temperature).
// Catalog:    every type's schema (fields, labels, category) as JSON, so UIs and
//             other mods can build editors without hard-coding condition types.

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"
#include "ConditionEngine/Schema.h"

#include <shared_mutex>

namespace ConditionEngine {

    using ConditionFactory = std::function<std::unique_ptr<Condition>(const nlohmann::json&)>;

    struct TypeInfo {
        std::string name;                    // Canonical type name written by ToJson ("Time")
        std::vector<std::string> aliases;    // Extra names accepted on read ("TimeCondition")
        ConditionFactory factory;

        // Expression syntax metadata (see Expression.h).
        // positional: JSON fields filled by positional arguments, in order.
        // compareField: field set by a trailing comparison, e.g. ActorValue(Health) < 30
        //               sets "value" = 30 and "operation" = "<".
        std::vector<std::string> positional;
        std::string compareField;
        // Fields ToJson writes for UIs that the factory ignores; omitted from expression text.
        std::vector<std::string> derived;
        std::string description;

        // -- Schema (see Schema.h) ------------------------------------------
        std::string label;                   // Display name ("Time of Day"); defaults to name
        std::string category = "other";      // CategoryInfo id
        std::string icon;                    // Optional short glyph for UIs
        std::vector<FieldSchema> fields;     // Editable fields, in display order
        std::vector<std::string> requiresPlugins; // All must be loaded; types remain registered when unavailable.
        bool hidden = false;                 // Legacy/internal: accepted but not offered in pickers
        std::string source;                  // Who registered it ("ConditionEngine", plugin name)
    };

    struct NamedConditionInfo {
        std::string id;
        std::string source;       // Where it came from (preset file, plugin name, "papyrus")
        std::string expression;   // Original text, when registered from an expression
    };

    // Resolver for host-owned named conditions. Returns null when the key is unknown.
    // Hosts that keep ownership return a non-owning pointer, e.g.
    //   std::shared_ptr<const Condition>(std::shared_ptr<const Condition>{}, rawPtr)
    // which must stay alive for the duration of the evaluation.
    using NamedResolver = std::function<std::shared_ptr<const Condition>(const std::string& key)>;

    using ActorFlagProvider = std::function<bool(RE::Actor*)>;
    using ActorFloatProvider = std::function<float(RE::Actor*)>;

    class Registry {
    public:
        static Registry& Get();

        // ── Types ────────────────────────────────────────────────────────────
        // Returns false if the name or an alias is already registered.
        bool RegisterType(TypeInfo info);
        const TypeInfo* FindType(const std::string& nameOrAlias) const;
        std::vector<const TypeInfo*> GetTypes() const;
        std::unique_ptr<Condition> Create(const nlohmann::json& json) const;

        // Called by Create for types nobody registered (e.g. to proxy types that live
        // in another module). Returning null drops the condition as before.
        using UnknownTypeHandler = std::function<std::unique_ptr<Condition>(const nlohmann::json&)>;
        void SetUnknownTypeHandler(UnknownTypeHandler handler);

        // -- Categories and catalog -----------------------------------------
        void RegisterCategory(CategoryInfo category);
        std::vector<CategoryInfo> GetCategories() const;

        // {"version", "categories": [...], "types": [{name, label, category, icon,
        //   description, aliases, fields, positional, compareField, dependencies,
        //   hidden, source}]}
        nlohmann::json GetCatalog() const;

        // JSON Schema (draft-07) for condition JSON, generated from the catalog
        nlohmann::json GetJsonSchema() const;

        // ── Named conditions ─────────────────────────────────────────────────
        // Replaces any existing engine-owned entry with the same id.
        void RegisterNamed(const std::string& id, std::shared_ptr<const Condition> condition,
                           const std::string& source = {}, const std::string& expression = {});
        bool UnregisterNamed(const std::string& id);
        // Removes every engine-owned entry registered with this source. Returns the count.
        std::size_t UnregisterSource(const std::string& source);
        std::vector<NamedConditionInfo> ListNamed() const;

        // Looks up engine-owned entries first, then host resolvers.
        std::shared_ptr<const Condition> FindNamed(const std::string& key) const;

        void AddNamedResolver(NamedResolver resolver);

        // ── Providers ────────────────────────────────────────────────────────
        void SetLoveSceneProvider(ActorFlagProvider provider);
        void SetTemperatureProvider(ActorFloatProvider provider);
        bool QueryLoveScene(RE::Actor* actor) const;
        float QueryTemperature(RE::Actor* actor) const;

    private:
        Registry();

        struct NamedEntry {
            std::shared_ptr<const Condition> condition;
            NamedConditionInfo info;
        };

        mutable std::shared_mutex mutex_;
        std::vector<std::unique_ptr<TypeInfo>> types_;
        std::unordered_map<std::string, const TypeInfo*> typeLookup_;
        std::unordered_map<std::string, NamedEntry> named_;
        std::vector<NamedResolver> resolvers_;
        std::vector<CategoryInfo> categories_;
        UnknownTypeHandler unknownTypeHandler_;
        ActorFlagProvider loveSceneProvider_;
        ActorFloatProvider temperatureProvider_;
    };

    // Fluent TypeInfo construction:
    //   TypeBuilder("Level", "Actor Level", "actor")
    //       .Fields({Field::Int("level", "Level", 1)})
    //       .Positional({"level"}).Compare("level")
    //       .Factory([](const json& j) { ... })
    //       .Register(registry);
    class TypeBuilder {
    public:
        TypeBuilder(std::string name, std::string label, std::string category) {
            info_.name = std::move(name);
            info_.label = std::move(label);
            info_.category = std::move(category);
        }

        TypeBuilder& Aliases(std::vector<std::string> v) { info_.aliases = std::move(v); return *this; }
        TypeBuilder& Icon(std::string v) { info_.icon = std::move(v); return *this; }
        TypeBuilder& Describe(std::string v) { info_.description = std::move(v); return *this; }
        TypeBuilder& Fields(std::vector<FieldSchema> v) { info_.fields = std::move(v); return *this; }
        TypeBuilder& Positional(std::vector<std::string> v) { info_.positional = std::move(v); return *this; }
        TypeBuilder& Compare(std::string field) { info_.compareField = std::move(field); return *this; }
        TypeBuilder& Derived(std::vector<std::string> v) { info_.derived = std::move(v); return *this; }
        TypeBuilder& RequiresPlugins(std::vector<std::string> v) { info_.requiresPlugins = std::move(v); return *this; }
        TypeBuilder& Hidden(bool v = true) { info_.hidden = v; return *this; }
        TypeBuilder& Source(std::string v) { info_.source = std::move(v); return *this; }
        TypeBuilder& Factory(ConditionFactory v) { info_.factory = std::move(v); return *this; }

        // Positional arguments default to the field keys in order (comparison fields excluded)
        bool Register(Registry& registry) {
            if (info_.positional.empty()) {
                for (const auto& f : info_.fields) {
                    if (f.type != FieldType::Comparison && f.type != FieldType::Conditions && f.key != info_.compareField) {
                        info_.positional.push_back(f.key);
                    }
                }
            }
            return registry.RegisterType(std::move(info_));
        }

    private:
        TypeInfo info_;
    };

    // Registers every built-in condition type. Called once by Registry::Get().
    void RegisterBuiltinTypes(Registry& registry);

} // namespace ConditionEngine
