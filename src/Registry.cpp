#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Availability.h"

#include <mutex>

namespace ConditionEngine {

    namespace {
        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }
    }

    Registry& Registry::Get() {
        static Registry instance;
        return instance;
    }

    Registry::Registry() {
        RegisterBuiltinTypes(*this);
    }

    // ── Types ────────────────────────────────────────────────────────────────

    bool Registry::RegisterType(TypeInfo info) {
        std::unique_lock lock(mutex_);

        std::vector<std::string> keys;
        keys.push_back(Lower(info.name));
        for (const auto& alias : info.aliases) keys.push_back(Lower(alias));

        for (const auto& key : keys) {
            if (typeLookup_.contains(key)) {
                SKSE::log::warn("ConditionEngine: condition type '{}' already registered (key '{}')",
                                info.name, key);
                return false;
            }
        }

        if (info.label.empty()) info.label = info.name;
        if (info.source.empty()) info.source = "ConditionEngine";
        // Comparable types expose their operator as a field so generic editors show it
        if (!info.compareField.empty() &&
            std::none_of(info.fields.begin(), info.fields.end(),
                         [](const FieldSchema& f) { return f.type == FieldType::Comparison; })) {
            auto at = std::find_if(info.fields.begin(), info.fields.end(),
                                   [&](const FieldSchema& f) { return f.key == info.compareField; });
            info.fields.insert(at, Field::Comparison());
        }

        auto owned = std::make_unique<TypeInfo>(std::move(info));
        for (const auto& key : keys) typeLookup_[key] = owned.get();
        types_.push_back(std::move(owned));
        lock.unlock();
        Availability::Get().Invalidate();
        return true;
    }

    const TypeInfo* Registry::FindType(const std::string& nameOrAlias) const {
        std::shared_lock lock(mutex_);
        auto it = typeLookup_.find(Lower(nameOrAlias));
        return it != typeLookup_.end() ? it->second : nullptr;
    }

    std::vector<const TypeInfo*> Registry::GetTypes() const {
        std::shared_lock lock(mutex_);
        std::vector<const TypeInfo*> result;
        result.reserve(types_.size());
        for (const auto& t : types_) result.push_back(t.get());
        return result;
    }

    std::unique_ptr<Condition> Registry::Create(const nlohmann::json& json) const {
        if (!json.is_object() || !json.contains("type") || !json["type"].is_string()) {
            return nullptr;
        }

        const std::string type = json["type"].get<std::string>();

        // Copy the factory out so the lock is not held while it runs: factories for
        // composite types (Logical) recurse back into Create.
        ConditionFactory factory;
        {
            std::shared_lock lock(mutex_);
            auto it = typeLookup_.find(Lower(type));
            if (it != typeLookup_.end()) factory = it->second->factory;
        }

        if (!factory) {
            UnknownTypeHandler fallback;
            {
                std::shared_lock lock(mutex_);
                fallback = unknownTypeHandler_;
            }
            if (fallback) {
                if (auto proxied = fallback(json)) {
                    proxied->forcePolled = json.value("forcePolled", false);
                    return proxied;
                }
            }
            SKSE::log::warn("Condition::FromJson - Unknown condition type '{}', condition will be dropped", type);
            return nullptr;
        }

        std::unique_ptr<Condition> result;
        try {
            result = factory(json);
        } catch (const std::exception& e) {
            SKSE::log::warn("Condition::FromJson - Failed to parse '{}' condition: {}", type, e.what());
            return nullptr;
        }
        if (!result) return nullptr;

        // Cross-cutting fields that apply to all condition types
        result->forcePolled = json.value("forcePolled", false);
        return result;
    }

    void Registry::SetUnknownTypeHandler(UnknownTypeHandler handler) {
        std::unique_lock lock(mutex_);
        unknownTypeHandler_ = std::move(handler);
    }

    // -- Categories and catalog ---------------------------------------------

    void Registry::RegisterCategory(CategoryInfo category) {
        std::unique_lock lock(mutex_);
        for (auto& existing : categories_) {
            if (existing.id == category.id) {
                existing = std::move(category);
                return;
            }
        }
        categories_.push_back(std::move(category));
    }

    std::vector<CategoryInfo> Registry::GetCategories() const {
        std::shared_lock lock(mutex_);
        auto result = categories_;
        std::stable_sort(result.begin(), result.end(),
                         [](const CategoryInfo& a, const CategoryInfo& b) { return a.order < b.order; });
        return result;
    }

    namespace {
        // Dependencies of a default-configured instance (factories never touch game state)
        EventDependency DefaultDependencies(const TypeInfo& info) {
            nlohmann::json defaults{{"type", info.name}};
            for (const auto& f : info.fields) {
                if (!f.defaultValue.is_null()) defaults[f.key] = f.defaultValue;
            }
            try {
                if (auto instance = info.factory ? info.factory(defaults) : nullptr) {
                    return instance->GetEventDependencies();
                }
            } catch (...) {
            }
            return EventDependency::None;
        }
    }

    nlohmann::json Registry::GetCatalog() const {
        nlohmann::json categories = nlohmann::json::array();
        for (const auto& c : GetCategories()) {
            categories.push_back({{"id", c.id}, {"label", c.label}, {"order", c.order}});
        }

        nlohmann::json types = nlohmann::json::array();
        for (const auto* info : GetTypes()) {
            nlohmann::json fields = nlohmann::json::array();
            for (const auto& f : info->fields) fields.push_back(FieldToJson(f));

            nlohmann::json type{
                {"name", info->name},
                {"label", info->label},
                {"category", info->category},
                {"description", info->description},
                {"aliases", info->aliases},
                {"fields", std::move(fields)},
                {"positional", info->positional},
                {"dependencies", DependencyNames(DefaultDependencies(*info))},
                {"source", info->source},
            };
            type["requiresPlugins"] = info->requiresPlugins;
            type["availability"] = Availability::Get().CheckType(*info).ToJson();
            if (!info->icon.empty()) type["icon"] = info->icon;
            if (!info->compareField.empty()) type["compareField"] = info->compareField;
            if (info->hidden) type["hidden"] = true;
            types.push_back(std::move(type));
        }

        return {{"version", 1}, {"availabilityRevision", Availability::Get().Revision()}, {"categories", std::move(categories)}, {"types", std::move(types)}};
    }

    nlohmann::json Registry::GetJsonSchema() const {
        nlohmann::json variants = nlohmann::json::array();
        for (const auto* info : GetTypes()) {
            nlohmann::json names = nlohmann::json::array({info->name});
            for (const auto& alias : info->aliases) names.push_back(alias);

            nlohmann::json properties{{"type", {{"enum", names}}}};
            nlohmann::json required = nlohmann::json::array({"type"});
            for (const auto& f : info->fields) {
                properties[f.key] = FieldToJsonSchema(f);
                if (f.required) required.push_back(f.key);
            }
            properties["forcePolled"] = {{"type", "boolean"}, {"description", "Re-evaluate on every poll tick"}};

            nlohmann::json variant{{"title", info->label},
                                   {"type", "object"},
                                   {"properties", std::move(properties)},
                                   {"required", std::move(required)}};
            if (!info->description.empty()) variant["description"] = info->description;
            variants.push_back(std::move(variant));
        }

        return {
            {"$schema", "http://json-schema.org/draft-07/schema#"},
            {"$id", "https://dresscode.mod/schemas/condition.schema.json"},
            {"title", "Condition"},
            {"description", "A ConditionEngine condition. Generated from the type registry - do not edit by hand."},
            {"type", "object"},
            {"required", {"type"}},
            {"oneOf", std::move(variants)},
        };
    }

    // ── Named conditions ─────────────────────────────────────────────────────

    void Registry::RegisterNamed(const std::string& id, std::shared_ptr<const Condition> condition,
                                 const std::string& source, const std::string& expression) {
        if (id.empty() || !condition) return;
        std::unique_lock lock(mutex_);
        auto& entry = named_[id];
        if (entry.condition && entry.info.source != source) {
            SKSE::log::warn("ConditionEngine: named condition '{}' from '{}' replaced by '{}'",
                            id, entry.info.source, source);
        }
        entry.condition = std::move(condition);
        entry.info = NamedConditionInfo{id, source, expression};
        lock.unlock();
        Availability::Get().Invalidate();
    }

    bool Registry::UnregisterNamed(const std::string& id) {
        std::unique_lock lock(mutex_);
        const bool removed = named_.erase(id) > 0;
        lock.unlock();
        if (removed) Availability::Get().Invalidate();
        return removed;
    }

    std::size_t Registry::UnregisterSource(const std::string& source) {
        std::unique_lock lock(mutex_);
        const auto removed = std::erase_if(named_, [&](const auto& kv) { return kv.second.info.source == source; });
        lock.unlock();
        if (removed) Availability::Get().Invalidate();
        return removed;
    }

    std::vector<NamedConditionInfo> Registry::ListNamed() const {
        std::shared_lock lock(mutex_);
        std::vector<NamedConditionInfo> result;
        result.reserve(named_.size());
        for (const auto& [id, entry] : named_) result.push_back(entry.info);
        return result;
    }

    std::shared_ptr<const Condition> Registry::FindNamed(const std::string& key) const {
        if (key.empty()) return nullptr;

        std::vector<NamedResolver> resolvers;
        {
            std::shared_lock lock(mutex_);
            auto it = named_.find(key);
            if (it != named_.end()) return it->second.condition;
            resolvers = resolvers_;
        }

        for (const auto& resolver : resolvers) {
            if (auto found = resolver(key)) return found;
        }
        return nullptr;
    }

    void Registry::AddNamedResolver(NamedResolver resolver) {
        if (!resolver) return;
        std::unique_lock lock(mutex_);
        resolvers_.push_back(std::move(resolver));
        lock.unlock();
        Availability::Get().Invalidate();
    }

    // ── Providers ────────────────────────────────────────────────────────────

    void Registry::SetLoveSceneProvider(ActorFlagProvider provider) {
        std::unique_lock lock(mutex_);
        loveSceneProvider_ = std::move(provider);
    }

    void Registry::SetTemperatureProvider(ActorFloatProvider provider) {
        std::unique_lock lock(mutex_);
        temperatureProvider_ = std::move(provider);
    }

    bool Registry::QueryLoveScene(RE::Actor* actor) const {
        ActorFlagProvider provider;
        {
            std::shared_lock lock(mutex_);
            provider = loveSceneProvider_;
        }
        return provider ? provider(actor) : false;
    }

    float Registry::QueryTemperature(RE::Actor* actor) const {
        ActorFloatProvider provider;
        {
            std::shared_lock lock(mutex_);
            provider = temperatureProvider_;
        }
        return provider ? provider(actor) : 20.0f;
    }

} // namespace ConditionEngine
