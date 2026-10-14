#pragma once

// Registry.h - Condition type registry, named conditions, and host providers.
//
// Types:      every condition type (built-in or host-supplied) registers a factory
//             under a canonical name plus aliases. Condition::FromJson dispatches here.
// Named:      reusable conditions referenced by id/name from a "Rule" condition or the
//             expression syntax (Rule("Name") / @Name). The engine stores its own
//             (presets, C API, Papyrus) and consults host resolvers (e.g. DressCode rules).
// Values:     named numeric values referenced as $name (see Value.h), from presets,
//             the C API or Papyrus. Numeric types also declare TypeInfo::value.
// Providers:  hooks for game state the engine cannot know by itself (love scenes,
//             survival-mod temperature).
// Catalog:    every type's schema (fields, labels, category) as JSON, so UIs and
//             other mods can build editors without hard-coding condition types.

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"
#include "ConditionEngine/Schema.h"
#include "ConditionEngine/Value.h"

#include <shared_mutex>

namespace ConditionEngine {

    using ConditionFactory = std::function<std::unique_ptr<Condition>(const nlohmann::json&)>;

    // Where pickers offer an item (API v6). Metadata only: everything compiles and
    // evaluates for everyone whatever it says.
    enum class Offer {
        Everywhere,  // every picker (default)
        Owner,       // only pickers that ask as the item's owner (GetCatalog filter "for")
        Nowhere      // expression-only; TypeInfo::hidden means the same
    };
    const char* OfferName(Offer offer);  // "everywhere", "owner", "nowhere"
    std::optional<Offer> ParseOffer(std::string_view name);

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
        // Set when the type can be read as a number (Condition::ReadValue). Its unit and range.
        std::optional<ValueInfo> value;
        bool hidden = false;                 // Legacy/internal: accepted but not offered in pickers (= Offer::Nowhere)
        std::string source;                  // Who registered it ("ConditionEngine", plugin name) - also its owner
        Offer offer = Offer::Everywhere;
        std::vector<std::string> tags;       // Free filter tags; the catalog adds automatic ones
    };

    // A named value ($name). Built from a preset entry, RegisterNamedValue or an implicit push.
    struct NamedValueDef {
        enum class Scope { Auto, Actor, Global };

        std::string name;                              // as written; lookups ignore case
        std::shared_ptr<const Value> value;            // null for a pushed-only value
        std::shared_ptr<const Condition> enabledWhen;  // false for an actor = Unavailable for that actor
        std::optional<double> fallback;                // read while pending / unavailable
        std::string label;
        ValueInfo info;                                // unit, range
        Scope scope = Scope::Auto;
        bool global = false;                           // resolved scope (Auto inferred from the nodes)
        // Pushed values (SetValue): reverts to pushedDefault (or Unavailable) when a push times out
        bool pushed = false;
        std::optional<double> pushedDefault;
        double pushedTimeoutSeconds = 0.0;             // default timeout when SetValue passes 0
        double graceSeconds = 0.0;                     // keep the last good value this long after a failed read
        std::vector<std::string> requiresPlugins;
        std::vector<std::string> requiresDlls;         // must be loaded in the game process ("SomeNative.dll")
        std::string source;                            // preset file, owner plugin, "papyrus", "implicit"
        std::string owner;                             // for Offer::Owner; defaults to source
        std::string expression;                        // value text when given as an expression
        // Picker metadata (v6)
        std::string category;
        std::string icon;
        std::string description;
        Offer offer = Offer::Everywhere;
        std::vector<std::string> tags;
        nlohmann::json definition;                     // as registered

        // {"name", "label", "unit", "range", "integer", "scope", "pushed", "source", "expression"}
        nlohmann::json ToJson() const;

        // Parses a definition: expression text ("$a * 2"), a value JSON node, or an object
        // {"expr" | "value", "label", "unit", "range", "integer", "enabledWhen", "fallback",
        //  "scope", "pushed": {"default", "timeoutSeconds"}, "graceSeconds", "requiresPlugins",
        //  "requiresDlls", "category", "icon", "description", "offer", "tags", "owner"}.
        // Returns null and fills `error` when it is invalid.
        static std::shared_ptr<NamedValueDef> Parse(const std::string& name, const nlohmann::json& definition,
                                                    const std::string& source, std::string* error);

        // Value names are identifiers: letters, digits and '_', not starting with a digit
        static bool IsValidName(std::string_view name);
    };

    struct NamedConditionInfo {
        std::string id;
        std::string source;       // Where it came from (preset file, plugin name, "papyrus")
        std::string expression;   // Original text, when registered from an expression
        // Picker metadata (v6)
        std::string label;
        std::string category;
        std::string icon;
        std::string description;
        std::string owner;        // for Offer::Owner; defaults to source
        Offer offer = Offer::Everywhere;
        std::vector<std::string> tags;

        // Reads label, category, icon, description, owner, offer and tags from a
        // definition object. False with `error` for an invalid offer.
        bool ReadMetadata(const nlohmann::json& definition, std::string* error);
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
        // Built-in / host categories: replaces any category with the same id. The
        // source defaults to "ConditionEngine", which other owners cannot replace.
        void RegisterCategory(CategoryInfo category);
        // Plugin categories (v6). Categories are topical and shared ("survival" means
        // survival whoever registered it): the first registrant keeps the label, later
        // registrations of the same id are reuse and only fill in a missing description,
        // icon or parent. Parent cycles are refused; an unknown parent is top level.
        bool RegisterCategory(CategoryInfo category, const std::string& owner, std::string* error);
        std::vector<CategoryInfo> GetCategories() const;

        // Human label for a source / owner id ("NoTalkJustHands" -> "No Talk Just Hands"),
        // shown as each item's origin. Unregistered ids label themselves.
        void RegisterSource(const std::string& id, const std::string& label);
        std::string SourceLabel(const std::string& id) const;

        // Everything, unfiltered:
        // {"version", "availabilityRevision", "categories": [...],
        //  "types": [{name, label, category, icon, description, aliases, fields, positional,
        //             compareField, dependencies, hidden, offer, tags, source,
        //             value: {unit, range, integer}}],
        //  "namedValues": [...], "namedConditions": [...]}
        // An item whose category nobody registered gets that category created on the fly
        // (top level, labelled from the id). Every item carries "source" and
        // "sourceLabel" (its origin) and "sources" lists [{id, label, count}].
        nlohmann::json GetCatalog() const;
        // Filtered view for a picker (v6):
        //   {"for": "<owner>", "include": ["types", "values", "conditions", "categories"],
        //    "offered": true}   // false = also items not offered to this picker ("show all")
        // The catalog is small: pickers filter by category, source, tags and text themselves.
        nlohmann::json GetCatalog(const nlohmann::json& filter) const;

        // JSON Schema (draft-07) for condition JSON, generated from the catalog
        nlohmann::json GetJsonSchema() const;

        // ── Named conditions ─────────────────────────────────────────────────
        // Replaces any existing engine-owned entry with the same id.
        void RegisterNamed(const std::string& id, std::shared_ptr<const Condition> condition,
                           const std::string& source = {}, const std::string& expression = {});
        // With picker metadata (v6); info.id is set from `id`
        void RegisterNamed(const std::string& id, std::shared_ptr<const Condition> condition, NamedConditionInfo info);
        bool UnregisterNamed(const std::string& id);
        // Removes every engine-owned named condition and named value registered with
        // this source. Returns the count.
        std::size_t UnregisterSource(const std::string& source);
        std::vector<NamedConditionInfo> ListNamed() const;

        // Looks up engine-owned entries first, then host resolvers.
        std::shared_ptr<const Condition> FindNamed(const std::string& key) const;

        void AddNamedResolver(NamedResolver resolver);

        // ── Named values ─────────────────────────────────────────────────────
        // Replaces any existing value with the same name (case-insensitive).
        void RegisterNamedValue(std::shared_ptr<const NamedValueDef> definition);
        bool UnregisterNamedValue(const std::string& name);
        std::shared_ptr<const NamedValueDef> FindNamedValue(const std::string& name) const;
        std::vector<std::shared_ptr<const NamedValueDef>> ListNamedValues() const;

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
        std::unordered_map<std::string, std::shared_ptr<const NamedValueDef>> namedValues_;  // lowercase keys
        std::vector<NamedResolver> resolvers_;
        std::vector<CategoryInfo> categories_;
        std::unordered_map<std::string, std::string> sourceLabels_;  // lowercase id -> label
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
        TypeBuilder& Value(ValueInfo v) { info_.value = std::move(v); return *this; }
        TypeBuilder& Hidden(bool v = true) { info_.hidden = v; return *this; }
        TypeBuilder& Offer(ConditionEngine::Offer v) { info_.offer = v; return *this; }
        TypeBuilder& Tags(std::vector<std::string> v) { info_.tags = std::move(v); return *this; }
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
