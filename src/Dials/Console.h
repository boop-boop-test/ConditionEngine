#pragma once

#include <string_view>

namespace ConditionEngine::Dials::Console {
    // "ce dials <rest>": `rest` is the line after "dials" (main thread)
    void Run(std::string_view rest);
}
