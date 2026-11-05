// Papyrus.cpp - Dials.psc natives. They run on Papyrus threads: the engine is locked,
// changes publish on the next main-thread task.

#include "Papyrus.h"
#include "Service.h"
#include "Editor.h"

namespace ConditionEngine::Dials::Papyrus {

    namespace {
        using StaticFn = RE::StaticFunctionTag;
        constexpr const char* kScript = "Dials";

        ActorKey Key(RE::Actor* actor) { return actor ? actor->GetFormID() : 0; }

        void Changed() { Service::Get().RequestPublish(); }

        float GetValue(StaticFn*, RE::BSFixedString id, RE::Actor* actor) {
            return static_cast<float>(Service::Get().GetEngine().Get(id.c_str(), Key(actor)).value_or(0.0));
        }

        void SetValue(StaticFn*, RE::BSFixedString id, float value, RE::Actor* actor) {
            if (Service::Get().GetEngine().Set(id.c_str(), Key(actor), value, "Papyrus")) Changed();
        }

        void AddValue(StaticFn*, RE::BSFixedString id, float delta, RE::Actor* actor) {
            if (Service::Get().GetEngine().Add(id.c_str(), Key(actor), delta, "Papyrus")) Changed();
        }

        void Reset(StaticFn*, RE::BSFixedString id, RE::Actor* actor) {
            if (Service::Get().GetEngine().Reset(id.c_str(), Key(actor), "Papyrus")) Changed();
        }

        void Toggle(StaticFn*, RE::BSFixedString id, RE::Actor* actor) {
            if (Service::Get().GetEngine().Toggle(id.c_str(), Key(actor), "Papyrus")) Changed();
        }

        void Cycle(StaticFn*, RE::BSFixedString id, std::int32_t step, RE::Actor* actor) {
            if (Service::Get().GetEngine().Cycle(id.c_str(), Key(actor), step, "Papyrus")) Changed();
        }

        RE::BSFixedString GetLabel(StaticFn*, RE::BSFixedString id, RE::Actor* actor) {
            auto& engine = Service::Get().GetEngine();
            const auto def = engine.Find(id.c_str());
            if (!def) return "";
            return def->OptionLabel(engine.Get(id.c_str(), Key(actor)).value_or(def->defaultValue)).c_str();
        }

        void Signal(StaticFn*, RE::BSFixedString name, RE::Actor* actor, float number, RE::BSFixedString text) {
            EventInfo info;
            info.actor = actor ? actor->GetFormID() : kPlayer;
            info.number = number;
            info.text = text.c_str();
            std::string event = name.c_str();
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask([event = std::move(event), info = std::move(info)]() {
                    auto& s = Service::Get();
                    s.GetEngine().OnModEvent(event, info);
                    s.Publish();
                });
            }
        }

        RE::BSFixedString Define(StaticFn*, RE::BSFixedString id, RE::BSFixedString json, RE::BSFixedString source) {
            const auto o = Service::Get().DefineText(id.c_str(), json.c_str(), source.empty() ? "Papyrus" : source.c_str());
            std::string out;
            for (const auto& e : o.errors) out += (out.empty() ? "" : "\n") + e;
            if (!o.ok && out.empty()) out = "not defined";
            return out.c_str();
        }

        bool Remove(StaticFn*, RE::BSFixedString id) { return Service::Get().Remove(id.c_str()); }
        bool Exists(StaticFn*, RE::BSFixedString id) { return Service::Get().GetEngine().Exists(id.c_str()); }

        std::vector<RE::BSFixedString> List(StaticFn*) {
            std::vector<RE::BSFixedString> out;
            for (const auto& def : Service::Get().GetEngine().Definitions()) out.emplace_back(def.id.c_str());
            return out;
        }

        RE::BSFixedString Describe(StaticFn*, RE::BSFixedString id) {
            const auto d = Service::Get().Describe(id.c_str());
            return d.is_null() ? "" : d.dump().c_str();
        }

        void OpenEditor(StaticFn*, RE::BSFixedString id) {
            std::string dial = id.c_str();
            if (auto* tasks = SKSE::GetTaskInterface()) tasks->AddTask([dial]() { Editor::Open(dial); });
        }

        void ReloadFiles(StaticFn*) { Service::Get().ReloadFiles(); }

        bool Register(RE::BSScript::IVirtualMachine* vm) {
            vm->RegisterFunction("GetValue", kScript, GetValue, true);
            vm->RegisterFunction("SetValue", kScript, SetValue);
            vm->RegisterFunction("AddValue", kScript, AddValue);
            vm->RegisterFunction("Reset", kScript, Reset);
            vm->RegisterFunction("Toggle", kScript, Toggle);
            vm->RegisterFunction("Cycle", kScript, Cycle);
            vm->RegisterFunction("GetLabel", kScript, GetLabel, true);
            vm->RegisterFunction("Signal", kScript, Signal);
            vm->RegisterFunction("Define", kScript, Define);
            vm->RegisterFunction("Remove", kScript, Remove);
            vm->RegisterFunction("Exists", kScript, Exists, true);
            vm->RegisterFunction("List", kScript, List);
            vm->RegisterFunction("Describe", kScript, Describe);
            vm->RegisterFunction("OpenEditor", kScript, OpenEditor);
            vm->RegisterFunction("ReloadFiles", kScript, ReloadFiles);
            return true;
        }
    }

    void Install() {
        if (auto* papyrus = SKSE::GetPapyrusInterface()) papyrus->Register(Register);
    }

} // namespace ConditionEngine::Dials::Papyrus
