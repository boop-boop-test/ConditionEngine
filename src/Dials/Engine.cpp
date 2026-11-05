#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace ConditionEngine::Dials {

    namespace {
        constexpr std::uint32_t kSaveVersion = 1;
        constexpr std::size_t kLogSize = 200;

        // ── Byte streams for Save / Load ─────────────────────────────────────
        class Writer {
        public:
            std::vector<std::uint8_t> bytes;
            template <class T>
            void Put(T v) {
                const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
                bytes.insert(bytes.end(), p, p + sizeof(T));
            }
            void PutString(const std::string& s) {
                Put(static_cast<std::uint32_t>(s.size()));
                bytes.insert(bytes.end(), s.begin(), s.end());
            }
        };

        class Reader {
        public:
            explicit Reader(std::span<const std::uint8_t> d) : data(d) {}
            std::span<const std::uint8_t> data;
            std::size_t pos = 0;
            bool ok = true;
            template <class T>
            T Get() {
                T v{};
                if (pos + sizeof(T) > data.size()) {
                    ok = false;
                    return v;
                }
                std::memcpy(&v, data.data() + pos, sizeof(T));
                pos += sizeof(T);
                return v;
            }
            std::string GetString() {
                const auto n = Get<std::uint32_t>();
                if (!ok || n > 4096 || pos + n > data.size()) {
                    ok = false;
                    return {};
                }
                std::string s(reinterpret_cast<const char*>(data.data() + pos), n);
                pos += n;
                return s;
            }
        };

        std::string ExprError(const std::string& where, const std::string& field, const std::string& source,
                              const std::string& error) {
            return where + ": " + field + " '" + source + "': " + (error.empty() ? "does not compile" : error);
        }
    }

    Engine::Engine(Host& host) : host_(host) {}

    Engine::~Engine() {
        for (auto& d : dials_) ReleaseHandles(*d);
    }

    // ── Definitions ──────────────────────────────────────────────────────────

    void Engine::ReleaseHandles(Dial& dial) {
        if (dial.track) host_.ReleaseCondition(dial.track);
        dial.track = 0;
        for (auto& r : dial.rules) {
            if (r.condition) host_.ReleaseCondition(r.condition);
            if (r.onlyIf) host_.ReleaseCondition(r.onlyIf);
            if (r.otherIf) host_.ReleaseCondition(r.otherIf);
            if (r.value) host_.ReleaseValue(r.value);
            r = {};
        }
    }

    bool Engine::Compile(Dial& dial, std::vector<std::string>& errors) {
        const auto before = errors.size();
        const auto& def = dial.def;
        std::string error;
        if (def.scope == Scope::Actor && !def.track.empty()) {
            dial.track = host_.CompileCondition(def.track, error);
            if (!dial.track) errors.push_back(ExprError("dial '" + def.id + "'", "track", def.track, error));
        }
        dial.rules.resize(def.rules.size());
        for (std::size_t i = 0; i < def.rules.size(); ++i) {
            const auto& rule = def.rules[i];
            auto& compiled = dial.rules[i];
            const auto where = "dial '" + def.id + "' rule " + std::to_string(i + 1);
            auto condition = [&](const std::string& source, Handle& out, const char* field) {
                if (source.empty()) return;
                error.clear();
                out = host_.CompileCondition(source, error);
                if (!out) errors.push_back(ExprError(where, field, source, error));
            };
            if (rule.trigger == TriggerKind::When || rule.trigger == TriggerKind::While) {
                condition(rule.condition, compiled.condition, rule.trigger == TriggerKind::When ? "when" : "while");
            }
            condition(rule.onlyIf, compiled.onlyIf, "if");
            condition(rule.otherIf, compiled.otherIf, "otherIf");
            if (!rule.value.expr.empty()) {
                error.clear();
                compiled.value = host_.CompileValue(rule.value.expr, error);
                if (!compiled.value) errors.push_back(ExprError(where, std::string(ActionName(rule.action)), rule.value.expr, error));
            }
        }
        return errors.size() == before;
    }

    Engine::Dial* Engine::FindDial(const std::string& id) {
        auto it = byId_.find(Lower(id));
        return it == byId_.end() ? nullptr : it->second;
    }

    const Engine::Dial* Engine::FindDial(const std::string& id) const {
        auto it = byId_.find(Lower(id));
        return it == byId_.end() ? nullptr : it->second;
    }

    DefineResult Engine::Define(DialDef def) {
        std::lock_guard lock(mutex_);
        DefineResult result;
        if (!IsValidId(def.id)) {
            result.errors.push_back("'" + def.id + "' is not a valid dial name");
            return result;
        }
        auto fresh = std::make_unique<Dial>();
        fresh->def = std::move(def);
        if (!Compile(*fresh, result.errors)) {
            ReleaseHandles(*fresh);
            return result;
        }
        const auto key = Lower(fresh->def.id);
        Dial* dial = FindDial(fresh->def.id);
        if (dial) {
            // Replace: keep the values, clamp them to the new range
            ReleaseHandles(*dial);
            dial->def = std::move(fresh->def);
            dial->track = fresh->track;
            dial->rules = std::move(fresh->rules);
            dial->trackCache.clear();
            dial->tracked.clear();
            if (dial->def.scope == Scope::Global) {
                // A dial that changed scope starts over
                for (auto it = dial->cells.begin(); it != dial->cells.end();) {
                    it = it->first != 0 ? dial->cells.erase(it) : std::next(it);
                }
            } else {
                dial->cells.erase(0);
            }
            for (auto& [_, cell] : dial->cells) cell.raw = dial->def.Clamp(cell.raw);
            std::erase_if(pending_, [&](const Pending& p) {
                return IEquals(p.id, dial->def.id) && p.rule >= dial->def.rules.size();
            });
        } else {
            dials_.push_back(std::move(fresh));
            dial = dials_.back().get();
            byId_[key] = dial;
            if (auto it = orphans_.find(key); it != orphans_.end()) {
                for (auto& [actor, cell] : it->second) {
                    if ((dial->def.scope == Scope::Global) != (actor == 0)) continue;
                    cell.raw = dial->def.Clamp(cell.raw);
                    dial->cells[actor] = cell;
                }
                orphans_.erase(it);
            }
        }
        // Every value is published again (the game layer re-registered the value)
        for (auto& [actor, cell] : dial->cells) {
            const double exposed = dial->def.Exposed(cell.raw);
            changes_.push_back({dial->def.id, actor, exposed, cell.published, "defined", false});
            cell.published = exposed;
            cell.publishedOnce = true;
        }
        result.ok = true;
        return result;
    }

    bool Engine::Remove(const std::string& id) {
        std::lock_guard lock(mutex_);
        Dial* dial = FindDial(id);
        if (!dial) return false;
        const auto key = Lower(dial->def.id);
        if (!dial->cells.empty()) orphans_[key] = std::move(dial->cells);
        ReleaseHandles(*dial);
        std::erase_if(pending_, [&](const Pending& p) { return IEquals(p.id, id); });
        byId_.erase(key);
        std::erase_if(dials_, [&](const auto& d) { return d.get() == dial; });
        return true;
    }

    std::size_t Engine::RemoveSource(const std::string& source) {
        std::vector<std::string> ids;
        {
            std::lock_guard lock(mutex_);
            for (const auto& d : dials_) {
                if (IEquals(d->def.source, source)) ids.push_back(d->def.id);
            }
        }
        for (const auto& id : ids) Remove(id);
        return ids.size();
    }

    std::vector<DialDef> Engine::Definitions() const {
        std::lock_guard lock(mutex_);
        std::vector<DialDef> out;
        for (const auto& d : dials_) out.push_back(d->def);
        return out;
    }

    std::optional<DialDef> Engine::Find(const std::string& id) const {
        std::lock_guard lock(mutex_);
        const Dial* dial = FindDial(id);
        if (!dial) return std::nullopt;
        return dial->def;
    }

    bool Engine::Exists(const std::string& id) const {
        std::lock_guard lock(mutex_);
        return FindDial(id) != nullptr;
    }

    // ── Values ───────────────────────────────────────────────────────────────

    ActorKey Engine::CellKey(const Dial& dial, ActorKey actor) const {
        if (dial.def.scope == Scope::Global) return 0;
        return actor == 0 ? kPlayer : actor;
    }

    Engine::Cell& Engine::CellFor(Dial& dial, ActorKey key) {
        auto [it, added] = dial.cells.try_emplace(key);
        if (added) {
            it->second.raw = dial.def.defaultValue;
            it->second.published = dial.def.Exposed(dial.def.defaultValue);  // what ConditionEngine reads by default
            it->second.lastGameHours = gameClockKnown_ ? gameHours_ : -1;
        }
        return it->second;
    }

    double Engine::RawValue(const Dial& dial, ActorKey key) const {
        auto it = dial.cells.find(key);
        return it == dial.cells.end() ? dial.def.defaultValue : it->second.raw;
    }

    void Engine::Write(Dial& dial, ActorKey key, double raw, const std::string& cause, bool discrete) {
        const auto& def = dial.def;
        raw = def.Clamp(raw);
        if (discrete && def.integer) raw = std::round(raw);
        auto& cell = CellFor(dial, key);
        cell.raw = raw;
        const double exposed = def.Exposed(raw);
        const double previous = cell.published;
        if (exposed == previous) return;

        bool crossed = false;
        for (double t : def.thresholds) {
            if ((previous < t && exposed >= t) || (previous >= t && exposed < t)) crossed = true;
        }
        const bool bound = (exposed == def.min || exposed == def.max);
        const bool publish = discrete || crossed || bound || std::fabs(exposed - previous) >= def.Precision() - 1e-9;
        if (!publish) return;

        const bool notify = discrete || crossed || bound;
        cell.published = exposed;
        cell.publishedOnce = true;
        changes_.push_back({def.id, key, exposed, previous, cause, notify});
        if (notify) {
            log_.push_back({now_, def.id, key, previous, exposed, cause});
            while (log_.size() > kLogSize) log_.pop_front();
        }
    }

    std::optional<double> Engine::Get(const std::string& id, ActorKey actor) const {
        std::lock_guard lock(mutex_);
        const Dial* dial = FindDial(id);
        if (!dial) return std::nullopt;
        return dial->def.Exposed(RawValue(*dial, CellKey(*dial, actor)));
    }

    bool Engine::Set(const std::string& id, ActorKey actor, double value, const std::string& cause) {
        std::lock_guard lock(mutex_);
        Dial* dial = FindDial(id);
        if (!dial) return false;
        Write(*dial, CellKey(*dial, actor), value, cause, true);
        return true;
    }

    bool Engine::Add(const std::string& id, ActorKey actor, double delta, const std::string& cause) {
        std::lock_guard lock(mutex_);
        Dial* dial = FindDial(id);
        if (!dial) return false;
        const auto key = CellKey(*dial, actor);
        Write(*dial, key, RawValue(*dial, key) + delta, cause, true);
        return true;
    }

    bool Engine::Reset(const std::string& id, ActorKey actor, const std::string& cause) {
        std::lock_guard lock(mutex_);
        Dial* dial = FindDial(id);
        if (!dial) return false;
        Write(*dial, CellKey(*dial, actor), dial->def.defaultValue, cause, true);
        return true;
    }

    namespace {
        double ToggleOf(const DialDef& def, double raw) {
            return def.Exposed(raw) > (def.min + def.max) / 2.0 ? def.min : def.max;
        }

        double CycleOf(const DialDef& def, double raw, int step) {
            const double current = def.Exposed(raw);
            if (def.kind == Kind::Choice && !def.options.empty()) {
                std::vector<int> keys;
                for (const auto& [v, _] : def.options) keys.push_back(v);
                const int n = static_cast<int>(keys.size());
                int pos = 0;
                for (int i = 0; i < n; ++i) {
                    if (keys[i] <= current) pos = i;
                }
                const int next = ((pos + step) % n + n) % n;
                return keys[next];
            }
            double v = std::round(current) + step;
            if (v > def.max) v = def.min;
            if (v < def.min) v = def.max;
            return v;
        }
    }

    bool Engine::Toggle(const std::string& id, ActorKey actor, const std::string& cause) {
        std::lock_guard lock(mutex_);
        Dial* dial = FindDial(id);
        if (!dial) return false;
        const auto key = CellKey(*dial, actor);
        Write(*dial, key, ToggleOf(dial->def, RawValue(*dial, key)), cause, true);
        return true;
    }

    bool Engine::Cycle(const std::string& id, ActorKey actor, int step, const std::string& cause) {
        std::lock_guard lock(mutex_);
        Dial* dial = FindDial(id);
        if (!dial) return false;
        const auto key = CellKey(*dial, actor);
        Write(*dial, key, CycleOf(dial->def, RawValue(*dial, key), step), cause, true);
        return true;
    }

    std::vector<std::pair<ActorKey, double>> Engine::Values(const std::string& id) const {
        std::lock_guard lock(mutex_);
        std::vector<std::pair<ActorKey, double>> out;
        const Dial* dial = FindDial(id);
        if (!dial) return out;
        for (const auto& [actor, cell] : dial->cells) out.emplace_back(actor, dial->def.Exposed(cell.raw));
        std::sort(out.begin(), out.end());
        return out;
    }

    std::vector<ActorKey> Engine::TrackedActors(const std::string& id) const {
        std::lock_guard lock(mutex_);
        const Dial* dial = FindDial(id);
        return dial ? dial->tracked : std::vector<ActorKey>{};
    }

    // ── Rules ────────────────────────────────────────────────────────────────

    std::string Engine::CauseOf(const Dial& dial, std::size_t index) const {
        const auto& rule = dial.def.rules[index];
        if (!rule.label.empty()) return rule.label;
        std::string what;
        switch (rule.trigger) {
            case TriggerKind::Game: what = "on " + std::string(GameEventName(rule.game)); break;
            case TriggerKind::Event: what = "event " + rule.event; break;
            case TriggerKind::When: what = "when " + rule.condition; break;
            case TriggerKind::While: what = rule.condition.empty() ? "always" : "while " + rule.condition; break;
            case TriggerKind::Every: what = "every " + std::to_string(rule.every); break;
            case TriggerKind::Key: what = "key " + KeyName(rule.key); break;
        }
        return "rule " + std::to_string(index + 1) + " (" + what + ")";
    }

    bool Engine::IsTracked(Dial& dial, ActorKey actor) {
        if (dial.def.scope == Scope::Global) return true;
        if (actor == 0) actor = kPlayer;
        if (dial.def.track.empty()) return actor == kPlayer;
        if (!dial.track) return false;
        auto it = dial.trackCache.find(actor);
        if (it != dial.trackCache.end() && now_ - it->second.second < trackInterval_) return it->second.first;
        const bool tracked = host_.Evaluate(dial.track, actor).value_or(false);
        dial.trackCache[actor] = {tracked, now_};
        return tracked;
    }

    ActorKey Engine::ActorFor(const Rule& rule, const EventInfo& info) const {
        switch (rule.actor) {
            case Role::Player: return kPlayer;
            case Role::Other: return info.other;
            case Role::Crosshair: return info.crosshair;
            default: return info.actor ? info.actor : kPlayer;
        }
    }

    bool Engine::Allowed(Dial& dial, std::size_t index, ActorKey subject, const EventInfo* info, bool rollChance) {
        const auto& rule = dial.def.rules[index];
        auto& compiled = dial.rules[index];
        if (info) {
            if (!rule.form.empty() && (!info->form || !host_.FormMatches(rule.form, info->form))) return false;
            if (!rule.keyword.empty() && (!info->form || !host_.HasKeyword(rule.keyword, info->form))) return false;
            if (!rule.text.empty() && !IEquals(rule.text, info->text)) return false;
            if (IEquals(rule.other, "player") && info->other != kPlayer) return false;
            if (IEquals(rule.other, "!player") && info->other == kPlayer) return false;
            if (compiled.otherIf && (!info->other || !host_.Evaluate(compiled.otherIf, info->other).value_or(false))) {
                return false;
            }
        }
        if (!IsTracked(dial, subject)) return false;
        if (compiled.onlyIf && !host_.Evaluate(compiled.onlyIf, subject).value_or(false)) return false;
        if (rule.cooldown > 0) {
            const auto key = CellKey(dial, subject);
            auto it = compiled.state.find(key);
            if (it != compiled.state.end()) {
                const double last = rule.clock == Clock::Game ? it->second.lastFiredGame : it->second.lastFiredReal;
                if (ClockNow(rule.clock) - last < rule.cooldown) return false;
            }
        }
        if (rollChance && rule.chance < 1.0 && host_.Random01() >= rule.chance) return false;
        return true;
    }

    bool Engine::Eligible(const std::string& id, std::size_t index, ActorKey actor) {
        std::lock_guard lock(mutex_);
        Dial* dial = FindDial(id);
        if (!dial || !dial->def.enabled || index >= dial->def.rules.size() || !dial->def.rules[index].enabled) return false;
        return Allowed(*dial, index, actor ? actor : kPlayer, nullptr, false);
    }

    std::optional<double> Engine::AmountOf(const CompiledRule& compiled, const Rule& rule, ActorKey subject) {
        if (rule.value.constant) return rule.value.constant;
        if (!compiled.value) return std::nullopt;
        auto v = host_.EvaluateValue(compiled.value, subject);
        if (v && !std::isfinite(*v)) return std::nullopt;
        return v;
    }

    void Engine::Apply(Dial& dial, std::size_t index, ActorKey subject, const EventInfo* info) {
        const auto& rule = dial.def.rules[index];
        auto& compiled = dial.rules[index];
        const auto& def = dial.def;
        const auto key = CellKey(dial, subject);
        host_.SetEventNumber(info ? info->number : 0.0);

        std::optional<double> amount;
        if (rule.action != ActionKind::Toggle && rule.action != ActionKind::Reset) {
            amount = AmountOf(compiled, rule, subject);
            if (!amount) return;  // the expression is unavailable for this actor right now
        }
        const double old = RawValue(dial, key);
        double next = old;
        switch (rule.action) {
            case ActionKind::Set: next = *amount; break;
            case ActionKind::Add: next = old + *amount; break;
            case ActionKind::Multiply: next = old * *amount; break;
            case ActionKind::Toggle: next = ToggleOf(def, old); break;
            case ActionKind::Reset: next = def.defaultValue; break;
            case ActionKind::Cycle: next = CycleOf(def, old, static_cast<int>(std::lround(*amount))); break;
            default: return;  // rate / approach run from Tick
        }
        const auto cause = CauseOf(dial, index);
        Write(dial, key, next, cause, true);
        if (!rule.tell.empty()) {
            const double exposed = def.Exposed(RawValue(dial, key));
            const auto option = def.OptionLabel(exposed);
            notes_.push_back({def.id, subject ? subject : kPlayer, rule.tell, rule.tellNarration, exposed,
                              option.empty() ? def.label : option});
        }
        auto& st = compiled.state[key];
        st.lastFiredReal = now_;
        st.lastFiredGame = gameHours_;

        if (rule.forDuration > 0) {
            const double due = ClockNow(rule.clock) + rule.forDuration;
            if (rule.action == ActionKind::Add) {
                const double applied = RawValue(dial, key) - old;
                if (applied != 0) pending_.push_back({def.id, key, index, false, applied, rule.clock, due});
            } else {
                auto it = std::find_if(pending_.begin(), pending_.end(), [&](const Pending& p) {
                    return p.restore && p.rule == index && p.actor == key && IEquals(p.id, def.id);
                });
                if (it != pending_.end()) {
                    it->due = due;  // fired again: keep the original value, end later
                } else {
                    pending_.push_back({def.id, key, index, true, old, rule.clock, due});
                }
            }
        }
    }

    void Engine::RunEvent(Dial& dial, std::size_t index, const EventInfo& info) {
        const ActorKey subject = ActorFor(dial.def.rules[index], info);
        if (!subject) return;  // other / crosshair with nobody there
        if (Allowed(dial, index, subject, &info)) Apply(dial, index, subject, &info);
    }

    bool Engine::FireRule(const std::string& id, std::size_t index, ActorKey actor) {
        std::lock_guard lock(mutex_);
        Dial* dial = FindDial(id);
        if (!dial || index >= dial->def.rules.size()) return false;
        const auto& rule = dial->def.rules[index];
        const ActorKey subject = actor ? actor : kPlayer;
        if (!rule.IsContinuous()) {
            EventInfo info;
            info.actor = subject;
            Apply(*dial, index, subject, &info);
            return true;
        }
        // A continuous rule: one second (or one game hour) of it
        auto& compiled = dial->rules[index];
        const auto key = CellKey(*dial, subject);
        host_.SetEventNumber(0);
        const auto amount = AmountOf(compiled, rule, subject);
        if (!amount) return true;
        const double raw = RawValue(*dial, key);
        double next = raw;
        if (rule.action == ActionKind::Rate) next = raw + *amount;
        else if (rule.action == ActionKind::Approach) next = raw + (*amount - raw) * (1.0 - std::pow(0.5, 1.0 / rule.halfLife));
        else next = *amount;
        Write(*dial, key, next, CauseOf(*dial, index) + " (test)", true);
        return true;
    }

    void Engine::ExpirePending() {
        if (pending_.empty()) return;
        std::vector<Pending> due;
        std::erase_if(pending_, [&](const Pending& p) {
            if (ClockNow(p.clock) < p.due) return false;
            due.push_back(p);
            return true;
        });
        for (const auto& p : due) {
            Dial* dial = FindDial(p.id);
            if (!dial) continue;
            const auto cause = "end of " + (p.rule < dial->def.rules.size() ? CauseOf(*dial, p.rule) : std::string("a rule"));
            if (p.restore) {
                Write(*dial, p.actor, p.amount, cause, true);
            } else {
                Write(*dial, p.actor, RawValue(*dial, p.actor) - p.amount, cause, true);
            }
        }
    }

    void Engine::Tick(double realDt, double gameHours, const std::vector<ActorKey>& loaded) {
        std::lock_guard lock(mutex_);
        realDt = std::max(0.0, realDt);
        now_ += realDt;
        const double previousGame = gameClockKnown_ ? gameHours_ : gameHours;
        const double tickGameDt = std::clamp(gameHours - previousGame, 0.0, maxCatchUpHours_);
        gameHours_ = gameHours;
        gameClockKnown_ = true;
        loaded_ = loaded;

        ExpirePending();

        std::unordered_set<ActorKey> loadedSet(loaded.begin(), loaded.end());
        for (auto& dialPtr : dials_) {
            Dial& dial = *dialPtr;
            if (!dial.def.enabled) continue;
            const auto& def = dial.def;

            // Who the dial runs for this tick
            std::vector<ActorKey> subjects;
            if (def.scope == Scope::Global) {
                subjects.push_back(0);
            } else {
                for (ActorKey a : loaded) {
                    if (IsTracked(dial, a)) subjects.push_back(a);
                }
                std::erase_if(dial.trackCache, [&](const auto& e) { return !loadedSet.contains(e.first); });
            }
            dial.tracked = def.scope == Scope::Global ? std::vector<ActorKey>{} : subjects;
            std::unordered_set<ActorKey> keys;
            for (ActorKey s : subjects) keys.insert(CellKey(dial, s));
            for (auto& compiled : dial.rules) {
                // An actor that left starts a fresh "when" baseline when it comes back
                std::erase_if(compiled.state, [&](const auto& e) {
                    return !keys.contains(e.first) && e.second.lastFiredReal < now_ - 600.0;
                });
                for (auto& [key, st] : compiled.state) {
                    if (!keys.contains(key)) st.last.reset();
                }
            }

            // Timers fire for every subject at once
            std::vector<bool> everyDue(def.rules.size(), false);
            for (std::size_t i = 0; i < def.rules.size(); ++i) {
                const auto& rule = def.rules[i];
                if (!rule.enabled || rule.trigger != TriggerKind::Every) continue;
                auto& compiled = dial.rules[i];
                const double clockNow = ClockNow(rule.clock);
                if (compiled.nextEvery < 0) {
                    compiled.nextEvery = clockNow + rule.every;
                } else if (clockNow >= compiled.nextEvery) {
                    everyDue[i] = true;
                    compiled.nextEvery = clockNow + rule.every;
                }
            }

            for (ActorKey subject : subjects) {
                const auto key = CellKey(dial, subject);
                auto cellIt = dial.cells.find(key);
                double gameDt = tickGameDt;
                if (cellIt != dial.cells.end() && cellIt->second.lastGameHours >= 0) {
                    gameDt = std::clamp(gameHours - cellIt->second.lastGameHours, 0.0, maxCatchUpHours_);
                }

                for (std::size_t i = 0; i < def.rules.size(); ++i) {
                    const auto& rule = def.rules[i];
                    if (!rule.enabled) continue;
                    auto& compiled = dial.rules[i];
                    switch (rule.trigger) {
                        case TriggerKind::While: {
                            if (compiled.condition && !host_.Evaluate(compiled.condition, subject).value_or(false)) break;
                            if (compiled.onlyIf && !host_.Evaluate(compiled.onlyIf, subject).value_or(false)) break;
                            const double dt = rule.clock == Clock::Game ? gameDt : realDt;
                            if (dt <= 0 && rule.action != ActionKind::Set) break;
                            host_.SetEventNumber(0);
                            const auto amount = AmountOf(compiled, rule, subject);
                            if (!amount) break;
                            const double raw = RawValue(dial, key);
                            const auto cause = CauseOf(dial, i);
                            if (rule.action == ActionKind::Rate) {
                                Write(dial, key, raw + *amount * dt, cause, false);
                            } else if (rule.action == ActionKind::Approach) {
                                const double k = 1.0 - std::pow(0.5, dt / rule.halfLife);
                                double next = raw + (*amount - raw) * k;
                                if (std::fabs(*amount - next) < (def.max - def.min) * 1e-6) next = *amount;
                                Write(dial, key, next, cause, false);
                            } else {
                                Write(dial, key, *amount, cause, true);
                            }
                            break;
                        }
                        case TriggerKind::When: {
                            const auto current = host_.Evaluate(compiled.condition, subject);
                            if (!current) break;  // unavailable: keep the last result
                            auto& st = compiled.state[key];
                            const auto last = st.last;
                            st.last = *current;
                            if (!last || *last == *current) break;
                            const bool fire = rule.edge == Edge::Both || (rule.edge == Edge::Rising) == *current;
                            if (fire && Allowed(dial, i, subject, nullptr)) Apply(dial, i, subject, nullptr);
                            break;
                        }
                        case TriggerKind::Every:
                            if (everyDue[i] && Allowed(dial, i, subject, nullptr)) Apply(dial, i, subject, nullptr);
                            break;
                        default: break;
                    }
                }
                if (auto it = dial.cells.find(key); it != dial.cells.end()) it->second.lastGameHours = gameHours;
            }
        }
    }

    void Engine::OnGameEvent(GameEvent event, const EventInfo& info) {
        std::lock_guard lock(mutex_);
        for (auto& dial : dials_) {
            if (!dial->def.enabled) continue;
            for (std::size_t i = 0; i < dial->def.rules.size(); ++i) {
                const auto& rule = dial->def.rules[i];
                if (rule.enabled && rule.trigger == TriggerKind::Game && rule.game == event) RunEvent(*dial, i, info);
            }
        }
    }

    void Engine::OnModEvent(const std::string& name, const EventInfo& info) {
        std::lock_guard lock(mutex_);
        for (auto& dial : dials_) {
            if (!dial->def.enabled) continue;
            for (std::size_t i = 0; i < dial->def.rules.size(); ++i) {
                const auto& rule = dial->def.rules[i];
                if (rule.enabled && rule.trigger == TriggerKind::Event && IEquals(rule.event, name)) {
                    // A dial does not hear its own change notices (no feedback loop)
                    if (IEquals(name, "Dials_Changed") && IEquals(info.text, dial->def.id)) continue;
                    RunEvent(*dial, i, info);
                }
            }
        }
    }

    void Engine::OnKey(std::uint32_t scanCode, ActorKey crosshair) {
        std::lock_guard lock(mutex_);
        EventInfo info;
        info.actor = kPlayer;
        info.crosshair = crosshair;
        for (auto& dial : dials_) {
            if (!dial->def.enabled) continue;
            for (std::size_t i = 0; i < dial->def.rules.size(); ++i) {
                const auto& rule = dial->def.rules[i];
                if (rule.enabled && rule.trigger == TriggerKind::Key && rule.key == scanCode) RunEvent(*dial, i, info);
            }
        }
    }

    // ── Sessions ─────────────────────────────────────────────────────────────

    void Engine::ClearValues() {
        std::lock_guard lock(mutex_);
        for (auto& dial : dials_) {
            dial->cells.clear();
            dial->trackCache.clear();
            dial->tracked.clear();
            for (auto& r : dial->rules) {
                r.state.clear();
                r.nextEvery = -1;
            }
        }
        orphans_.clear();
        pending_.clear();
        changes_.clear();
        notes_.clear();
        gameClockKnown_ = false;
    }

    void Engine::OnSessionStart() {
        {
            std::lock_guard lock(mutex_);
            gameClockKnown_ = false;
            for (auto& dial : dials_) {
                dial->trackCache.clear();
                for (auto& r : dial->rules) {
                    for (auto& [_, st] : r.state) st.last.reset();
                    r.nextEvery = -1;
                }
                for (auto& [actor, cell] : dial->cells) {
                    const double exposed = dial->def.Exposed(cell.raw);
                    changes_.push_back({dial->def.id, actor, exposed, dial->def.Exposed(dial->def.defaultValue), "loaded", false});
                    cell.published = exposed;
                    cell.publishedOnce = true;
                }
            }
        }
        EventInfo info;
        info.actor = kPlayer;
        OnGameEvent(GameEvent::Load, info);
    }

    std::vector<std::uint8_t> Engine::Save() const {
        std::lock_guard lock(mutex_);
        Writer w;
        w.Put(kSaveVersion);
        auto putCells = [&](const std::string& id, const std::unordered_map<ActorKey, Cell>& cells) {
            w.PutString(id);
            w.Put(static_cast<std::uint32_t>(cells.size()));
            for (const auto& [actor, cell] : cells) {
                w.Put(actor);
                w.Put(cell.raw);
                w.Put(cell.lastGameHours);
            }
        };
        std::uint32_t count = 0;
        for (const auto& d : dials_) count += d->def.persist && !d->cells.empty() ? 1 : 0;
        count += static_cast<std::uint32_t>(orphans_.size());
        w.Put(count);
        for (const auto& d : dials_) {
            if (d->def.persist && !d->cells.empty()) putCells(d->def.id, d->cells);
        }
        for (const auto& [id, cells] : orphans_) putCells(id, cells);

        std::vector<const Pending*> saved;
        for (const auto& p : pending_) {
            const Dial* dial = FindDial(p.id);
            if (dial && dial->def.persist) saved.push_back(&p);
        }
        w.Put(static_cast<std::uint32_t>(saved.size()));
        for (const auto* p : saved) {
            w.PutString(p->id);
            w.Put(p->actor);
            w.Put(static_cast<std::uint32_t>(p->rule));
            w.Put(static_cast<std::uint8_t>(p->restore ? 1 : 0));
            w.Put(p->amount);
            w.Put(static_cast<std::uint8_t>(p->clock == Clock::Game ? 1 : 0));
            // Real clocks restart every session: save what is left; game clocks: the due time
            w.Put(p->clock == Clock::Game ? p->due : p->due - now_);
        }
        return std::move(w.bytes);
    }

    bool Engine::Load(std::span<const std::uint8_t> data, const FormResolver& resolve) {
        std::lock_guard lock(mutex_);
        Reader r(data);
        const auto version = r.Get<std::uint32_t>();
        if (!r.ok || version != kSaveVersion) return false;
        auto mapKey = [&](ActorKey key) -> std::optional<ActorKey> {
            if (key == 0) return 0u;
            return resolve ? resolve(key) : std::optional<ActorKey>(key);
        };
        const auto count = r.Get<std::uint32_t>();
        for (std::uint32_t i = 0; r.ok && i < count; ++i) {
            const auto id = r.GetString();
            const auto cells = r.Get<std::uint32_t>();
            if (!r.ok || cells > 1'000'000) return false;
            Dial* dial = FindDial(id);
            for (std::uint32_t c = 0; r.ok && c < cells; ++c) {
                const auto actor = r.Get<ActorKey>();
                Cell cell;
                cell.raw = r.Get<double>();
                cell.lastGameHours = r.Get<double>();
                const auto key = mapKey(actor);
                if (!r.ok || !key) continue;
                if (dial) {
                    if ((dial->def.scope == Scope::Global) != (*key == 0)) continue;
                    cell.raw = dial->def.Clamp(cell.raw);
                    cell.published = dial->def.Exposed(dial->def.defaultValue);
                    dial->cells[*key] = cell;
                } else {
                    orphans_[Lower(id)][*key] = cell;
                }
            }
        }
        const auto pendingCount = r.Get<std::uint32_t>();
        for (std::uint32_t i = 0; r.ok && i < pendingCount; ++i) {
            Pending p;
            p.id = r.GetString();
            const auto actor = r.Get<ActorKey>();
            p.rule = r.Get<std::uint32_t>();
            p.restore = r.Get<std::uint8_t>() != 0;
            p.amount = r.Get<double>();
            p.clock = r.Get<std::uint8_t>() ? Clock::Game : Clock::Real;
            const double due = r.Get<double>();
            const auto key = mapKey(actor);
            if (!r.ok || !key || !FindDial(p.id)) continue;
            p.actor = *key;
            p.due = p.clock == Clock::Game ? due : now_ + due;
            pending_.push_back(std::move(p));
        }
        return r.ok;
    }

    // ── Outputs ──────────────────────────────────────────────────────────────

    std::vector<Change> Engine::TakeChanges() {
        std::lock_guard lock(mutex_);
        std::vector<Change> out;
        out.swap(changes_);
        return out;
    }

    std::vector<Note> Engine::TakeNotes() {
        std::lock_guard lock(mutex_);
        std::vector<Note> out;
        out.swap(notes_);
        return out;
    }

    bool Engine::HasChanges() const {
        std::lock_guard lock(mutex_);
        return !changes_.empty();
    }

    std::vector<LogEntry> Engine::RecentLog(std::size_t max) const {
        std::lock_guard lock(mutex_);
        const auto n = std::min(max, log_.size());
        return {log_.end() - static_cast<std::ptrdiff_t>(n), log_.end()};
    }

    double Engine::Now() const {
        std::lock_guard lock(mutex_);
        return now_;
    }

} // namespace ConditionEngine::Dials
