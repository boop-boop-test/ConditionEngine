/// @file Plugin.cpp
/// @brief SKSE entry point for the standalone ConditionEngine.dll.

#include "Papyrus.h"

#include "ConditionEngine/Presets.h"
#include "ConditionEngine/Availability.h"
#include "ConditionEngine/Conditions/PapyrusCallCondition.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Settings.h"
#include "ConditionEngine/Values/PushedValues.h"
#include "ConditionEngine/Watcher.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace {
    void InitializeLog() {
        auto path = SKSE::log::log_directory();
        if (!path) return;
        *path /= "ConditionEngine.log";

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("ConditionEngine", std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    }

    void OnMessage(SKSE::MessagingInterface::Message* message) {
        switch (message->type) {
            case SKSE::MessagingInterface::kDataLoaded: {
                ConditionEngine::Availability::Get().RefreshLoadedPlugins();
                const auto settings = ConditionEngine::LoadSettings(ConditionEngine::GetDefaultSettingsPath());
                ConditionEngine::ApplyTemperatureSettings(settings);

                auto count = ConditionEngine::LoadPresetDirectory(ConditionEngine::GetDefaultPresetDirectory());
                SKSE::log::info("Loaded {} named conditions and values from presets", count);

                auto& watcher = ConditionEngine::Watcher::Get();
                watcher.SetPollInterval(settings.pollInterval);
                watcher.Install(settings.detectInterval);
                break;
            }
            case SKSE::MessagingInterface::kPreLoadGame:
            case SKSE::MessagingInterface::kNewGame:
                ConditionEngine::Availability::Get().RefreshLoadedPlugins();
                ConditionEngine::Papyrus::ClearScriptState();
                ConditionEngine::PushedValues::Get().ClearAll();  // pushed values are never saved
                ConditionEngine::PapyrusCalls::Get().ClearAll();
                ConditionEngine::Watcher::Get().OnGameLoad();
                break;
            case SKSE::MessagingInterface::kPostLoadGame:
                ConditionEngine::Availability::Get().RefreshLoadedPlugins();
                break;
            default:
                break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);
    InitializeLog();

    SKSE::log::info("ConditionEngine loaded ({} condition types)",
                    ConditionEngine::Registry::Get().GetTypes().size());

    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    ConditionEngine::Papyrus::Register();
    return true;
}
