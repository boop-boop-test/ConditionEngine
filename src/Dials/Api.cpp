// Api.cpp - the dial functions of ConditionEngineAPI.h

#include "ConditionEngine/ConditionEngineAPI.h"
#include "Editor.h"
#include "Serialization.h"
#include "Service.h"

#include <cstring>

namespace {
    using ConditionEngine::Dials::Service;

    ConditionEngine::Dials::ActorKey Key(void* actor) {
        return actor ? static_cast<RE::Actor*>(actor)->GetFormID() : ConditionEngine::Dials::kPlayer;
    }

    uint32_t Copy(const std::string& s, char* buf, uint32_t size) {
        const auto needed = static_cast<uint32_t>(s.size() + 1);
        if (buf && size) {
            const auto n = std::min<std::size_t>(s.size(), size - 1);
            std::memcpy(buf, s.data(), n);
            buf[n] = '\0';
        }
        return needed;
    }

    CE_Result Done(bool found) {
        if (!found) return CE_ERR_NOT_FOUND;
        Service::Get().RequestPublish();
        return CE_OK;
    }

    std::string Owner(const char* owner) { return owner && *owner ? owner : "a mod"; }
}

extern "C" {

CONDITIONENGINE_API CE_Result ConditionEngine_DefineDial(const char* id, const char* json, const char* owner, char* errorBuf,
                                                         uint32_t errorBufSize) {
    if (!id || !json) return CE_ERR_INVALID_ARG;
    const auto o = Service::Get().DefineText(id, json, Owner(owner));
    std::string errors;
    for (const auto& e : o.errors) errors += (errors.empty() ? "" : "\n") + e;
    Copy(errors, errorBuf, errorBufSize);
    return o.ok ? CE_OK : CE_ERR_PARSE;
}

CONDITIONENGINE_API CE_Result ConditionEngine_RemoveDial(const char* id) {
    if (!id) return CE_ERR_INVALID_ARG;
    return Service::Get().Remove(id) ? CE_OK : CE_ERR_NOT_FOUND;
}

CONDITIONENGINE_API uint32_t ConditionEngine_RemoveDialOwner(const char* owner) {
    return static_cast<uint32_t>(Service::Get().RemoveSource(Owner(owner)));
}

CONDITIONENGINE_API CE_Result ConditionEngine_GetDial(const char* id, void* actor, double* outValue) {
    if (!id || !outValue) return CE_ERR_INVALID_ARG;
    const auto v = Service::Get().GetEngine().Get(id, Key(actor));
    if (!v) return CE_ERR_NOT_FOUND;
    *outValue = *v;
    return CE_OK;
}

CONDITIONENGINE_API CE_Result ConditionEngine_SetDial(const char* id, void* actor, double value) {
    return id ? Done(Service::Get().GetEngine().Set(id, Key(actor), value, "C API")) : CE_ERR_INVALID_ARG;
}

CONDITIONENGINE_API CE_Result ConditionEngine_AddDial(const char* id, void* actor, double delta) {
    return id ? Done(Service::Get().GetEngine().Add(id, Key(actor), delta, "C API")) : CE_ERR_INVALID_ARG;
}

CONDITIONENGINE_API CE_Result ConditionEngine_ResetDial(const char* id, void* actor) {
    return id ? Done(Service::Get().GetEngine().Reset(id, Key(actor), "C API")) : CE_ERR_INVALID_ARG;
}

CONDITIONENGINE_API void ConditionEngine_SignalDials(const char* eventName, void* actor, double number, const char* text) {
    if (!eventName) return;
    ConditionEngine::Dials::EventInfo info;
    info.actor = Key(actor);
    info.number = number;
    info.text = text ? text : "";
    std::string name = eventName;
    if (auto* tasks = SKSE::GetTaskInterface()) {
        tasks->AddTask([name = std::move(name), info = std::move(info)]() {
            auto& s = Service::Get();
            s.GetEngine().OnModEvent(name, info);
            s.Publish();
        });
    }
}

CONDITIONENGINE_API uint32_t ConditionEngine_GetDialsSnapshot(char* buf, uint32_t bufSize) {
    return Copy(Service::Get().Snapshot().dump(), buf, bufSize);
}

CONDITIONENGINE_API uint32_t ConditionEngine_SubscribeDials(CE_DialChangedFn fn, void* userData) {
    return Service::Get().Subscribe(fn, userData);
}

CONDITIONENGINE_API void ConditionEngine_UnsubscribeDials(uint32_t subscription) { Service::Get().Unsubscribe(subscription); }

CONDITIONENGINE_API int32_t ConditionEngine_IsHosted(void) { return ConditionEngine::Dials::Serialization::Hosted() ? 1 : 0; }

CONDITIONENGINE_API uint32_t ConditionEngine_SaveDials(uint8_t* buf, uint32_t bufSize) {
    const auto bytes = ConditionEngine::Dials::Serialization::SaveBytes();
    const auto needed = static_cast<uint32_t>(bytes.size());
    if (buf && bufSize >= needed) std::memcpy(buf, bytes.data(), bytes.size());
    return needed;
}

CONDITIONENGINE_API CE_Result ConditionEngine_LoadDials(const uint8_t* data, uint32_t size) {
    if (!data && size) return CE_ERR_INVALID_ARG;
    return ConditionEngine::Dials::Serialization::LoadBytes({data, size}) ? CE_OK : CE_ERR_PARSE;
}

CONDITIONENGINE_API void ConditionEngine_RevertDials(void) { ConditionEngine::Dials::Serialization::RevertValues(); }

CONDITIONENGINE_API int32_t ConditionEngine_OpenEditor(const char* id) {
    if (!ConditionEngine::Editor::Available()) return 0;
    std::string target = id ? id : "";
    if (auto* tasks = SKSE::GetTaskInterface()) tasks->AddTask([target]() { ConditionEngine::Editor::Open(target); });
    return 1;
}

}  // extern "C"
