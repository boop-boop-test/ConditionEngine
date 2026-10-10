/// @file Plugin.cpp
/// @brief SKSE entry point for the standalone ConditionEngine.dll.

#include "Papyrus.h"

#include "ConditionEngine/Presets.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Settings.h"
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
                const auto settings = ConditionEngine::LoadSettings(ConditionEngine::GetDefaultSettingsPath());
                ConditionEngine::ApplyTemperatureSettings(settings);

                auto count = ConditionEngine::LoadPresetDirectory(ConditionEngine::GetDefaultPresetDirectory());
                SKSE::log::info("Loaded {} named condition presets", count);

                auto& watcher = ConditionEngine::Watcher::Get();
                watcher.SetPollInterval(settings.pollInterval);
                watcher.Install(settings.detectInterval);
                break;
            }
            case SKSE::MessagingInterface::kPreLoadGame:
            case SKSE::MessagingInterface::kNewGame:
                ConditionEngine::Papyrus::ClearScriptState();
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
