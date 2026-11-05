#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Availability.h"
#include "ConditionEngine/Stats.h"

#include <mutex>
#include <sstream>

namespace ConditionEngine {

    namespace {
        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        // "survival.hunger_level" -> "Hunger Level"
        std::string LabelFromId(const std::string& id) {
            std::string word = id.substr(id.find_last_of('.') == std::string::npos ? 0 : id.find_last_of('.') + 1);
            std::string label;
            bool upper = true;
            for (char c : word) {
                if (c == '_' || c == '-' || c == ' ') {
                    if (!label.empty() && label.back() != ' ') label += ' ';
                    upper = true;
                    continue;
                }
                label += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
                upper = false;
            }
            return label.empty() ? id : label;
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

        info.statsSlot = Stats::RegisterSlot(info.name);
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

    bool Registry::SetTypeCost(const std::string& nameOrAlias, int cost) {
        std::unique_lock lock(mutex_);
        auto it = typeLookup_.find(Lower(nameOrAlias));
        if (it == typeLookup_.end()) return false;
        const_cast<TypeInfo*>(it->second)->cost = std::clamp(cost, 0, 9);
        return true;
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
        const TypeInfo* info = nullptr;
        {
            std::shared_lock lock(mutex_);
            auto it = typeLookup_.find(Lower(type));
            if (it != typeLookup_.end()) {
                info = it->second;
                factory = info->factory;
            }
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
        result->typeInfo = info;
        return result;
    }

    void Registry::SetUnknownTypeHandler(UnknownTypeHandler handler) {
        std::unique_lock lock(mutex_);
        unknownTypeHandler_ = std::move(handler);
    }

    // -- Categories and catalog ---------------------------------------------

    void Registry::RegisterCategory(CategoryInfo category) {
        if (category.source.empty()) category.source = "ConditionEngine";
        std::unique_lock lock(mutex_);
        for (auto& existing : categories_) {
            if (existing.id == category.id) {
                existing = std::move(category);
                return;
            }
        }
        categories_.push_back(std::move(category));
    }

    bool Registry::RegisterCategory(CategoryInfo category, const std::string& owner, std::string* error) {
        auto fail = [&](std::string message) {
            if (error) *error = std::move(message);
            return false;
        };
        if (category.id.empty()) return fail("a category needs an \"id\"");
        if (category.label.empty()) category.label = LabelFromId(category.id);
        category.source = owner.empty() ? "plugin" : owner;

        std::unique_lock lock(mutex_);
        auto find = [&](const std::string& id) -> CategoryInfo* {
            for (auto& c : categories_) {
                if (c.id == id) return &c;
            }
            return nullptr;
        };
        if (auto* existing = find(category.id)) {
            // Shared and topical: the first registrant keeps the label; later ones reuse
            // the id and may only fill in what is missing
            if (existing->description.empty()) existing->description = category.description;
            if (existing->icon.empty()) existing->icon = category.icon;
            if (existing->parent.empty() && !category.parent.empty() && category.parent != existing->id) {
                existing->parent = category.parent;
            }
            return true;
        }
        // Refuse a parent chain that leads back to this id
        std::string parent = category.parent;
        for (int depth = 0; depth < 32 && !parent.empty(); ++depth) {
            if (parent == category.id) return fail("category '" + category.id + "' would be its own ancestor");
            const auto* up = find(parent);
            parent = up ? up->parent : std::string{};
        }
        categories_.push_back(std::move(category));
        return true;
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

    namespace {
        bool IEq(std::string_view a, std::string_view b) {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                       return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
                   });
        }

        // What a picker asks for; it filters by category, source, tags and text itself
        struct CatalogFilter {
            bool offered = false;   // apply `offer`
            std::string forOwner;   // the picker's identity, for Offer::Owner items
            std::unordered_set<std::string> include{"types", "values", "conditions", "categories"};

            static CatalogFilter From(const nlohmann::json& j) {
                CatalogFilter f;
                if (!j.is_object()) return f;
                f.offered = j.value("offered", true);
                f.forOwner = Lower(j.value("for", ""));
                if (j.contains("include") && j["include"].is_array()) {
                    f.include.clear();
                    for (const auto& v : j["include"]) {
                        if (v.is_string()) f.include.insert(Lower(v.get<std::string>()));
                    }
                }
                return f;
            }
        };
    }

    const char* OfferName(Offer offer) {
        switch (offer) {
            case Offer::Owner: return "owner";
            case Offer::Nowhere: return "nowhere";
            default: return "everywhere";
        }
    }

    std::optional<Offer> ParseOffer(std::string_view name) {
        if (IEq(name, "everywhere")) return Offer::Everywhere;
        if (IEq(name, "owner")) return Offer::Owner;
        if (IEq(name, "nowhere")) return Offer::Nowhere;
        return std::nullopt;
    }

    bool NamedConditionInfo::ReadMetadata(const nlohmann::json& definition, std::string* error) {
        if (!definition.is_object()) return true;
        label = definition.value("label", label);
        category = definition.value("category", category);
        icon = definition.value("icon", icon);
        description = definition.value("description", description);
        owner = definition.value("owner", owner);
        if (definition.contains("offer")) {
            auto parsed = definition["offer"].is_string() ? ParseOffer(definition["offer"].get<std::string>()) : std::nullopt;
            if (!parsed) {
                if (error) *error = "\"offer\" must be everywhere, owner or nowhere";
                return false;
            }
            offer = *parsed;
        }
        if (definition.contains("tags")) tags = ReadStringList(definition, "tags");
        return true;
    }

    void Registry::RegisterSource(const std::string& id, const std::string& label) {
        if (id.empty()) return;
        std::unique_lock lock(mutex_);
        sourceLabels_[Lower(id)] = label.empty() ? id : label;
    }

    std::string Registry::SourceLabel(const std::string& id) const {
        std::shared_lock lock(mutex_);
        auto it = sourceLabels_.find(Lower(id));
        return it != sourceLabels_.end() ? it->second : id;
    }

    nlohmann::json Registry::GetCatalog() const {
        return GetCatalog(nlohmann::json{{"offered", false}});
    }

    nlohmann::json Registry::GetCatalog(const nlohmann::json& filterJson) const {
        using json = nlohmann::json;
        const auto filter = CatalogFilter::From(filterJson);

        // Registered categories, plus one per unknown id an item names (top level,
        // labelled from the id) - items are never dropped or regrouped
        std::map<std::string, CategoryInfo> categories;
        for (auto& c : GetCategories()) categories.emplace(c.id, std::move(c));
        auto resolveCategory = [&](const std::string& id, const char* fallback) -> std::string {
            const std::string key = id.empty() ? fallback : id;
            if (!categories.contains(key)) {
                CategoryInfo created;
                created.id = key;
                created.label = LabelFromId(key);
                created.order = 1000;
                categories.emplace(key, std::move(created));
            }
            return key;
        };
        auto offeredTo = [&](Offer offer, const std::string& owner) {
            if (!filter.offered) return true;
            if (offer == Offer::Everywhere) return true;
            return offer == Offer::Owner && !filter.forOwner.empty() && Lower(owner) == filter.forOwner;
        };

        std::map<std::string, int> sourceCounts;
        auto stamp = [&](json& item, const std::string& source, const std::string& category, const std::string& declared,
                         Offer offer, std::vector<std::string> tags) {
            item["source"] = source;
            item["sourceLabel"] = SourceLabel(source);
            item["owner"] = source;
            item["category"] = category;
            if (!declared.empty() && declared != category) item["declaredCategory"] = declared;
            item["offer"] = OfferName(offer);
            item["tags"] = std::move(tags);
            ++sourceCounts[source];
        };

        // ── Types ──
        json types = json::array();
        if (filter.include.contains("types")) {
            for (const auto* info : GetTypes()) {
                const Offer offer = info->hidden ? Offer::Nowhere : info->offer;
                std::vector<std::string> tags = info->tags;
                tags.push_back("condition");
                if (info->value) tags.push_back("value");
                if (info->name == "PapyrusCall") tags.push_back("async");
                if (!info->requiresPlugins.empty()) tags.push_back("requires-plugin");
                const auto category = resolveCategory(info->category, "other");
                if (!offeredTo(offer, info->source)) continue;

                json fields = json::array();
                for (const auto& f : info->fields) fields.push_back(FieldToJson(f));
                json type{
                    {"name", info->name},
                    {"label", info->label},
                    {"description", info->description},
                    {"aliases", info->aliases},
                    {"fields", std::move(fields)},
                    {"positional", info->positional},
                    {"dependencies", DependencyNames(DefaultDependencies(*info))},
                };
                type["requiresPlugins"] = info->requiresPlugins;
                type["availability"] = Availability::Get().CheckType(*info).ToJson();
                if (!info->icon.empty()) type["icon"] = info->icon;
                if (!info->compareField.empty()) type["compareField"] = info->compareField;
                if (offer == Offer::Nowhere) type["hidden"] = true;  // pre-v6 readers
                if (info->value) type["value"] = info->value->ToJson();
                type["cost"] = info->cost;
                stamp(type, info->source, category, info->category, offer, std::move(tags));
                types.push_back(std::move(type));
            }
        }

        // ── Named values ──
        json namedValues = json::array();
        if (filter.include.contains("values")) {
            for (const auto& def : ListNamedValues()) {
                std::vector<std::string> tags = def->tags;
                tags.push_back("value");
                tags.push_back(def->global ? "global" : "per-actor");
                if (def->pushed) tags.push_back("pushed");
                if (def->value && def->value->ToJson().dump().find("\"PapyrusCall\"") != std::string::npos) tags.push_back("async");
                if (!def->requiresPlugins.empty() || !def->requiresDlls.empty()) tags.push_back("requires-plugin");
                const auto owner = def->owner.empty() ? def->source : def->owner;
                const auto category = resolveCategory(def->category, "values");
                if (!offeredTo(def->offer, owner)) continue;
                json item = def->ToJson();
                if (!def->icon.empty()) item["icon"] = def->icon;
                if (!def->description.empty()) item["description"] = def->description;
                item["file"] = def->source;
                stamp(item, owner, category, def->category, def->offer, std::move(tags));
                namedValues.push_back(std::move(item));
            }
        }

        // ── Named conditions (engine-owned) ──
        json namedConditions = json::array();
        if (filter.include.contains("conditions")) {
            for (const auto& info : ListNamed()) {
                std::vector<std::string> tags = info.tags;
                tags.push_back("condition");
                const auto owner = info.owner.empty() ? info.source : info.owner;
                const auto category = resolveCategory(info.category, "named");
                if (!offeredTo(info.offer, owner)) continue;
                const std::string label = info.label.empty() ? info.id : info.label;
                json item{{"id", info.id}, {"label", label}, {"expression", info.expression}, {"file", info.source}};
                if (!info.icon.empty()) item["icon"] = info.icon;
                if (!info.description.empty()) item["description"] = info.description;
                stamp(item, owner, category, info.category, info.offer, std::move(tags));
                namedConditions.push_back(std::move(item));
            }
        }

        json categoryList = json::array();
        if (filter.include.contains("categories")) {
            std::vector<const CategoryInfo*> ordered;
            for (const auto& [id, c] : categories) ordered.push_back(&c);
            std::stable_sort(ordered.begin(), ordered.end(), [](const CategoryInfo* a, const CategoryInfo* b) {
                return a->order != b->order ? a->order < b->order : a->label < b->label;
            });
            for (const auto* c : ordered) categoryList.push_back(c->ToJson());
        }

        json sources = json::array();
        for (const auto& [id, count] : sourceCounts) {
            sources.push_back({{"id", id}, {"label", SourceLabel(id)}, {"count", count}});
        }

        return {{"version", 2},
                {"availabilityRevision", Availability::Get().Revision()},
                {"categories", std::move(categoryList)},
                {"sources", std::move(sources)},
                {"types", std::move(types)},
                {"namedValues", std::move(namedValues)},
                {"namedConditions", std::move(namedConditions)}};
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
            {"$id", "https://github.com/boop-boop-test/ConditionEngine/schemas/condition.schema.json"},
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

    void Registry::RegisterNamed(const std::string& id, std::shared_ptr<const Condition> condition, NamedConditionInfo info) {
        if (id.empty() || !condition) return;
        info.id = id;
        std::unique_lock lock(mutex_);
        auto& entry = named_[id];
        if (entry.condition && entry.info.source != info.source) {
            SKSE::log::warn("ConditionEngine: named condition '{}' from '{}' replaced by '{}'", id, entry.info.source,
                            info.source);
        }
        entry.condition = std::move(condition);
        entry.info = std::move(info);
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
        const auto removed = std::erase_if(named_, [&](const auto& kv) { return kv.second.info.source == source; }) +
                             std::erase_if(namedValues_, [&](const auto& kv) { return kv.second->source == source; });
        RebuildRefreshIndex();
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

    bool Registry::IsEngineNamed(const std::string& key) const {
        std::shared_lock lock(mutex_);
        return named_.contains(key);
    }

    void Registry::AddNamedResolver(NamedResolver resolver) {
        if (!resolver) return;
        std::unique_lock lock(mutex_);
        resolvers_.push_back(std::move(resolver));
        lock.unlock();
        Availability::Get().Invalidate();
    }

    // ── Named values ─────────────────────────────────────────────────────────

    void Registry::RegisterNamedValue(std::shared_ptr<const NamedValueDef> definition) {
        if (!definition || definition->name.empty()) return;
        // A source for a combined value nobody declared: declare it implicitly
        for (const auto& target : definition->provides) {
            if (!NamedValueDef::IsValidName(target) || FindNamedValue(target)) continue;
            auto combined = std::make_shared<NamedValueDef>();
            combined->name = target;
            combined->key = Lower(target);
            combined->source = "implicit";
            combined->definition = {{"combined", true}};
            combined->global = true;  // per actor as soon as a per-actor source exists (NamedValueRef::ReadsActor)
            RegisterNamedValue(std::move(combined));
        }
        std::unique_lock lock(mutex_);
        auto& entry = namedValues_[Lower(definition->name)];
        // Declaring a value that a source already declared implicitly is the normal order, not a replacement
        if (entry && entry->source != definition->source && entry->source != "implicit") {
            SKSE::log::warn("ConditionEngine: named value '{}' from '{}' replaced by '{}'",
                            definition->name, entry->source, definition->source);
        }
        entry = std::move(definition);
        RebuildRefreshIndex();
        lock.unlock();
        Availability::Get().Invalidate();
    }

    void Registry::RebuildRefreshIndex() {
        refreshIndex_.clear();
        providerIndex_.clear();
        for (const auto& [key, def] : namedValues_) {
            for (const auto& event : def->refreshOn) refreshIndex_[Lower(event)].push_back(key);
            for (const auto& target : def->provides) providerIndex_[Lower(target)].push_back(key);
        }
    }

    std::vector<std::shared_ptr<const NamedValueDef>> Registry::ProvidersOf(const std::string& name) const {
        std::vector<std::shared_ptr<const NamedValueDef>> result;
        {
            std::shared_lock lock(mutex_);
            if (providerIndex_.empty()) return result;
            auto it = providerIndex_.find(Lower(name));
            if (it == providerIndex_.end()) return result;
            for (const auto& key : it->second) {
                if (auto def = namedValues_.find(key); def != namedValues_.end()) result.push_back(def->second);
            }
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return a->priority != b->priority ? a->priority > b->priority : a->key < b->key;
        });
        return result;
    }

    std::vector<std::shared_ptr<const NamedValueDef>> Registry::ValuesRefreshedBy(std::string_view eventName) const {
        std::vector<std::shared_ptr<const NamedValueDef>> result;
        std::shared_lock lock(mutex_);
        if (refreshIndex_.empty()) return result;
        auto it = refreshIndex_.find(Lower(std::string(eventName)));
        if (it == refreshIndex_.end()) return result;
        for (const auto& key : it->second) {
            if (auto def = namedValues_.find(key); def != namedValues_.end()) result.push_back(def->second);
        }
        return result;
    }

    bool Registry::UnregisterNamedValue(const std::string& name) {
        std::unique_lock lock(mutex_);
        const bool removed = namedValues_.erase(Lower(name)) > 0;
        if (removed) RebuildRefreshIndex();
        lock.unlock();
        if (removed) Availability::Get().Invalidate();
        return removed;
    }

    std::shared_ptr<const NamedValueDef> Registry::FindNamedValue(const std::string& name) const {
        if (name.empty()) return nullptr;
        std::shared_lock lock(mutex_);
        auto it = namedValues_.find(Lower(name));
        return it != namedValues_.end() ? it->second : nullptr;
    }

    std::vector<std::shared_ptr<const NamedValueDef>> Registry::ListNamedValues() const {
        std::shared_lock lock(mutex_);
        std::vector<std::shared_ptr<const NamedValueDef>> result;
        result.reserve(namedValues_.size());
        for (const auto& [key, def] : namedValues_) result.push_back(def);
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a->name < b->name; });
        return result;
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
