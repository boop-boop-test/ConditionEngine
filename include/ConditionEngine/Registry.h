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

#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Common.h"

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
        ActorFlagProvider loveSceneProvider_;
        ActorFloatProvider temperatureProvider_;
    };

    // Registers every built-in condition type. Called once by Registry::Get().
    void RegisterBuiltinTypes(Registry& registry);

} // namespace ConditionEngine
