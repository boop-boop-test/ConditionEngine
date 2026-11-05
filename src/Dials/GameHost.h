#pragma once

// GameHost.h - The engine's Host in the game: expressions through ConditionEngine, forms
// and keywords through the data handler, the DialsEvent value type.

#include "Engine.h"

namespace ConditionEngine::Dials {

    class GameHost final : public Host {
    public:
        Handle CompileCondition(const std::string& source, std::string& error) override;
        Handle CompileValue(const std::string& source, std::string& error) override;
        void ReleaseCondition(Handle handle) override;
        void ReleaseValue(Handle handle) override;
        std::optional<bool> Evaluate(Handle condition, ActorKey actor) override;
        std::optional<double> EvaluateValue(Handle value, ActorKey actor) override;
        bool FormMatches(const std::string& filter, std::uint32_t formID) override;
        bool HasKeyword(const std::string& keyword, std::uint32_t formID) override;
        double Random01() override;
        void SetEventNumber(double number) override;
    };

    // The number DialsEvent reads (main thread, while a rule's expressions run)
    double EventNumber();
    // Registers the DialsEvent value type with ConditionEngine
    void RegisterEventValueType();

    // An actor from a key (0 / 0x14 = the player); null when not loaded in memory
    RE::Actor* ActorOf(ActorKey key);
    ActorKey KeyOf(const RE::TESForm* form);  // 0 unless an actor
    // A form from "0x12EB7|Skyrim.esm", "Skyrim.esm|0x12EB7", "0x12EB7" (runtime ID) or an editor ID
    RE::TESForm* ParseForm(const std::string& text);
    std::string ActorName(ActorKey key);

} // namespace ConditionEngine::Dials
