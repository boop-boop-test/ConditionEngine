#pragma once

// Console.h - the "ce" in-game console command (ConditionEngine.dll only)
//
// Skyrim has no free console command slots, so "ce" takes over an unused vanilla one
// ([Console] sReplaceCommand, default TestCode). Not installed when that command is
// missing or another mod already took it.

#include <string>

namespace ConditionEngine::Console {

    // Main thread, after kDataLoaded. Empty `replace` adds nothing.
    void Install(const std::string& replace);

} // namespace ConditionEngine::Console
