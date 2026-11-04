# Getting started

ConditionEngine ships as `Data/SKSE/Plugins/ConditionEngine.dll` and exports a plain C API. Your plugin can link its
import library or resolve the functions at runtime. Resolving at runtime lets your mod run without ConditionEngine
installed.

## 1. Add the header

Copy `include/ConditionEngine/ConditionEngineAPI.h` and `include/ConditionEngine/Export.h` into your project (keep the
`ConditionEngine/` folder, the API header includes `ConditionEngine/Export.h`).

## 2. Load the DLL

Either link `ConditionEngine.lib` and call the functions directly, or resolve them with `GetProcAddress`. For the
second, define `CONDITIONENGINE_STATIC` before including the header: that gives plain declarations to take
`decltype` of, and imports nothing.

```cpp
#define CONDITIONENGINE_STATIC
#include <ConditionEngine/ConditionEngineAPI.h>

namespace CE {
    HMODULE dll = nullptr;
    decltype(&ConditionEngine_Compile) Compile = nullptr;
    decltype(&ConditionEngine_Release) Release = nullptr;
    decltype(&ConditionEngine_Evaluate) Evaluate = nullptr;
    // ... one line per function you use

    template <class F>
    bool Resolve(F& out, const char* name) {
        out = reinterpret_cast<F>(GetProcAddress(dll, name));
        return out != nullptr;
    }

    bool Load() {
        dll = GetModuleHandleW(L"ConditionEngine.dll");
        if (!dll) return false;                                   // not installed
        auto getVersion = reinterpret_cast<decltype(&ConditionEngine_GetAPIVersion)>(
            GetProcAddress(dll, "ConditionEngine_GetAPIVersion"));
        if (!getVersion || getVersion() != CONDITIONENGINE_API_VERSION) return false;
        return Resolve(Compile, "ConditionEngine_Compile") &&
               Resolve(Release, "ConditionEngine_Release") &&
               Resolve(Evaluate, "ConditionEngine_Evaluate");
    }
}
```

## 3. Call it at the right time

| SKSE message | What you can do |
|---|---|
| `kPostLoad` | `ConditionEngine.dll` is loaded: call `CE::Load()`, register condition types, value types, sources and categories |
| `kDataLoaded` | Forms resolve and preset files are loaded: compile expressions that name forms, register named conditions that depend on presets |
| `kPostLoadGame` / `kNewGame` | Start watches on actors |

Registrations made through the C API last for the whole session. Papyrus registrations, pushed values and watches on
created actors (`0xFF...` FormIDs) are cleared on every game load.

## Conventions

- **Handles** are `uint32_t`; 0 is invalid. Release what you compile (`ConditionEngine_Release`,
  `ConditionEngine_ReleaseValue`) and unwatch what you watch (`ConditionEngine_Unwatch`).
- **Actors** are `RE::Actor*` passed as `void*`. Null means the player.
- **Strings out** use your buffer: the function returns the size it needs (including the terminating null) and
  writes as much as fits. Call with a null buffer and size 0 to ask for the size.
- **Structs** start with `structSize`; set it to `sizeof` the struct (`CE_WatchOptions options{sizeof options};`).
- **Results**: `CE_OK` (0) means success; the other `CE_Result` codes are `CE_ERR_PARSE`, `CE_ERR_INVALID_HANDLE`,
  `CE_ERR_INVALID_ARG`, `CE_ERR_ALREADY_EXISTS` and `CE_ERR_NOT_FOUND`.
- **Threads**: every function can be called from any thread, but evaluation, value reads and watch callbacks read
  game state and run on the main thread. Call `ConditionEngine_Evaluate` from the main thread (an SKSE task, a hook,
  a menu callback). `ConditionEngine_ReadListWatch` and `ConditionEngine_ReadValueWatch` are the non-blocking reads
  for other threads.
- **The API is version 1, prerelease**: it may still change until the first release. Check
  `ConditionEngine_GetAPIVersion` as above.
