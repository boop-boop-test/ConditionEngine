#include "CEApi.h"

namespace ConditionEngine::Dials::CE {

    namespace {
        Api g_api;
        bool g_ready = false;
    }

    bool Load(std::string&) {
        if (g_ready) return true;
#define DIALS_CE_BIND(name) g_api.name = &ConditionEngine_##name
        DIALS_CE_BIND(GetAPIVersion);
        DIALS_CE_BIND(Compile);
        DIALS_CE_BIND(Release);
        DIALS_CE_BIND(Evaluate);
        DIALS_CE_BIND(ToExpression);
        DIALS_CE_BIND(Explain);
        DIALS_CE_BIND(RegisterNamed);
        DIALS_CE_BIND(UnregisterNamed);
        DIALS_CE_BIND(UnregisterOwner);
        DIALS_CE_BIND(LookupNamed);
        DIALS_CE_BIND(GetCatalog);
        DIALS_CE_BIND(RegisterCategory);
        DIALS_CE_BIND(RegisterSource);
        DIALS_CE_BIND(RegisterValueType);
        DIALS_CE_BIND(NotifyEvent);
        DIALS_CE_BIND(CompileValue);
        DIALS_CE_BIND(ReleaseValue);
        DIALS_CE_BIND(EvaluateValue);
        DIALS_CE_BIND(ExplainValue);
        DIALS_CE_BIND(RegisterNamedValue);
        DIALS_CE_BIND(UnregisterNamedValue);
        DIALS_CE_BIND(ListNamedValues);
        DIALS_CE_BIND(SetValue);
        DIALS_CE_BIND(SetGlobalValue);
        DIALS_CE_BIND(ClearValue);
        DIALS_CE_BIND(ClearGlobalValue);
#undef DIALS_CE_BIND
        g_ready = true;
        return true;
    }

    bool Ready() { return g_ready; }
    const Api& Get() { return g_api; }

} // namespace ConditionEngine::Dials::CE
