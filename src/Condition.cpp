#include "ConditionEngine/Condition.h"
#include "ConditionEngine/Availability.h"
#include "ConditionEngine/Forms.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Stats.h"

#include <atomic>
#include <regex>
#include <sstream>

namespace ConditionEngine {

    Condition::~Condition() { delete availabilityMemo_.load(); }

    bool Condition::EvaluateAvailable(const EvaluationContext& context) const {
        return CheckAvailability().Ready() && EvaluateCounted(context);
    }

    AvailabilityStatus Condition::CheckAvailability() const {
        return detail::CachedCheck(availabilityMemo_, [this] { return Availability::Get().Check(*this); });
    }

    void Condition::InvalidateAvailability() const {
        if (auto* memo = availabilityMemo_.load(std::memory_order_acquire)) {
            std::lock_guard lock(memo->mutex);
            memo->revision = ~0ull;
        }
    }

    bool Condition::EvaluateCounted(const EvaluationContext& context) const {
        if (!Stats::Enabled()) return Evaluate(context);
        Stats::Scope scope(typeInfo ? typeInfo->statsSlot : Stats::kUnregistered, false);
        return Evaluate(context);
    }

    TriResult Condition::EvaluateTriCounted(const EvaluationContext& context) const {
        if (!Stats::Enabled()) return EvaluateTri(context);
        Stats::Scope scope(typeInfo ? typeInfo->statsSlot : Stats::kUnregistered, false);
        return EvaluateTri(context);
    }

    // ── Dot-accessor helper ─────────────────────────────────────────────────
    // Given a param reference like "MyDial.dialUUID", try to resolve it by:
    //   1. Looking for the base name ("MyDial") in args
    //   2. Parsing the value as JSON
    //   3. Extracting the sub-field ("dialUUID")
    // Returns empty string if resolution fails.
    static std::string ResolveDotAccessor(const std::string& fullName,
                                          const std::unordered_map<std::string, std::string>& args) {
        auto dotPos = fullName.find('.');
        if (dotPos == std::string::npos) return {};

        std::string baseName = fullName.substr(0, dotPos);
        std::string fieldName = fullName.substr(dotPos + 1);

        auto it = args.find(baseName);
        if (it == args.end()) return {};

        try {
            auto j = nlohmann::json::parse(it->second);
            if (j.is_object() && j.contains(fieldName)) {
                const auto& val = j[fieldName];
                if (val.is_string()) return val.get<std::string>();
                // For arrays/numbers, dump as compact JSON string
                return val.dump();
            }
        } catch (...) { /* not valid JSON, ignore */ }
        return {};
    }

    // ── SubstituteJsonValues ─────────────────────────────────────────────────
    // Recursively walk a JSON tree.  For every *string* value that contains
    // ${varName}, replace it with the corresponding entry from `args`.
    // If the fully-resolved string is a valid number, convert the JSON
    // node to a numeric type so that Condition::FromJson sees a number,
    // not a string.  This is what makes Float / Int module parameters
    // "just work" inside condition thresholds.
    void SubstituteJsonValues(nlohmann::json& json,
                              const std::unordered_map<std::string, std::string>& args) {
        if (args.empty()) return;

        if (json.is_string()) {
            std::string text = json.get<std::string>();
            static const std::regex varPattern(R"(\$\{([^}]+)\})");
            if (!std::regex_search(text, varPattern)) return;

            // Perform substitutions
            std::string result;
            std::string::const_iterator searchStart = text.cbegin();
            std::smatch match;
            while (std::regex_search(searchStart, text.cend(), match, varPattern)) {
                result.append(searchStart, match[0].first);
                std::string varName = match[1].str();
                auto it = args.find(varName);
                if (it != args.end()) {
                    result.append(it->second);
                } else {
                    // Try dot-accessor: ${param.field} → parse param value as JSON, extract field
                    std::string dotResult = ResolveDotAccessor(varName, args);
                    result.append(!dotResult.empty() ? dotResult : match[0].str());
                }
                searchStart = match.suffix().first;
            }
            result.append(searchStart, text.cend());

            // If the whole resolved value looks like a boolean, store it as one
            // so that Condition::FromJson (which calls json.value<bool>() etc.) works.
            if (result == "true" || result == "True" || result == "TRUE") {
                json = true;
                return;
            }
            if (result == "false" || result == "False" || result == "FALSE") {
                json = false;
                return;
            }

            // If the whole resolved value looks like a number, store it as one
            // so that Condition::FromJson (which calls json.value<float>() etc.) works.
            // Skip form strings like "0x00028170|Skyrim.esm" — they start with digits
            // but must stay as strings for ParseFormString to resolve them.
            if (!Forms::IsFormString(result)) {
                try {
                    if (!result.empty() && (std::isdigit(result[0]) || result[0] == '-' || result[0] == '+' || result[0] == '.')) {
                        std::size_t pos = 0;
                        if (result.find('.') != std::string::npos) {
                            float fval = std::stof(result, &pos);
                            if (pos == result.size()) {
                                json = fval;
                                return;
                            }
                        } else {
                            int ival = std::stoi(result, &pos);
                            if (pos == result.size()) {
                                json = ival;
                                return;
                            }
                        }
                    }
                } catch (...) { /* not numeric, fall through */ }
            }

            // If the resolved value looks like a JSON array or object (e.g. from a
            // dot-accessor resolving a compound sub-field like matchValues: [1,2,3]),
            // parse it back to a native JSON type so Condition::FromJson sees an
            // array/object rather than a string containing brackets.
            if (!result.empty() && (result.front() == '[' || result.front() == '{')) {
                try {
                    json = nlohmann::json::parse(result);
                    return;
                } catch (...) { /* not valid JSON, treat as plain string */ }
            }

            json = result;
            return;
        }

        if (json.is_object()) {
            for (auto& [key, val] : json.items()) {
                SubstituteJsonValues(val, args);
            }
            return;
        }

        if (json.is_array()) {
            for (auto& element : json) {
                SubstituteJsonValues(element, args);
            }
        }
    }

    // ── Condition::FromJson ──────────────────────────────────────────────────
    std::unique_ptr<Condition> Condition::FromJson(const nlohmann::json& json) {
        return Registry::Get().Create(json);
    }

    // ── Condition::ResolveParameters ─────────────────────────────────────────
    std::unique_ptr<Condition> Condition::ResolveParameters(
            const std::unordered_map<std::string, std::string>& args) const {
        if (args.empty()) return CloneComplete();

        // JSON round-trip: serialise → substitute ${var} → deserialise
        nlohmann::json j = ToJsonComplete();
        SubstituteJsonValues(j, args);
        auto resolved = Condition::FromJson(j);
        // FromJson reads forcePolled from the JSON, so it's preserved automatically
        return resolved ? std::move(resolved) : CloneComplete();
    }

    bool CompareFloat(float lhs, float rhs, ComparisonOperator op) {
        switch (op) {
            case ComparisonOperator::Equal:        return lhs == rhs;
            case ComparisonOperator::NotEqual:     return lhs != rhs;
            case ComparisonOperator::GreaterThan:  return lhs > rhs;
            case ComparisonOperator::LessThan:     return lhs < rhs;
            case ComparisonOperator::GreaterEqual: return lhs >= rhs;
            case ComparisonOperator::LessEqual:    return lhs <= rhs;
        }
        return false;
    }

    bool CompareInt(int lhs, int rhs, ComparisonOperator op) {
        switch (op) {
            case ComparisonOperator::Equal:        return lhs == rhs;
            case ComparisonOperator::NotEqual:     return lhs != rhs;
            case ComparisonOperator::GreaterThan:  return lhs > rhs;
            case ComparisonOperator::LessThan:     return lhs < rhs;
            case ComparisonOperator::GreaterEqual: return lhs >= rhs;
            case ComparisonOperator::LessEqual:    return lhs <= rhs;
        }
        return false;
    }

    std::string ComparisonOpToString(ComparisonOperator op) {
        switch (op) {
            case ComparisonOperator::Equal:        return "==";
            case ComparisonOperator::NotEqual:     return "!=";
            case ComparisonOperator::GreaterThan:  return ">";
            case ComparisonOperator::LessThan:     return "<";
            case ComparisonOperator::GreaterEqual: return ">=";
            case ComparisonOperator::LessEqual:    return "<=";
        }
        return "==";
    }

    std::string ComparisonOpToName(ComparisonOperator op) {
        switch (op) {
            case ComparisonOperator::Equal:        return "Equal";
            case ComparisonOperator::NotEqual:     return "NotEqual";
            case ComparisonOperator::GreaterThan:  return "Greater";
            case ComparisonOperator::LessThan:     return "Less";
            case ComparisonOperator::GreaterEqual: return "GreaterOrEqual";
            case ComparisonOperator::LessEqual:    return "LessOrEqual";
        }
        return "Equal";
    }

    ComparisonOperator ParseComparisonOperator(const nlohmann::json& json, const std::string& key) {
        // Check primary key first, then fall back to alternate key name
        std::string actualKey = key;
        if (!json.contains(key)) {
            if (key == "operation" && json.contains("operator")) {
                actualKey = "operator";
            } else if (key == "operator" && json.contains("operation")) {
                actualKey = "operation";
            } else {
                return ComparisonOperator::Equal;
            }
        }
        
        const auto& val = json[actualKey];
        if (val.is_number()) {
            return static_cast<ComparisonOperator>(val.get<int>());
        }
        if (val.is_string()) {
            std::string op = val.get<std::string>();
            if (op == "Equal" || op == "equal" || op == "==" || op == "Equals") return ComparisonOperator::Equal;
            if (op == "NotEqual" || op == "notEqual" || op == "!=" || op == "Not") return ComparisonOperator::NotEqual;
            if (op == "Greater" || op == "GreaterThan" || op == "greaterThan" || op == ">") return ComparisonOperator::GreaterThan;
            if (op == "Less" || op == "LessThan" || op == "lessThan" || op == "<") return ComparisonOperator::LessThan;
            if (op == "GreaterOrEqual" || op == "GreaterEqual" || op == "greaterEqual" || op == ">=") return ComparisonOperator::GreaterEqual;
            if (op == "LessOrEqual" || op == "LessEqual" || op == "lessEqual" || op == "<=") return ComparisonOperator::LessEqual;
        }
        return ComparisonOperator::Equal;
    }

    // ── List readers ─────────────────────────────────────────────────────────
    // Parameterized list values arrive as comma-separated strings after
    // ${param} substitution, so every list reader accepts both shapes.
    std::vector<std::string> ReadStringList(const nlohmann::json& json, const std::string& key) {
        std::vector<std::string> result;
        if (!json.contains(key)) return result;
        const auto& val = json[key];
        if (val.is_array()) {
            for (const auto& item : val) {
                if (item.is_string()) result.push_back(item.get<std::string>());
                else if (!item.is_null()) result.push_back(item.dump());
            }
        } else if (val.is_string()) {
            std::istringstream ss(val.get<std::string>());
            std::string token;
            while (std::getline(ss, token, ',')) {
                auto start = token.find_first_not_of(" 	");
                auto end = token.find_last_not_of(" 	");
                if (start != std::string::npos) {
                    result.push_back(token.substr(start, end - start + 1));
                }
            }
        }
        return result;
    }

    std::vector<int> ReadIntList(const nlohmann::json& json, const std::string& key) {
        std::vector<int> result;
        if (json.contains(key) && json[key].is_array()) {
            for (const auto& v : json[key]) {
                if (v.is_number()) {
                    result.push_back(v.get<int>());
                } else if (v.is_string()) {
                    try { result.push_back(std::stoi(v.get<std::string>())); } catch (...) {}
                }
            }
            return result;
        }
        for (const auto& token : ReadStringList(json, key)) {
            try { result.push_back(std::stoi(token)); } catch (...) {}
        }
        return result;
    }

    std::vector<std::uint32_t> ReadUIntList(const nlohmann::json& json, const std::string& key) {
        std::vector<std::uint32_t> result;
        if (json.contains(key) && json[key].is_array()) {
            for (const auto& v : json[key]) {
                if (v.is_number()) {
                    result.push_back(v.get<std::uint32_t>());
                } else if (v.is_string()) {
                    try { result.push_back(static_cast<std::uint32_t>(std::stoul(v.get<std::string>()))); } catch (...) {}
                }
            }
            return result;
        }
        for (const auto& token : ReadStringList(json, key)) {
            try { result.push_back(static_cast<std::uint32_t>(std::stoul(token))); } catch (...) {}
        }
        return result;
    }

    // ── Logging ──────────────────────────────────────────────────────────────
    namespace Log {
        namespace {
            std::atomic<TraceLoggerGetter> g_traceLoggerGetter{nullptr};
        }

        void SetTraceLogger(TraceLoggerGetter getter) {
            g_traceLoggerGetter.store(getter);
        }

        std::shared_ptr<spdlog::logger> GetTraceLogger() {
            auto getter = g_traceLoggerGetter.load();
            return getter ? getter() : nullptr;
        }
    }

} // namespace ConditionEngine
