/// @file Console.cpp
/// @brief The "ce" console command: explain expressions, list named entries, stats.

#include "Console.h"

#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Explain.h"
#include "ConditionEngine/Expression.h"
#include "ConditionEngine/Forms.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Stats.h"
#include "ConditionEngine/Value.h"
#include "ConditionEngine/Values/ValueNodes.h"

#include <algorithm>
#include <format>
#include <sstream>

namespace ConditionEngine::Console {

    namespace {
        constexpr const char* kLongName = "ConditionEngine";
        constexpr const char* kShortName = "ce";
        constexpr const char* kHelp = "ConditionEngine: ce help";

        constexpr const char* kUsage =
            "ce eval <expression>            why it is true / false (or what a value reads)\n"
            "                                for the selected actor, else the player\n"
            "ce evalfor <actor> <expression> the same for: player, selected, a form ID (14 or\n"
            "                                0x14|Skyrim.esm), an editor ID or a loaded actor's name\n"
            "ce list [filter]                named conditions and values, read for the actor\n"
            "ce stats [on|off|reset]         evaluation counters and timings\n"
            "Quote expressions with commas: ce eval \"Time(20, 6) && $hunger > 50\"";

        std::string Lower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        void Print(const std::string& text) {
            auto* log = RE::ConsoleLog::GetSingleton();
            if (!log) return;
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line)) log->Print("%s", line.c_str());
        }

        // Splits off the next whitespace-separated word ("quoted words" stay together)
        std::string NextWord(std::string_view& rest) {
            while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front()))) rest.remove_prefix(1);
            if (rest.empty()) return {};
            std::size_t end;
            std::string word;
            if (rest.front() == '"') {
                end = rest.find('"', 1);
                word = std::string(rest.substr(1, end == std::string_view::npos ? end : end - 1));
                end = end == std::string_view::npos ? rest.size() : end + 1;
            } else {
                end = 0;
                while (end < rest.size() && !std::isspace(static_cast<unsigned char>(rest[end]))) ++end;
                word = std::string(rest.substr(0, end));
            }
            rest.remove_prefix(end);
            return word;
        }

        // The rest of the line, without surrounding whitespace or one pair of enclosing quotes
        std::string Remainder(std::string_view rest) {
            while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front()))) rest.remove_prefix(1);
            while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.back()))) rest.remove_suffix(1);
            if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"' &&
                rest.find('"', 1) == rest.size() - 1) {
                rest = rest.substr(1, rest.size() - 2);
            }
            return std::string(rest);
        }

        RE::Actor* SelectedActor() {
            auto selected = RE::Console::GetSelectedRef();
            return selected ? selected->As<RE::Actor>() : nullptr;
        }

        RE::Actor* DefaultActor() {
            if (auto* actor = SelectedActor()) return actor;
            return RE::PlayerCharacter::GetSingleton();
        }

        // Loaded actors whose base is `npc`, or whose name matches `name` (case-insensitive)
        RE::Actor* FindLoaded(const RE::TESNPC* npc, const std::string& name) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto matches = [&](RE::Actor* actor) {
                if (!actor) return false;
                if (npc) return actor->GetActorBase() == npc;
                const char* display = actor->GetDisplayFullName();
                return display && Lower(display) == name;
            };
            if (matches(player)) return player;
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                for (auto& handle : lists->highActorHandles) {
                    auto actor = handle.get();
                    if (matches(actor.get())) return actor.get();
                }
            }
            return nullptr;
        }

        RE::Actor* ResolveActor(const std::string& token) {
            const auto lower = Lower(token);
            if (lower == "player") return RE::PlayerCharacter::GetSingleton();
            if (lower == "selected" || lower == "sel") return SelectedActor();

            // A bare hex form ID as typed in the console ("14", "0001A66B")
            if (token.find('|') == std::string::npos) {
                try {
                    std::size_t used = 0;
                    const auto id = std::stoul(token, &used, 16);
                    if (used == token.size()) {
                        if (auto* form = RE::TESForm::LookupByID(static_cast<RE::FormID>(id))) {
                            if (auto* actor = form->As<RE::Actor>()) return actor;
                            if (auto* npc = form->As<RE::TESNPC>()) return FindLoaded(npc, {});
                        }
                    }
                } catch (...) {
                }
            }
            if (auto* form = Forms::LookupAny(token)) {
                if (auto* actor = form->As<RE::Actor>()) return actor;
                if (auto* npc = form->As<RE::TESNPC>()) return FindLoaded(npc, {});
            }
            return FindLoaded(nullptr, lower);
        }

        std::string ActorText(RE::Actor* actor) {
            const char* name = actor->GetDisplayFullName();
            return std::format("{} ({:08X})", name && *name ? name : "?", actor->GetFormID());
        }

        void Eval(RE::Actor* actor, const std::string& text) {
            if (text.empty()) {
                Print("ce: no expression. ce help");
                return;
            }
            Print("ce: " + ActorText(actor));
            EvaluationContext context(actor);
            std::string conditionError;
            if (auto condition = Expression::CompileAny(text, &conditionError)) {
                Print(Explain::ToText(Explain::ExplainCondition(*condition, context)));
                return;
            }
            std::string valueError;
            if (auto value = Expression::CompileValueAny(text, &valueError)) {
                Print(Explain::ToText(Explain::ExplainValue(*value, context)));
                return;
            }
            Print("ce: not a condition: " + conditionError);
            Print("ce: not a value: " + valueError);
        }

        void List(RE::Actor* actor, const std::string& filter) {
            auto& registry = Registry::Get();
            const auto needle = Lower(filter);
            auto wanted = [&](const std::string& name) { return needle.empty() || Lower(name).contains(needle); };
            EvaluationContext context(actor);
            Print("ce: " + ActorText(actor));

            auto values = registry.ListNamedValues();
            std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return a->key < b->key; });
            std::size_t shown = 0;
            for (const auto& def : values) {
                if (!wanted(def->name)) continue;
                const auto r = NamedValueRef(def->name).Read(context);
                std::string line = "  $" + def->name + " = ";
                line += r.Ok() ? Explain::FormatNumber(r.value) + def->info.unit
                               : std::format("{} ({})", ValueStatusName(r.status), r.reason);
                if (r.Ok() && r.from) line += "  (from " + r.FromText() + ")";
                if (!def->source.empty()) line += "  [" + def->source + "]";
                Print(line);
                ++shown;
            }

            auto named = registry.ListNamed();
            std::sort(named.begin(), named.end(), [](const auto& a, const auto& b) { return Lower(a.id) < Lower(b.id); });
            for (const auto& info : named) {
                if (!wanted(info.id)) continue;
                auto condition = registry.FindNamed(info.id);
                if (!condition) continue;
                const auto r = condition->EvaluateTri(context);
                const char* state = r.state == TriState::True      ? "true"
                                    : r.state == TriState::Pending ? "pending"
                                    : r.state == TriState::Unavailable ? "unavailable"
                                                                       : "false";
                std::string line = "  @" + info.id + " = " + state;
                if (r.state != TriState::True && r.state != TriState::False && !r.reason.empty()) {
                    line += " (" + r.reason + ")";
                }
                if (!info.source.empty()) line += "  [" + info.source + "]";
                Print(line);
                ++shown;
            }
            Print(std::format("ce: {} shown. ce eval $name or ce eval @name explains one", shown));
        }

        void StatsCommand(const std::string& arg) {
            const auto what = Lower(arg);
            if (what == "on" || what == "off") {
                Stats::SetEnabled(what == "on");
                Print(std::format("ce: stats {}", what));
                return;
            }
            if (what == "reset") {
                Stats::Reset();
                Print("ce: stats reset");
                return;
            }
            const auto s = Stats::Snapshot();
            const auto& w = s["watcher"];
            const auto& a = s["availability"];
            const auto& p = s["papyrusCalls"];
            Print(std::format("ce: stats {} for {:.1f}s{}", s.value("enabled", false) ? "on" : "off",
                              s.value("sinceMs", 0.0) / 1000.0,
                              s.value("enabled", false) ? "" : " (ce stats on to time evaluations)"));
            Print(std::format("  watches: {} conditions, {} values", w.value("conditionWatches", 0),
                              w.value("valueWatches", 0)));
            Print(std::format("  ticks: {} avg {:.1f}us max {:.1f}us; {} evaluations, {} value samples",
                              w.value("ticks", 0), w.value("tickAverageUs", 0.0), w.value("tickMaxUs", 0.0),
                              w.value("conditionEvaluations", 0), w.value("valueSamples", 0)));
            if (const auto consoleTicks = w.value("consoleModeTicks", 0); consoleTicks > 0) {
                Print(std::format("  console mode was on in {} of {} ticks: the game's condition functions print",
                                  consoleTicks, w.value("ticks", 0)));
                Print("  \"GetInCell >> 0\" lines for every check (anyone's) while it is stuck on");
            }
            Print(std::format("  availability: revision {}, {} checks, {} rebinds", a.value("revision", 0),
                              a.value("checks", 0), a.value("rebinds", 0)));
            Print(std::format("  papyrus calls: {} dispatched, {} completed, {} failed, {} timed out",
                              p.value("dispatched", 0), p.value("completed", 0), p.value("failed", 0),
                              p.value("timedOut", 0)));
            if (!s.contains("types")) return;
            auto types = s["types"];
            std::sort(types.begin(), types.end(),
                      [](const auto& x, const auto& y) { return x.value("totalUs", 0.0) > y.value("totalUs", 0.0); });
            std::size_t n = 0;
            for (const auto& t : types) {
                if (++n > 10) break;
                Print(std::format("  {:<20} {:>8} evals {:>8} reads {:>10.1f}us total {:>10.1f}us self",
                                  t.value("name", ""), t.value("evaluations", 0), t.value("reads", 0),
                                  t.value("totalUs", 0.0), t.value("selfUs", 0.0)));
            }
        }

        void Run(const std::string& line) {
            std::string_view rest(line);
            NextWord(rest);  // the command itself
            const auto sub = Lower(NextWord(rest));

            if (sub.empty() || sub == "help" || sub == "?") {
                Print(kUsage);
            } else if (sub == "eval" || sub == "e" || sub == "explain") {
                Eval(DefaultActor(), Remainder(rest));
            } else if (sub == "evalfor" || sub == "for") {
                const auto token = NextWord(rest);
                auto* actor = token.empty() ? nullptr : ResolveActor(token);
                if (!actor) {
                    Print("ce: no loaded actor '" + token + "'");
                    return;
                }
                Eval(actor, Remainder(rest));
            } else if (sub == "list" || sub == "ls") {
                List(DefaultActor(), Remainder(rest));
            } else if (sub == "stats") {
                StatsCommand(Remainder(rest));
            } else {
                Print("ce: unknown '" + sub + "'");
                Print(kUsage);
            }
        }

        bool Execute(const RE::SCRIPT_PARAMETER*, RE::SCRIPT_FUNCTION::ScriptData*, RE::TESObjectREFR*,
                     RE::TESObjectREFR*, RE::Script* script, RE::ScriptLocals*, double&, std::uint32_t&) {
            if (!script) return true;
            try {
                Run(script->GetCommand());
            } catch (const std::exception& e) {
                Print(std::string("ce: error: ") + e.what());
            }
            return true;
        }

        // Optional words so the console accepts anything after "ce"; the line is re-read whole
        RE::SCRIPT_PARAMETER g_params[] = {
            {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true}, {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true},
            {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true}, {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true},
            {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true}, {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true},
            {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true}, {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true},
            {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true}, {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true},
            {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true}, {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true},
            {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true}, {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true},
            {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true}, {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true},
            {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true}, {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true},
            {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true}, {"Text", RE::SCRIPT_PARAM_TYPE::kChar, true},
        };
    }

    void Install(const std::string& replace) {
        if (replace.empty()) return;
        if (RE::SCRIPT_FUNCTION::LocateConsoleCommand(kLongName)) {
            SKSE::log::info("Console: '{}' already installed", kLongName);
            return;
        }
        auto* command = RE::SCRIPT_FUNCTION::LocateConsoleCommand(replace);
        if (!command) {
            SKSE::log::warn("Console: no console command '{}' to replace ([Console] sReplaceCommand); 'ce' not added",
                            replace);
            return;
        }
        // Another mod that took the slot usually renamed it; also refuse a slot with a short name in use
        if (command->shortName && *command->shortName && Lower(command->shortName) != Lower(replace)) {
            SKSE::log::warn("Console: '{}' has short name '{}', so it may be in use; 'ce' not added", replace,
                            command->shortName);
            return;
        }

        command->functionName = kLongName;
        command->shortName = kShortName;
        command->helpString = kHelp;
        command->referenceFunction = false;
        command->SetParameters(g_params);
        command->executeFunction = Execute;
        command->conditionFunction = nullptr;
        SKSE::log::info("Console: 'ce' installed in place of '{}'", replace);
    }

} // namespace ConditionEngine::Console
