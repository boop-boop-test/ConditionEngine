#include "ConditionEngine/Presets.h"
#include "ConditionEngine/Availability.h"
#include "ConditionEngine/Expression.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Weather/WeatherProfiles.h"

#include <fstream>

namespace ConditionEngine {

    namespace {
        struct BuiltinPreset {
            const char* name;
            const unsigned char* data;
            std::size_t size;
        };
#include "ConditionEngine/BuiltinPresets.inc"
    }

    std::filesystem::path GetDefaultPresetDirectory() {
        return std::filesystem::path("Data") / "SKSE" / "Plugins" / "ConditionEngine";
    }

    std::size_t LoadPresetFile(const std::filesystem::path& file) {
        nlohmann::json root;
        try {
            std::ifstream in(file);
            root = nlohmann::json::parse(in, nullptr, true, /*ignore_comments=*/true);
        } catch (const std::exception& e) {
            SKSE::log::error("ConditionEngine: failed to read preset file '{}': {}", file.string(), e.what());
            return 0;
        }
        return LoadPresetJson(root, file.filename().string());
    }

    std::size_t LoadBuiltinPresets() {
        std::size_t total = 0;
        for (const auto& preset : kBuiltinPresets) {
            nlohmann::json root;
            try {
                root = nlohmann::json::parse(preset.data, preset.data + preset.size, nullptr, true, /*ignore_comments=*/true);
            } catch (const std::exception& e) {
                SKSE::log::error("ConditionEngine: built-in preset '{}' is not valid JSON: {}", preset.name, e.what());
                continue;
            }
            total += LoadPresetJson(root, std::string(preset.name) + " (built in)");
        }
        return total;
    }

    std::size_t LoadPresetJson(const nlohmann::json& root, const std::string& source) {
        const bool hasConditions = root.is_object() && root.contains("conditions") && root["conditions"].is_object();
        const bool hasValues = root.is_object() && root.contains("values") && root["values"].is_object();
        const bool hasCategories = root.is_object() && root.contains("categories") && root["categories"].is_array();
        const bool hasWeathers = root.is_object() && root.contains("weathers");
        const bool hasDials = root.is_object() && root.contains("dials");  // read by the dials (src/Dials/Service.cpp)
        if (!hasConditions && !hasValues && !hasCategories && !hasWeathers) {
            if (hasDials) return 0;
            SKSE::log::warn("ConditionEngine: preset '{}' has no \"conditions\", \"values\", \"categories\" or \"weathers\"",
                            source);
            return 0;
        }

        auto& registry = Registry::Get();
        std::size_t count = 0;

        // Origin labels: {"NoTalkJustHands": "No Talk Just Hands"}
        if (root.contains("sources") && root["sources"].is_object()) {
            for (const auto& [id, label] : root["sources"].items()) {
                if (label.is_string()) registry.RegisterSource(id, label.get<std::string>());
            }
        }

        // Topical categories, shared by id: [{"id", "label", "parent", "order", "icon", "description"}]
        if (hasCategories) {
            for (const auto& c : root["categories"]) {
                if (!c.is_object()) continue;
                CategoryInfo category;
                category.id = c.value("id", "");
                category.label = c.value("label", "");
                category.parent = c.value("parent", "");
                category.order = c.value("order", 100);
                category.icon = c.value("icon", "");
                category.description = c.value("description", "");
                std::string error;
                if (!registry.RegisterCategory(std::move(category), c.value("source", source), &error)) {
                    SKSE::log::warn("ConditionEngine: category in '{}' skipped: {}", source, error);
                }
            }
        }

        // Corrections to weather profiles: {"TS_*_Heavy": {"fog": 75}, "0x10A241|Skyrim.esm": {"rain": 50}}
        if (hasWeathers) {
            std::vector<std::string> warnings;
            count += Weather::AddOverrides(root["weathers"], source, &warnings);
            for (const auto& w : warnings) SKSE::log::warn("ConditionEngine: preset '{}': {}", source, w);
        }

        // Values first, so value problems are reported before conditions that use them
        // ($name resolves when read, so the order does not change behavior)
        if (hasValues) {
            for (const auto& [name, definition] : root["values"].items()) {
                std::string error;
                auto def = NamedValueDef::Parse(name, definition, source, &error);
                if (!def) {
                    SKSE::log::warn("ConditionEngine: value '{}' in '{}' skipped: {}", name, source, error);
                    continue;
                }
                // A source for a mod that isn't installed is skipped when read; checking its forms now would only
                // warn that they are missing (and form lookups need the game's data)
                const bool required = def->requiresPlugins.empty() && def->requiresDlls.empty() ||
                                      Availability::Get().CheckRequirements(def->requiresPlugins, def->requiresDlls).Ready();
                if (def->value && required) {
                    for (const auto& problem : def->value->Validate()) {
                        SKSE::log::warn("ConditionEngine: value '{}' in '{}': {}", name, source, problem);
                    }
                }
                registry.RegisterNamedValue(std::move(def));
                ++count;
            }
        }

        if (!hasConditions) return count;

        for (const auto& [id, value] : root["conditions"].items()) {
            std::unique_ptr<Condition> condition;
            std::string expressionText;
            std::string error;
            NamedConditionInfo info;
            info.source = source;

            if (value.is_string()) {
                expressionText = value.get<std::string>();
                condition = Expression::CompileAny(expressionText, &error);
            } else if (value.is_object() && !value.contains("type")) {
                // Definition with picker metadata: {"expr", "label", "category", "offer", "tags", ...}
                if (!info.ReadMetadata(value, &error)) {
                    SKSE::log::warn("ConditionEngine: preset '{}' in '{}' skipped: {}", id, source, error);
                    continue;
                }
                if (value.contains("expr") && value["expr"].is_string()) {
                    expressionText = value["expr"].get<std::string>();
                    condition = Expression::CompileAny(expressionText, &error);
                } else if (value.contains("condition") && value["condition"].is_object()) {
                    condition = Condition::FromJson(value["condition"]);
                    if (!condition) error = "not a known condition type";
                } else {
                    error = "needs \"expr\" or \"condition\"";
                }
            } else if (value.is_object()) {
                condition = Condition::FromJson(value);
                if (!condition) error = "not a known condition type";
            } else {
                error = "value must be an expression string or a condition object";
            }

            if (!condition) {
                SKSE::log::warn("ConditionEngine: preset '{}' in '{}' skipped: {}", id, source, error);
                continue;
            }

            for (const auto& problem : condition->Validate()) {
                SKSE::log::warn("ConditionEngine: preset '{}' in '{}': {}", id, source, problem);
            }

            info.expression = expressionText;
            registry.RegisterNamed(id, std::shared_ptr<const Condition>(std::move(condition)), std::move(info));
            ++count;
        }
        return count;
    }

    std::size_t LoadPresetDirectory(const std::filesystem::path& directory) {
        std::error_code ec;
        if (!std::filesystem::is_directory(directory, ec)) return 0;

        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());

        std::size_t total = 0;
        for (const auto& file : files) total += LoadPresetFile(file);
        return total;
    }

} // namespace ConditionEngine
