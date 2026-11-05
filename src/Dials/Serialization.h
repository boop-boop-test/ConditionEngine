#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace ConditionEngine::Dials::Serialization {
    // Plugin load, before Install: true when another plugin started this copy (it lives outside
    // Data/SKSE/Plugins), so the SKSE handle in use is the host's
    bool DetectHosted();
    bool Hosted();

    // Registers the 'DIAL' co-save callbacks, unless hosted
    void Install();

    // For a host that saves the values in its own co-save record (and the C API around them).
    // LoadBytes resolves form IDs through SKSE: call it from a co-save load callback.
    std::vector<std::uint8_t> SaveBytes();
    bool LoadBytes(std::span<const std::uint8_t> bytes);
    void RevertValues();
}
