#include "ConditionEngine/Registry.h"

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

        auto owned = std::make_unique<TypeInfo>(std::move(info));
        for (const auto& key : keys) typeLookup_[key] = owned.get();
        types_.push_back(std::move(owned));
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
    }

    bool Registry::UnregisterNamed(const std::string& id) {
        std::unique_lock lock(mutex_);
        return named_.erase(id) > 0;
    }

    std::size_t Registry::UnregisterSource(const std::string& source) {
        std::unique_lock lock(mutex_);
        return std::erase_if(named_, [&](const auto& kv) { return kv.second.info.source == source; });
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
