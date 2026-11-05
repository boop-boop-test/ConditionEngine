// Console.cpp - "ce dials ...": dials from the console (ConditionEngine's "ce" command hands over).

#include "Console.h"
#include "Service.h"
#include "Editor.h"

#include <format>
#include <sstream>

namespace ConditionEngine::Dials::Console {

    namespace {
        constexpr const char* kUsage =
            "ce dials                          every dial and its value (the selected actor's, else the player's)\n"
            "ce dials info <dial>              its definition, where it comes from, actors' values\n"
            "ce dials set <dial> <value>       set it (the selected actor's for actor dials)\n"
            "ce dials add <dial> <amount>      change it (also: reset, toggle, cycle)\n"
            "ce dials signal <event> [number]  fire the rules listening for an event\n"
            "ce dials log                      recent changes and what caused them\n"
            "ce dials skyrimnet [dial]         what get_dials() answers SkyrimNet for the selected actor\n"
            "ce dials reload                   read the dial sections of the preset files again\n"
            "ce ui [dial]                      open the editor";

        void Print(const std::string& text) {
            auto* log = RE::ConsoleLog::GetSingleton();
            if (!log) return;
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line)) log->Print("%s", line.c_str());
        }

        std::string NextWord(std::string_view& rest) {
            while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front()))) rest.remove_prefix(1);
            if (rest.empty()) return {};
            std::size_t end = 0;
            std::string word;
            if (rest.front() == '"') {
                end = rest.find('"', 1);
                word = std::string(rest.substr(1, end == std::string_view::npos ? end : end - 1));
                end = end == std::string_view::npos ? rest.size() : end + 1;
            } else {
                while (end < rest.size() && !std::isspace(static_cast<unsigned char>(rest[end]))) ++end;
                word = std::string(rest.substr(0, end));
            }
            rest.remove_prefix(end);
            return word;
        }

        ActorKey Selected() {
            auto ref = RE::Console::GetSelectedRef();
            return ref && ref->Is(RE::FormType::ActorCharacter) ? ref->GetFormID() : kPlayer;
        }

        std::string Value(const DialDef& def, double v) {
            auto text = std::format("{:g}", v);
            if (!def.unit.empty()) text += def.unit == "%" ? "%" : " " + def.unit;
            if (const auto label = def.OptionLabel(v); !label.empty()) text += " (" + label + ")";
            return text;
        }

        void List() {
            auto& engine = Service::Get().GetEngine();
            const auto actor = Selected();
            const auto defs = engine.Definitions();
            if (defs.empty()) {
                Print("dials: no dials. Add files to Data/SKSE/Plugins/Dials or make one in the editor (dials ui).");
                return;
            }
            Print(std::format("dials: {} dials, values for {}", defs.size(), ActorName(actor)));
            for (const auto& def : defs) {
                const double v = engine.Get(def.id, actor).value_or(def.defaultValue);
                Print(std::format("  ${:<24} {:<16} {}{}{}", def.id, Value(def, v), def.scope == Scope::Actor ? "actor" : "global",
                                  def.enabled ? "" : ", disabled", def.source.empty() ? "" : ", from " + def.source));
            }
        }

    }

    void Run(std::string_view line) {
        const auto sub = Lower(NextWord(line));
        auto& service = Service::Get();
        auto& engine = service.GetEngine();
        if (!service.Ready()) {
            Print("dials: not running: " + service.NotReadyReason());
            return;
        }
        if (sub.empty() || sub == "list") return List();
        if (sub == "help" || sub == "?") return Print(kUsage);
        if (sub == "log") {
            const auto log = engine.RecentLog(25);
            if (log.empty()) Print("dials: nothing changed yet");
            for (const auto& e : log) {
                Print(std::format("  {:>8.1f}s ${} {} {:g} -> {:g}: {}", e.time, e.id, ActorName(e.actor), e.from, e.to, e.cause));
            }
            return;
        }
        if (sub == "reload") {
            service.ReloadFiles();
            Print(std::format("dials: {} dials", engine.Definitions().size()));
            return;
        }
        if (sub == "ui" || sub == "editor") {
            Editor::Open(NextWord(line));
            return;
        }
        if (sub == "skyrimnet" || sub == "sn") {
            const auto dials = service.DialsFor(Selected());
            const auto only = NextWord(line);
            if (!only.empty()) {
                const auto it = dials.find(only);
                Print(it == dials.end() ? "dials: no dial '" + only + "'" : "get_dials(...)." + only + " = " + it->dump());
            } else {
                Print("get_dials(" + ActorName(Selected()) + ") = " + dials.dump(2));
            }
            return;
        }
        if (sub == "signal") {
            const auto event = NextWord(line);
            const auto number = NextWord(line);
            EventInfo info;
            info.actor = Selected();
            info.number = number.empty() ? 0.0 : std::atof(number.c_str());
            engine.OnModEvent(event, info);
            service.Publish();
            Print("dials: signalled " + event);
            return;
        }

        const auto id = NextWord(line);
        const auto def = engine.Find(id.starts_with('$') ? id.substr(1) : id);
        if (!def) {
            Print(id.empty() ? std::string(kUsage) : "dials: no dial '" + id + "'");
            return;
        }
        const auto actor = Selected();
        const auto arg = NextWord(line);
        if (sub == "info") {
            Print(service.Describe(def->id).dump(2));
            return;
        }
        if (sub == "set" || sub == "add") {
            if (arg.empty()) return Print("dials: " + sub + " <dial> <number>");
            const double v = std::atof(arg.c_str());
            if (sub == "set") engine.Set(def->id, actor, v, "console");
            else engine.Add(def->id, actor, v, "console");
        } else if (sub == "reset") {
            engine.Reset(def->id, actor, "console");
        } else if (sub == "toggle") {
            engine.Toggle(def->id, actor, "console");
        } else if (sub == "cycle") {
            engine.Cycle(def->id, actor, arg.empty() ? 1 : std::atoi(arg.c_str()), "console");
        } else {
            Print("dials: unknown '" + sub + "'");
            return Print(kUsage);
        }
        service.Publish();
        Print(std::format("dials: ${} = {} ({})", def->id, Value(*def, engine.Get(def->id, actor).value_or(0)),
                          ActorName(def->scope == Scope::Actor ? actor : 0)));
    }

} // namespace ConditionEngine::Dials::Console
