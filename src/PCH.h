#pragma once

// Minimal PCH for condition-engine-lib: CommonLib include ordering without
// host-specific dependencies.

#pragma warning(push)
#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>
#pragma warning(pop)

#include <nlohmann/json.hpp>

using namespace std::literals;
