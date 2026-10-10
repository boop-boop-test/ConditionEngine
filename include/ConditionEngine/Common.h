#pragma once

// Common.h - Shared includes and logging for ConditionEngine.
// Has no host-specific dependencies: only CommonLib, spdlog and nlohmann/json.

#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace ConditionEngine {

    namespace Log {
        // Optional verbose trace logger supplied by the host. When the getter is unset
        // or returns null, CE_TRACE is a no-op. Hosts typically route this to their
        // "extra logging" sink so condition evaluation can be traced on demand.
        using TraceLoggerGetter = std::shared_ptr<spdlog::logger> (*)();
        void SetTraceLogger(TraceLoggerGetter getter);
        std::shared_ptr<spdlog::logger> GetTraceLogger();
    }

} // namespace ConditionEngine

#define CE_TRACE(a_type, ...)                                                                  \
    do {                                                                                       \
        if (auto ce_trace_logger_ = ::ConditionEngine::Log::GetTraceLogger())                  \
            ce_trace_logger_->log(spdlog::source_loc(__FILE__, __LINE__, __FUNCTION__),        \
                                  spdlog::level::a_type, __VA_ARGS__);                         \
    } while (false)
