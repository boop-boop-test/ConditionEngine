#include "OarValues.h"

#include <cmath>
#include <format>
#include <mutex>

namespace ConditionEngine::Dials {

    OarValues& OarValues::Get() {
        static OarValues instance;
        return instance;
    }

    void OarValues::Define(const DialDef& def, const std::vector<std::pair<ActorKey, double>>& values) {
        Entry e;
        e.id = def.id;
        e.shared = def.oar;
        e.global = def.scope == Scope::Global;
        e.kind = def.kind;
        e.defaultValue = def.Exposed(def.defaultValue);
        e.unit = def.unit;
        e.options = def.options;
        e.globalValue = e.defaultValue;
        if (e.shared) {
            for (const auto& [actor, value] : values) {
                if (e.global) {
                    if (actor == 0) e.globalValue = value;
                } else if (actor != 0) {
                    e.values[actor] = value;
                }
            }
        }
        std::unique_lock lock(mutex_);
        dials_[Lower(def.id)] = std::move(e);
    }

    void OarValues::Remove(std::string_view id) {
        std::unique_lock lock(mutex_);
        dials_.erase(Lower(id));
    }

    void OarValues::Update(std::string_view id, ActorKey actor, double value) {
        std::unique_lock lock(mutex_);
        const auto it = dials_.find(Lower(id));
        if (it == dials_.end() || !it->second.shared) return;
        auto& e = it->second;
        if (e.global) e.globalValue = value;
        else if (actor != 0) e.values[actor] = value;
    }

    void OarValues::ClearValues() {
        std::unique_lock lock(mutex_);
        for (auto& [_, e] : dials_) {
            e.globalValue = e.defaultValue;
            e.values.clear();
        }
    }

    void OarValues::Clear() {
        std::unique_lock lock(mutex_);
        dials_.clear();
    }

    OarValues::Reading OarValues::Read(std::string_view id, ActorKey actor, bool withText) const {
        Reading r;
        std::string key = Lower(id);
        while (!key.empty() && key.front() == '$') key.erase(key.begin());
        std::shared_lock lock(mutex_);
        const auto it = dials_.find(key);
        if (it == dials_.end()) {
            r.status = Status::Unknown;
            if (withText) r.text = id.empty() ? "no dial named" : "no dial named '" + std::string(id) + "'";
            return r;
        }
        const auto& e = it->second;
        if (!e.shared) {
            r.status = Status::NotShared;
            if (withText) r.text = "dial '" + e.id + "' is not shared with OAR: turn on \"oar\" in the dial";
            return r;
        }
        r.status = Status::Ok;
        if (e.global) {
            r.value = e.globalValue;
        } else {
            const auto v = e.values.find(actor);
            r.value = v != e.values.end() ? v->second : e.defaultValue;
        }
        if (withText) {
            const double rounded = std::round(r.value * 100.0) / 100.0;
            r.text = std::format("{}", rounded);
            if (e.kind == Kind::Toggle) {
                r.text += r.value >= 0.5 ? " (on)" : " (off)";
            } else if (e.kind == Kind::Choice) {
                if (const auto o = e.options.find(static_cast<int>(std::lround(r.value))); o != e.options.end()) {
                    r.text += " (" + o->second + ")";
                }
            } else if (!e.unit.empty()) {
                r.text += " " + e.unit;
            }
        }
        return r;
    }

    std::size_t OarValues::SharedCount() const {
        std::shared_lock lock(mutex_);
        std::size_t n = 0;
        for (const auto& [_, e] : dials_) n += e.shared ? 1 : 0;
        return n;
    }

} // namespace ConditionEngine::Dials
