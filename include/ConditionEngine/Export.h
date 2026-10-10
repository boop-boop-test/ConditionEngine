#pragma once

// DLL export/import macro.
//
// CONDITIONENGINE_STATIC  - consuming as static lib (no decoration)
// CONDITIONENGINE_EXPORTS - building the DLL (dllexport)
// Neither                 - consuming the DLL (dllimport)

#if defined(CONDITIONENGINE_STATIC)
    #define CONDITIONENGINE_API
#elif defined(CONDITIONENGINE_EXPORTS)
    #define CONDITIONENGINE_API __declspec(dllexport)
#else
    #define CONDITIONENGINE_API __declspec(dllimport)
#endif
