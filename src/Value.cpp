#include "ConditionEngine/Value.h"
#include "ConditionEngine/Availability.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Expression.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Stats.h"
#include "ConditionEngine/Values/PushedValues.h"
#include "ConditionEngine/Values/ValueNodes.h"

#include <array>
#include <charconv>
#include <cmath>

namespace ConditionEngine {

    namespace {
        using json = nlohmann::json;

        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        bool IEquals(std::string_view a, std::string_view b) {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                       return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
                   });
        }

        int Severity(ValueStatus status) {
            switch (status) {
                case ValueStatus::Ok: return 0;
                case ValueStatus::Pending: return 1;
                case ValueStatus::Unavailable: return 2;
                default: return 3;
            }
        }

        // Dependencies that read the same thing for every actor
        constexpr auto kWorldDependencies = EventDependency::Time | EventDependency::Date | EventDependency::Weather |
                                            EventDependency::GlobalVariable | EventDependency::Quest |
                                            EventDependency::Value | EventDependency::Availability;

        bool DependsOnActor(EventDependency deps) {
            return (static_cast<std::uint32_t>(deps) & ~static_cast<std::uint32_t>(kWorldDependencies)) != 0;
        }

        // Last good value per (named value, actor) for graceSeconds
        struct LastGood {
            double value = 0.0;
            ValueResult::Clock::time_point at;
        };
        std::mutex g_lastGoodMutex;
        std::map<std::pair<std::string, RE::FormID>, LastGood> g_lastGood;

        // Guards GetEventDependencies / ReadsActor against $a -> $b -> $a cycles
        thread_local std::unordered_set<std::string> t_resolving;

        struct ResolvingGuard {
            std::string key;
            bool entered;
            explicit ResolvingGuard(std::string k) : key(std::move(k)), entered(t_resolving.insert(key).second) {}
            ~ResolvingGuard() { if (entered) t_resolving.erase(key); }
        };

        std::optional<double> ParseNumber(std::string_view text) {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
            if (text.empty()) return std::nullopt;
            double value = 0.0;
            auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (ec != std::errc() || end != text.data() + text.size()) return std::nullopt;
            return value;
        }
    }

    // ── Results ──────────────────────────────────────────────────────────────

    const char* ValueStatusName(ValueStatus status) {
        switch (status) {
            case ValueStatus::Ok: return "ok";
            case ValueStatus::Pending: return "pending";
            case ValueStatus::Unavailable: return "unavailable";
            default: return "error";
        }
    }

    void MergeValueStatus(ValueResult& into, const ValueResult& other) {
        if (Severity(other.status) > Severity(into.status)) into.status = other.status;
        if (!other.reason.empty()) {
            if (!into.reason.empty()) into.reason += "; ";
            into.reason += other.reason;
        }
        if (other.sampledAt && (!into.sampledAt || *other.sampledAt < *into.sampledAt)) into.sampledAt = other.sampledAt;
    }

    json ValueInfo::ToJson() const {
        json j{{"unit", unit}, {"integer", integer}};
        j["range"] = json::array({min ? json(*min) : json(nullptr), max ? json(*max) : json(nullptr)});
        return j;
    }

    // ── Value ────────────────────────────────────────────────────────────────

    Value::~Value() { delete availabilityMemo_.load(); }

    AvailabilityStatus Value::CheckAvailability() const {
        return detail::CachedCheck(availabilityMemo_, [this] { return Availability::Get().CheckValue(*this); });
    }

    bool Value::ReadsActor() const { return DependsOnActor(GetEventDependencies()); }

    ValueResult Value::ReadCounted(const EvaluationContext& context) const {
        if (!Stats::Enabled()) return Read(context);
        Stats::Scope scope(statsSlot, true);
        return Read(context);
    }

    std::unique_ptr<Value> Value::FromJson(const json& j, std::string* error) {
        auto fail = [&](std::string message) -> std::unique_ptr<Value> {
            if (error) *error = std::move(message);
            return nullptr;
        };

        if (j.is_number()) return std::make_unique<LiteralValue>(j.get<double>());
        if (j.is_boolean()) return std::make_unique<LiteralValue>(j.get<bool>() ? 1.0 : 0.0);
        if (j.is_string()) {
            const auto& text = j.get_ref<const std::string&>();
            if (auto number = ParseNumber(text)) return std::make_unique<LiteralValue>(*number);
            if (text.find("${") != std::string::npos) return fail("unresolved parameter " + text);
            return fail("not a value: \"" + text + "\"");
        }
        if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) {
            return fail("a value must be a number or an object with a \"type\"");
        }

        const std::string type = j["type"].get<std::string>();

        if (IEquals(type, "Math")) {
            const std::string opName = j.contains("operator") && j["operator"].is_string() ? j["operator"].get<std::string>() : "";
            auto op = MathValue::ParseOp(opName);
            if (!op) return fail("unknown Math operator '" + opName + "'");
            std::vector<std::shared_ptr<const Value>> args;
            if (j.contains("args") && j["args"].is_array()) {
                for (const auto& argJson : j["args"]) {
                    auto arg = FromJson(argJson, error);
                    if (!arg) return nullptr;
                    args.push_back(std::move(arg));
                }
            }
            const auto arity = MathValue::Arity(*op);
            if (arity ? args.size() != *arity : args.size() < 2) {
                return fail(std::string(MathValue::OpName(*op)) + " takes " +
                            (arity ? std::to_string(*arity) : std::string("two or more")) + " argument(s)");
            }
            return std::make_unique<MathValue>(*op, std::move(args));
        }

        if (IEquals(type, "NamedValue")) {
            const std::string name = j.contains("name") && j["name"].is_string() ? j["name"].get<std::string>() : "";
            if (name.empty()) return fail("NamedValue has no name");
            return std::make_unique<NamedValueRef>(name);
        }

        if (IEquals(type, "Truth")) {
            auto condition = j.contains("condition") ? Condition::FromJson(j["condition"]) : nullptr;
            if (!condition) return fail("Truth has no valid condition");
            return std::make_unique<TruthValue>(std::shared_ptr<const Condition>(std::move(condition)));
        }

        const auto* info = Registry::Get().FindType(type);
        auto condition = Condition::FromJson(j);
        if (!condition) return fail("unknown type '" + type + "'");
        std::shared_ptr<const Condition> shared(std::move(condition));
        if (info && info->value) return std::make_unique<TypeValue>(info, std::move(shared));
        return std::make_unique<TruthValue>(std::move(shared));
    }

    ValueResult ReadValueAvailable(const Value& value, const EvaluationContext& context) {
        const auto status = value.CheckAvailability();
        if (!status.Ready()) {
            return ValueResult::Fail(status.state == AvailabilityState::Pending ? ValueStatus::Pending : ValueStatus::Unavailable,
                                     status.reason);
        }
        return value.Read(context);
    }

    // ── Literal ──────────────────────────────────────────────────────────────

    json LiteralValue::ToJson() const {
        double whole = 0.0;
        if (std::modf(value_, &whole) == 0.0 && std::abs(value_) < 9e15) return static_cast<std::int64_t>(value_);
        return value_;
    }

    // ── Math ─────────────────────────────────────────────────────────────────

    namespace {
        constexpr std::pair<MathValue::Op, const char*> kMathOps[] = {
            {MathValue::Op::Add, "+"},       {MathValue::Op::Sub, "-"},     {MathValue::Op::Mul, "*"},
            {MathValue::Op::Div, "/"},       {MathValue::Op::Neg, "neg"},   {MathValue::Op::Min, "min"},
            {MathValue::Op::Max, "max"},     {MathValue::Op::Clamp, "clamp"}, {MathValue::Op::Abs, "abs"},
            {MathValue::Op::Lerp, "lerp"},
        };
    }

    MathValue::MathValue(Op op, std::vector<std::shared_ptr<const Value>> args) : op_(op), args_(std::move(args)) {
        statsSlot = Stats::kMath;
    }

    std::optional<MathValue::Op> MathValue::ParseOp(std::string_view name) {
        for (const auto& [op, text] : kMathOps) {
            if (IEquals(name, text)) return op;
        }
        return std::nullopt;
    }

    const char* MathValue::OpName(Op op) {
        for (const auto& [candidate, text] : kMathOps) {
            if (candidate == op) return text;
        }
        return "?";
    }

    std::optional<std::size_t> MathValue::Arity(Op op) {
        switch (op) {
            case Op::Neg:
            case Op::Abs: return 1;
            case Op::Clamp:
            case Op::Lerp: return 3;
            case Op::Min:
            case Op::Max: return std::nullopt;
            default: return 2;
        }
    }

    ValueResult MathValue::Read(const EvaluationContext& context) const {
        ValueResult result;
        // Arguments in a fixed buffer: only min / max with more than 8 arguments allocate
        std::array<double, 8> fixed{};
        std::vector<double> spill;
        double* v = fixed.data();
        if (args_.size() > fixed.size()) {
            spill.resize(args_.size());
            v = spill.data();
        }
        for (std::size_t i = 0; i < args_.size(); ++i) {
            const auto& arg = args_[i];
            auto r = arg ? arg->ReadCounted(context) : ValueResult::Fail(ValueStatus::Error, "missing argument");
            if (!r.Ok() || r.sampledAt) MergeValueStatus(result, r);
            v[i] = r.value;
        }
        if (args_.empty()) return ValueResult::Fail(ValueStatus::Error, "no arguments");
        double* end = v + args_.size();

        double out = 0.0;
        switch (op_) {
            case Op::Add: out = v[0] + v[1]; break;
            case Op::Sub: out = v[0] - v[1]; break;
            case Op::Mul: out = v[0] * v[1]; break;
            case Op::Div: out = v[1] == 0.0 ? 0.0 : v[0] / v[1]; break;  // x / 0 reads 0
            case Op::Neg: out = -v[0]; break;
            case Op::Abs: out = std::abs(v[0]); break;
            case Op::Min: out = *std::min_element(v, end); break;
            case Op::Max: out = *std::max_element(v, end); break;
            case Op::Clamp: out = std::min(std::max(v[0], v[1]), v[2]); break;
            case Op::Lerp: out = v[0] + (v[1] - v[0]) * v[2]; break;
        }
        if (!std::isfinite(out)) {
            MergeValueStatus(result, ValueResult::Fail(ValueStatus::Error, "result is not a finite number"));
            out = 0.0;
        }
        result.value = out;
        return result;
    }

    EventDependency MathValue::GetEventDependencies() const {
        EventDependency deps = EventDependency::None;
        for (const auto& arg : args_) {
            if (arg) deps |= arg->GetEventDependencies();
        }
        return deps;
    }

    bool MathValue::ReadsActor() const {
        return std::any_of(args_.begin(), args_.end(), [](const auto& arg) { return arg && arg->ReadsActor(); });
    }

    json MathValue::ToJson() const {
        json args = json::array();
        for (const auto& arg : args_) args.push_back(arg ? arg->ToJson() : json(nullptr));
        return {{"type", "Math"}, {"operator", OpName(op_)}, {"args", std::move(args)}};
    }

    std::vector<std::string> MathValue::Validate() const {
        std::vector<std::string> errors;
        for (const auto& arg : args_) {
            if (!arg) {
                errors.push_back(std::string(OpName(op_)) + ": missing argument");
                continue;
            }
            for (auto& e : arg->Validate()) errors.push_back(std::move(e));
        }
        return errors;
    }

    // ── Named value reference ────────────────────────────────────────────────

    NamedValueRef::NamedValueRef(std::string name) : name_(std::move(name)), key_(Lower(name_)) {
        statsSlot = Stats::kNamedValue;
    }

    std::shared_ptr<const NamedValueDef> NamedValueRef::Bind() const {
        const auto revision = Availability::Get().Revision();
        std::lock_guard lock(bindMutex_);
        if (boundRevision_ != revision) {
            bound_ = Registry::Get().FindNamedValue(key_);
            boundRevision_ = revision;
        }
        return bound_;
    }

    namespace {
        ValueResult ReadDefinition(const NamedValueDef& def, const EvaluationContext& context);

        // A combined value's sources, highest priority first, then its own expression:
        // the first available one answers; a pending one holds the result pending
        ValueResult ReadProviders(const NamedValueDef& def, const std::vector<std::shared_ptr<const NamedValueDef>>& providers,
                                  const EvaluationContext& context) {
            std::string tried;
            auto note = [&](const std::string& name, const ValueResult& r) {
                if (!tried.empty()) tried += "; ";
                tried += name + ": " + (r.reason.empty() ? ValueStatusName(r.status) : r.reason);
            };
            for (const auto& source : providers) {
                auto r = ReadDefinition(*source, context);
                if (r.Ok()) {
                    r.from = source->name;
                    return r;
                }
                if (r.status == ValueStatus::Pending) {
                    return ValueResult::Fail(ValueStatus::Pending, "waiting for " + source->name +
                                                                       (r.reason.empty() ? "" : " (" + r.reason + ")"));
                }
                note(source->name, r);
            }
            if (def.value) {
                auto r = def.value->ReadCounted(context);
                if (r.Ok()) {
                    r.from = def.name + " (own expression)";
                    return r;
                }
                if (r.status == ValueStatus::Pending) return r;
                note(def.name + " (own expression)", r);
            }
            return ValueResult::Fail(ValueStatus::Unavailable,
                                     tried.empty() ? "no source provides " + def.name : "no source available (" + tried + ")");
        }

        ValueResult ReadDefinition(const NamedValueDef& def, const EvaluationContext& context) {
            auto& inProgress = context.valuesInProgress;
            if (std::find(inProgress.begin(), inProgress.end(), &def) != inProgress.end()) {
                return ValueResult::Fail(ValueStatus::Error, "circular named value: " + def.name);
            }
            inProgress.push_back(&def);
            struct Leave {
                const EvaluationContext& context;
                ~Leave() { context.valuesInProgress.pop_back(); }
            } leave{context};
            std::string lowered;
            const std::string& key = def.key.empty() ? (lowered = Lower(def.name)) : def.key;

            ValueResult result;
            const auto enabled = def.enabledWhen ? def.enabledWhen->EvaluateTriCounted(context) : TriResult::Of(true);
            const bool disabled = enabled.state == TriState::False;
            if (enabled.state == TriState::Pending) {
                // Undecided is not "disabled": report what it waits for
                result = ValueResult::Fail(ValueStatus::Pending, enabled.reason.empty() ? def.name + " is waiting" : enabled.reason);
            } else if (enabled.state == TriState::Unavailable) {
                result = ValueResult::Fail(ValueStatus::Unavailable, "enabledWhen cannot be read: " +
                                                                         (enabled.reason.empty() ? std::string("unknown") : enabled.reason));
            } else if (disabled) {
                result = ValueResult::Fail(ValueStatus::Unavailable, def.name + " is not enabled for this actor");
            } else if (auto pushed = PushedValues::Get().Read(def, context.GetActor())) {
                result = std::move(*pushed);
            } else if (auto providers = Registry::Get().ProvidersOf(key); !providers.empty()) {
                result = ReadProviders(def, providers, context);
            } else if (def.value) {
                result = def.value->ReadCounted(context);
            } else if (def.pushed) {
                result = ValueResult::Fail(ValueStatus::Unavailable, PushedValues::Get().WhyMissing(def, context.GetActor()));
            } else {
                result = ValueResult::Fail(ValueStatus::Unavailable, "no source provides " + def.name);
            }
            if (def.graceSeconds > 0.0 && !disabled) {  // grace covers failed reads, not disabled actors
                const auto slot = std::make_pair(key, def.global ? RE::FormID(0) : PushedValues::ActorKey(context.GetActor()));
                std::lock_guard lock(g_lastGoodMutex);
                if (result.Ok()) {
                    g_lastGood[slot] = {result.value, result.sampledAt.value_or(ValueResult::Clock::now())};
                } else if (auto it = g_lastGood.find(slot); it != g_lastGood.end() &&
                           ValueResult::Clock::now() - it->second.at <= std::chrono::duration<double>(def.graceSeconds)) {
                    // Within grace: the last good value, its age showing how stale it is
                    auto held = ValueResult::Of(it->second.value);
                    held.sampledAt = it->second.at;
                    return held;
                }
            }
            if (!result.Ok() && def.fallback) result.value = *def.fallback;
            return result;
        }
    }

    ValueResult NamedValueRef::Read(const EvaluationContext& context) const {
        auto def = Bind();
        if (!def) return ValueResult::Fail(ValueStatus::Unavailable, "no value named " + name_);
        return ReadDefinition(*def, context);
    }

    EventDependency NamedValueRef::GetEventDependencies() const {
        EventDependency deps = EventDependency::Value;  // pushes and redefinitions
        auto def = Bind();
        if (!def) return deps;
        ResolvingGuard guard(Lower(def->name));
        if (!guard.entered) return deps;
        if (def->value) deps |= def->value->GetEventDependencies();
        if (def->enabledWhen) deps |= def->enabledWhen->GetEffectiveEventDependencies();
        for (const auto& source : Registry::Get().ProvidersOf(def->name)) {
            deps |= NamedValueRef(source->name).GetEventDependencies();
        }
        return deps;
    }

    bool NamedValueRef::ReadsActor() const {
        auto def = Bind();
        if (!def) return true;
        if (!def->global) return true;
        // A combined value is per actor when any source is
        auto providers = Registry::Get().ProvidersOf(def->name);
        ResolvingGuard guard(def->key);
        if (!guard.entered) return false;
        return std::any_of(providers.begin(), providers.end(),
                           [](const auto& p) { return NamedValueRef(p->name).ReadsActor(); });
    }

    std::vector<std::string> NamedValueRef::Validate() const {
        if (!NamedValueDef::IsValidName(name_)) return {"invalid value name '" + name_ + "'"};
        return {};
    }

    // ── Truth ────────────────────────────────────────────────────────────────

    ValueResult TruthValue::Read(const EvaluationContext& context) const {
        if (!condition_) return ValueResult::Fail(ValueStatus::Error, "missing condition");
        const auto r = condition_->EvaluateTriCounted(context);
        if (r.state == TriState::Pending) return ValueResult::Fail(ValueStatus::Pending, r.reason);
        if (r.state == TriState::Unavailable) return ValueResult::Fail(ValueStatus::Unavailable, r.reason);
        return ValueResult::Of(r.state == TriState::True ? 1.0 : 0.0);
    }

    EventDependency TruthValue::GetEventDependencies() const {
        return condition_ ? condition_->GetEffectiveEventDependencies() : EventDependency::None;
    }

    json TruthValue::ToJson() const {
        return {{"type", "Truth"}, {"condition", condition_ ? condition_->ToJsonComplete() : json(nullptr)}};
    }

    std::vector<std::string> TruthValue::Validate() const {
        return condition_ ? condition_->Validate() : std::vector<std::string>{"Truth: missing condition"};
    }

    // ── Numeric type ─────────────────────────────────────────────────────────

    TypeValue::TypeValue(const TypeInfo* type, std::shared_ptr<const Condition> condition)
        : type_(type), condition_(std::move(condition)) {
        statsSlot = type_ ? type_->statsSlot : Stats::kUnregistered;
    }

    ValueResult TypeValue::Read(const EvaluationContext& context) const {
        if (!condition_) return ValueResult::Fail(ValueStatus::Error, "missing condition");
        return condition_->ReadValue(context);
    }

    EventDependency TypeValue::GetEventDependencies() const {
        if (!condition_) return EventDependency::None;
        auto deps = condition_->GetEventDependencies();
        if (condition_->forcePolled) deps |= EventDependency::Polled;
        return deps;
    }

    json TypeValue::ToJson() const {
        if (!condition_) return nullptr;
        json j = condition_->ToJsonComplete();
        if (type_ && !type_->compareField.empty()) j.erase(type_->compareField);
        j.erase("operator");
        j.erase("operation");
        return j;
    }

    std::vector<std::string> TypeValue::Validate() const {
        return condition_ ? condition_->Validate() : std::vector<std::string>{"missing condition"};
    }

    // ── Named value definitions ──────────────────────────────────────────────

    bool NamedValueDef::IsValidName(std::string_view name) {
        if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return false;
        return std::all_of(name.begin(), name.end(),
                           [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
    }

    json NamedValueDef::ToJson() const {
        json j{{"name", name},
               {"label", label.empty() ? name : label},
               {"scope", global ? "global" : "actor"},
               {"pushed", pushed},
               {"source", source}};
        const auto meta = info.ToJson();
        for (const auto& [key, v] : meta.items()) j[key] = v;
        if (!expression.empty()) j["expression"] = expression;
        else if (value) j["expression"] = Expression::ToExpression(*value);
        if (fallback) j["fallback"] = *fallback;
        if (enabledWhen) j["enabledWhen"] = Expression::ToExpression(*enabledWhen);
        if (pushed) {
            j["pushedDefault"] = pushedDefault ? json(*pushedDefault) : json(nullptr);
            j["pushedTimeoutSeconds"] = pushedTimeoutSeconds;
        }
        if (!requiresPlugins.empty()) j["requiresPlugins"] = requiresPlugins;
        if (!requiresDlls.empty()) j["requiresDlls"] = requiresDlls;
        if (!refreshOn.empty()) j["refreshOn"] = refreshOn;
        if (!provides.empty()) {
            j["provides"] = provides;
            j["priority"] = priority;
        }
        if (auto providers = Registry::Get().ProvidersOf(name); !providers.empty()) {
            json list = json::array();
            for (const auto& p : providers) list.push_back({{"name", p->name}, {"priority", p->priority}, {"source", p->source}});
            j["providers"] = std::move(list);
        }
        if (!category.empty()) j["category"] = category;
        return j;
    }

    std::shared_ptr<NamedValueDef> NamedValueDef::Parse(const std::string& name, const json& definition,
                                                        const std::string& source, std::string* error) {
        auto fail = [&](std::string message) -> std::shared_ptr<NamedValueDef> {
            if (error) *error = std::move(message);
            return nullptr;
        };
        if (!IsValidName(name)) return fail("invalid value name '" + name + "' (letters, digits and _ only)");

        auto def = std::make_shared<NamedValueDef>();
        def->name = name;
        def->key = Lower(name);
        def->source = source;
        def->definition = definition;

        json valueSpec;
        if (definition.is_string() || definition.is_number()) {
            valueSpec = definition;
        } else if (definition.is_object() && definition.contains("type")) {
            valueSpec = definition;  // a bare value node
        } else if (definition.is_object()) {
            if (definition.contains("expr")) valueSpec = definition["expr"];
            else if (definition.contains("value")) valueSpec = definition["value"];

            def->label = definition.value("label", "");
            def->info.unit = definition.value("unit", "");
            def->info.integer = definition.value("integer", false);
            if (definition.contains("range") && definition["range"].is_array() && definition["range"].size() == 2) {
                const auto& range = definition["range"];
                if (range[0].is_number()) def->info.min = range[0].get<double>();
                if (range[1].is_number()) def->info.max = range[1].get<double>();
            }
            if (definition.contains("fallback")) {
                if (!definition["fallback"].is_number()) return fail("\"fallback\" must be a number");
                def->fallback = definition["fallback"].get<double>();
            }
            def->graceSeconds = definition.value("graceSeconds", 0.0);
            def->requiresPlugins = ReadStringList(definition, "requiresPlugins");
            def->category = definition.value("category", "");
            def->icon = definition.value("icon", "");
            def->description = definition.value("description", "");
            def->owner = definition.value("owner", "");
            def->tags = ReadStringList(definition, "tags");
            if (definition.contains("offer")) {
                auto offer = definition["offer"].is_string() ? ParseOffer(definition["offer"].get<std::string>()) : std::nullopt;
                if (!offer) return fail("\"offer\" must be everywhere, owner or nowhere");
                def->offer = *offer;
            }
            def->requiresDlls = ReadStringList(definition, "requiresDlls");
            def->refreshOn = ReadStringList(definition, "refreshOn");
            def->provides = ReadStringList(definition, "provides");
            def->priority = definition.value("priority", 0.0);
            for (const auto& target : def->provides) {
                if (!IsValidName(target)) return fail("invalid provides name '" + target + "'");
                if (Lower(target) == Lower(name)) return fail("a value cannot provide itself");
            }

            const std::string scope = definition.value("scope", "auto");
            if (IEquals(scope, "actor")) def->scope = Scope::Actor;
            else if (IEquals(scope, "global")) def->scope = Scope::Global;
            else if (!IEquals(scope, "auto")) return fail("\"scope\" must be auto, actor or global");

            if (definition.contains("pushed")) {
                const auto& pushed = definition["pushed"];
                if (pushed.is_boolean()) {
                    def->pushed = pushed.get<bool>();
                } else if (pushed.is_object()) {
                    def->pushed = true;
                    if (pushed.contains("default") && pushed["default"].is_number()) {
                        def->pushedDefault = pushed["default"].get<double>();
                    }
                    def->pushedTimeoutSeconds = pushed.value("timeoutSeconds", 0.0);
                } else {
                    return fail("\"pushed\" must be true or {\"default\", \"timeoutSeconds\"}");
                }
            }

            if (definition.contains("enabledWhen")) {
                const auto& when = definition["enabledWhen"];
                std::string conditionError;
                std::unique_ptr<Condition> condition;
                if (when.is_string()) condition = Expression::CompileAny(when.get<std::string>(), &conditionError);
                else if (when.is_object()) condition = Condition::FromJson(when);
                if (!condition) {
                    return fail("enabledWhen: " + (conditionError.empty() ? std::string("not a condition") : conditionError));
                }
                def->enabledWhen = std::move(condition);
            }
        } else {
            return fail("a value definition must be an expression, a value node or an object");
        }

        if (!valueSpec.is_null()) {
            std::string valueError;
            std::unique_ptr<Value> value;
            if (valueSpec.is_string()) {
                def->expression = valueSpec.get<std::string>();
                value = Expression::CompileValueAny(def->expression, &valueError);
            } else {
                value = Value::FromJson(valueSpec, &valueError);
            }
            if (!value) return fail(valueError.empty() ? "invalid value" : valueError);
            def->value = std::move(value);
        } else if (!def->pushed && !(definition.is_object() && definition.value("combined", false))) {
            return fail("needs \"expr\" (or \"pushed\" for a value set from scripts, or \"combined\": true for "
                        "a value read from its providers)");
        }

        switch (def->scope) {
            case Scope::Actor: def->global = false; break;
            case Scope::Global: def->global = true; break;
            default: {
                ResolvingGuard guard(Lower(name));  // a self-reference must not recurse
                const bool combined = definition.is_object() && definition.value("combined", false);
                def->global = def->value ? !def->value->ReadsActor() : combined;
                if (def->enabledWhen && DependsOnActor(def->enabledWhen->GetEventDependencies())) def->global = false;
                break;
            }
        }
        return def;
    }

} // namespace ConditionEngine
