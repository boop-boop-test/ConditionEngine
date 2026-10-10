#include "ConditionEngine/Presets.h"
#include "ConditionEngine/Expression.h"
#include "ConditionEngine/Registry.h"

#include <fstream>

namespace ConditionEngine {

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

        if (!root.is_object() || !root.contains("conditions") || !root["conditions"].is_object()) {
            SKSE::log::warn("ConditionEngine: preset file '{}' has no \"conditions\" object", file.string());
            return 0;
        }

        const std::string source = file.filename().string();
        auto& registry = Registry::Get();
        std::size_t count = 0;

        for (const auto& [id, value] : root["conditions"].items()) {
            std::unique_ptr<Condition> condition;
            std::string expressionText;
            std::string error;

            if (value.is_string()) {
                expressionText = value.get<std::string>();
                condition = Expression::CompileAny(expressionText, &error);
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

            registry.RegisterNamed(id, std::shared_ptr<const Condition>(std::move(condition)), source, expressionText);
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
