#pragma once

// CEApi.h - Dials reach ConditionEngine through its own C API (the same calls any other
// mod makes), so the dials code keeps one narrow way in: handles, named values, pushes.

#include "ConditionEngine/ConditionEngineAPI.h"

#include <string>

namespace ConditionEngine::Dials::CE {

    struct Api {
#define DIALS_CE_FN(name) decltype(&ConditionEngine_##name) name = nullptr
        DIALS_CE_FN(GetAPIVersion);
        DIALS_CE_FN(Compile);
        DIALS_CE_FN(Release);
        DIALS_CE_FN(Evaluate);
        DIALS_CE_FN(ToExpression);
        DIALS_CE_FN(Explain);
        DIALS_CE_FN(RegisterNamed);
        DIALS_CE_FN(UnregisterNamed);
        DIALS_CE_FN(UnregisterOwner);
        DIALS_CE_FN(LookupNamed);
        DIALS_CE_FN(GetCatalog);
        DIALS_CE_FN(RegisterCategory);
        DIALS_CE_FN(RegisterSource);
        DIALS_CE_FN(RegisterValueType);
        DIALS_CE_FN(NotifyEvent);
        DIALS_CE_FN(CompileValue);
        DIALS_CE_FN(ReleaseValue);
        DIALS_CE_FN(EvaluateValue);
        DIALS_CE_FN(ExplainValue);
        DIALS_CE_FN(RegisterNamedValue);
        DIALS_CE_FN(UnregisterNamedValue);
        DIALS_CE_FN(ListNamedValues);
        DIALS_CE_FN(SetValue);
        DIALS_CE_FN(SetGlobalValue);
        DIALS_CE_FN(ClearValue);
        DIALS_CE_FN(ClearGlobalValue);
#undef DIALS_CE_FN
    };

    // Fills the table from this DLL's exports. Always true; `why` stays for symmetry.
    bool Load(std::string& why);
    bool Ready();
    const Api& Get();

    // Calls a "size needed, write as much as fits" function and returns the whole string
    template <class Fn>
    std::string ReadString(Fn&& fn) {
        std::string out(512, '\0');
        auto needed = fn(out.data(), static_cast<std::uint32_t>(out.size()));
        if (needed > out.size()) {
            out.assign(needed, '\0');
            needed = fn(out.data(), static_cast<std::uint32_t>(out.size()));
        }
        out.resize(needed ? needed - 1 : 0);
        return out;
    }

} // namespace ConditionEngine::Dials::CE
