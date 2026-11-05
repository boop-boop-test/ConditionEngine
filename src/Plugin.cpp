/// @file Plugin.cpp
/// @brief SKSE entry point for the standalone ConditionEngine.dll.

#include "Console.h"
#include "OarConditions.h"
#include "Papyrus.h"
#include "Dials/Editor.h"
#include "Dials/Events.h"
#include "Dials/Papyrus.h"
#include "Dials/Serialization.h"
#include "Dials/Service.h"
#include "Dials/Settings.h"
#include "SkyrimNet/Bridge.h"

#include "ConditionEngine/Presets.h"
#include "ConditionEngine/Availability.h"
#include "ConditionEngine/Conditions/PapyrusCallCondition.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Settings.h"
#include "ConditionEngine/Values/PushedValues.h"
#include "ConditionEngine/Watcher.h"
#include "ConditionEngine/Weather/WeatherProfiles.h"

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
            case SKSE::MessagingInterface::kPostLoad:
                ConditionEngine::OarConditions::Register();
                ConditionEngine::Dials::Service::Get().Connect();
                break;
            case SKSE::MessagingInterface::kDataLoaded: {
                ConditionEngine::Availability::Get().RefreshLoadedPlugins();
                const auto settings = ConditionEngine::LoadSettings(ConditionEngine::GetDefaultSettingsPath());
                ConditionEngine::ApplyTemperatureSettings(settings);
                ConditionEngine::Weather::PrimeEditorIds();  // reads weather editor IDs on a worker

                const auto builtin = ConditionEngine::LoadBuiltinPresets();
                SKSE::log::info("Loaded {} built-in named conditions and values (survival, weather)", builtin);
                auto count = ConditionEngine::LoadPresetDirectory(ConditionEngine::GetDefaultPresetDirectory());
                SKSE::log::info("Loaded {} named conditions and values from presets", count);

                auto& watcher = ConditionEngine::Watcher::Get();
                watcher.SetPollInterval(settings.pollInterval);
                watcher.Install(settings.detectInterval);
                ConditionEngine::Console::Install(settings.consoleCommand);

                // Dials, the editor and SkyrimNet. The dial files are read on the first main-thread task,
                // after every plugin's kDataLoaded, so other mods' named values exist by then.
                ConditionEngine::Dials::LoadSettings();
                ConditionEngine::Dials::Events::Install();
                ConditionEngine::Editor::Install();
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([]() {
                        auto& dials = ConditionEngine::Dials::Service::Get();
                        dials.Start();
                        dials.onTick = [] { ConditionEngine::SkyrimNet::Tick(); };
                        ConditionEngine::SkyrimNet::Install();
                    });
                }
                break;
            }
            case SKSE::MessagingInterface::kPreLoadGame:
            case SKSE::MessagingInterface::kNewGame:
                ConditionEngine::Availability::Get().RefreshLoadedPlugins();
                ConditionEngine::OarConditions::OnGameLoad();
                ConditionEngine::Papyrus::ClearScriptState();
                ConditionEngine::PushedValues::Get().ClearAll();  // pushed values are never saved
                ConditionEngine::PapyrusCalls::Get().ClearAll();
                ConditionEngine::Watcher::Get().OnGameLoad();
                if (message->type == SKSE::MessagingInterface::kPreLoadGame) {
                    ConditionEngine::Dials::Service::Get().OnSessionEnd();
                    break;
                }
                // a new game: no load follows
                ConditionEngine::Dials::Service::Get().OnSessionEnd();
                ConditionEngine::Dials::Service::Get().OnSessionStart();
                ConditionEngine::Dials::Events::OnSessionStart();
                ConditionEngine::SkyrimNet::OnSessionStart();
                break;
            case SKSE::MessagingInterface::kPostLoadGame:
                ConditionEngine::Availability::Get().RefreshLoadedPlugins();
                ConditionEngine::Dials::Service::Get().OnSessionStart();
                ConditionEngine::Dials::Events::OnSessionStart();
                ConditionEngine::SkyrimNet::OnSessionStart();
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
    ConditionEngine::Dials::Papyrus::Install();
    ConditionEngine::Dials::Serialization::Install();
    return true;
}
