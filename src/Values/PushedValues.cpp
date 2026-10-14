#include "ConditionEngine/Values/PushedValues.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Watcher.h"

namespace ConditionEngine {

    namespace {
        constexpr RE::FormID kGlobalKey = 0;
        constexpr RE::FormID kPlayerFormId = 0x14;

        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        // The named value for a push, registering an implicit pushed value for unknown names
        std::shared_ptr<const NamedValueDef> Declare(const std::string& name, bool global) {
            auto& registry = Registry::Get();
            if (auto def = registry.FindNamedValue(name)) return def;
            if (!NamedValueDef::IsValidName(name)) return nullptr;
            auto def = std::make_shared<NamedValueDef>();
            def->name = name;
            def->key = Lower(name);
            def->pushed = true;
            def->global = global;
            def->scope = global ? NamedValueDef::Scope::Global : NamedValueDef::Scope::Actor;
            def->source = "implicit";
            def->definition = {{"pushed", true}, {"scope", global ? "global" : "actor"}};
            registry.RegisterNamedValue(def);
            return def;
        }
    }

    PushedValues& PushedValues::Get() {
        static PushedValues instance;
        return instance;
    }

    RE::FormID PushedValues::ActorKey(RE::Actor* actor) {
        return actor ? actor->GetFormID() : kPlayerFormId;
    }

    bool PushedValues::Set(RE::Actor* actor, const std::string& name, double value, double timeoutSeconds) {
        auto def = Declare(name, false);
        if (!def) return false;
        if (timeoutSeconds <= 0.0) timeoutSeconds = def->pushedTimeoutSeconds;
        // A global value has one slot whichever actor pushes it
        const RE::FormID key = def->global ? kGlobalKey : ActorKey(actor);
        return Store(key, def->name, value, timeoutSeconds);
    }

    bool PushedValues::SetGlobal(const std::string& name, double value, double timeoutSeconds) {
        auto def = Declare(name, true);
        if (!def) return false;
        if (timeoutSeconds <= 0.0) timeoutSeconds = def->pushedTimeoutSeconds;
        return Store(kGlobalKey, def->name, value, timeoutSeconds);
    }

    bool PushedValues::Clear(RE::Actor* actor, const std::string& name) {
        auto def = Registry::Get().FindNamedValue(name);
        return Remove(def && def->global ? kGlobalKey : ActorKey(actor), name);
    }

    bool PushedValues::ClearGlobal(const std::string& name) { return Remove(kGlobalKey, name); }

    void PushedValues::ClearAll() {
        std::lock_guard lock(mutex_);
        entries_.clear();
        ended_.clear();
        count_ = 0;
    }

    void PushedValues::ExpireDue() {
        const auto now = ValueResult::Clock::now();
        std::vector<RE::FormID> expired;
        {
            std::lock_guard lock(mutex_);
            for (auto it = entries_.begin(); it != entries_.end();) {
                if (it->second.expiresAt && now >= *it->second.expiresAt) {
                    expired.push_back(it->first.first);
                    ended_[it->first] = Ended::Expired;
                    it = entries_.erase(it);
                    count_ = entries_.size();
                } else {
                    ++it;
                }
            }
        }
        for (auto key : expired) {
            Watcher::Get().PostEvent(EventDependency::Value, key == kGlobalKey ? Watcher::kAllActors : key);
        }
    }

    bool PushedValues::Store(RE::FormID key, const std::string& name, double value, double timeoutSeconds) {
        if (!std::isfinite(value)) return false;
        Entry entry;
        entry.value = value;
        entry.setAt = ValueResult::Clock::now();
        if (timeoutSeconds > 0.0) {
            entry.expiresAt = entry.setAt + std::chrono::duration_cast<ValueResult::Clock::duration>(
                                                std::chrono::duration<double>(timeoutSeconds));
        }
        {
            std::lock_guard lock(mutex_);
            entries_[{key, Lower(name)}] = entry;
            ended_.erase({key, Lower(name)});
            count_ = entries_.size();
        }
        Watcher::Get().PostEvent(EventDependency::Value, key == kGlobalKey ? Watcher::kAllActors : key);
        return true;
    }

    bool PushedValues::Remove(RE::FormID key, const std::string& name) {
        bool removed;
        {
            std::lock_guard lock(mutex_);
            removed = entries_.erase({key, Lower(name)}) > 0;
            count_ = entries_.size();
            if (removed) ended_[{key, Lower(name)}] = Ended::Cleared;
        }
        if (removed) Watcher::Get().PostEvent(EventDependency::Value, key == kGlobalKey ? Watcher::kAllActors : key);
        return removed;
    }

    std::string PushedValues::WhyMissing(const NamedValueDef& def, RE::Actor* actor) const {
        const std::string name = Lower(def.name);
        const auto now = ValueResult::Clock::now();
        std::lock_guard lock(mutex_);
        for (const RE::FormID key : {def.global ? kGlobalKey : ActorKey(actor), kGlobalKey}) {
            if (auto it = entries_.find({key, name});
                it != entries_.end() && it->second.expiresAt && now >= *it->second.expiresAt) {
                return "the push of " + def.name + " expired";
            }
            if (auto it = ended_.find({key, name}); it != ended_.end()) {
                return it->second == Ended::Expired ? "the push of " + def.name + " expired"
                                                    : def.name + " was cleared";
            }
        }
        if (!def.global) {
            const bool elsewhere = std::any_of(entries_.begin(), entries_.end(), [&](const auto& kv) {
                return kv.first.second == name && kv.first.first != ActorKey(actor);
            });
            if (elsewhere) return def.name + " is pushed for other actors, not this one";
        }
        return "nothing pushed for " + def.name + (def.global ? std::string{} : " on this actor");
    }

    std::optional<ValueResult> PushedValues::Read(const NamedValueDef& def, RE::Actor* actor) const {
        if (count_.load(std::memory_order_acquire) == 0) {
            if (def.pushed && !def.value && def.pushedDefault) return ValueResult::Of(*def.pushedDefault);
            return std::nullopt;
        }
        std::string lowered;
        const std::string& name = def.key.empty() ? (lowered = Lower(def.name)) : def.key;
        const auto now = ValueResult::Clock::now();
        {
            std::lock_guard lock(mutex_);
            auto lookup = [&](RE::FormID key) -> const Entry* {
                auto it = entries_.find({key, name});
                if (it == entries_.end()) return nullptr;
                if (it->second.expiresAt && now >= *it->second.expiresAt) return nullptr;  // expired; Set replaces it
                return &it->second;
            };
            const Entry* entry = def.global ? nullptr : lookup(ActorKey(actor));
            if (!entry) entry = lookup(kGlobalKey);
            if (entry) {
                auto result = ValueResult::Of(entry->value);
                result.sampledAt = entry->setAt;
                return result;
            }
        }
        if (def.pushed && !def.value && def.pushedDefault) return ValueResult::Of(*def.pushedDefault);
        return std::nullopt;
    }

} // namespace ConditionEngine
