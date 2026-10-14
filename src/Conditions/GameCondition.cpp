#include "ConditionEngine/Conditions/GameCondition.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Forms.h"

#include <bit>
#include <format>
#include <cctype>

namespace ConditionEngine {

    namespace {
        using ParamType = RE::SCRIPT_PARAM_TYPE;

        bool IEquals(std::string_view a, std::string_view b) {
            return a.size() == b.size() &&
                   std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                       return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
                   });
        }

        std::optional<std::int32_t> ParseInt(const std::string& text) {
            try {
                std::size_t pos = 0;
                long long value = text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0 ? std::stoll(text, &pos, 16)
                                                                                         : std::stoll(text, &pos, 10);
                if (pos == text.size()) return static_cast<std::int32_t>(value);
            } catch (...) {
            }
            return std::nullopt;
        }

        void* IntParam(std::int32_t value) {
            return reinterpret_cast<void*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(value)));
        }

        bool IsUnsupported(ParamType type) {
            switch (type) {
                case ParamType::kChar:
                case ParamType::kScriptVar:
                case ParamType::kVMScriptVar:
                case ParamType::kAlias:
                case ParamType::kPackageDataCanBeNull:
                case ParamType::kPackageDataNumeric:
                case ParamType::kPackageData:
                case ParamType::kEventFunction:
                case ParamType::kEventFunctionMember:
                case ParamType::kEventFunctionData:
                case ParamType::kVATSValue:
                case ParamType::kVATSValueData:
                case ParamType::kMiscStat:
                case ParamType::kAnimGroup:
                    return true;
                default:
                    return false;
            }
        }

        bool IsIntegral(ParamType type) {
            switch (type) {
                case ParamType::kInt:
                case ParamType::kStage:
                case ParamType::kCrimeType:
                case ParamType::kAxis:
                case ParamType::kFormType:
                case ParamType::kAlignment:
                case ParamType::kCritStage:
                case ParamType::kRelationshipRank:
                case ParamType::kWardState:
                case ParamType::kFurnitureAnimType:
                case ParamType::kFurnitureEntryType:
                    return true;
                default:
                    return false;
            }
        }

        // Converts one textual argument into the void* slot the engine expects
        std::optional<void*> ConvertParam(ParamType type, const std::string& text, std::string& error) {
            if (IsUnsupported(type)) {
                error = "parameter type " + std::to_string(static_cast<int>(type)) + " is not supported";
                return std::nullopt;
            }
            if (type == ParamType::kFloat) {
                try {
                    return IntParam(static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(std::stof(text))));
                } catch (...) {
                    error = "expected a number, got '" + text + "'";
                    return std::nullopt;
                }
            }
            if (type == ParamType::kSex) {
                if (IEquals(text, "Male")) return IntParam(0);
                if (IEquals(text, "Female")) return IntParam(1);
            }
            if (type == ParamType::kCastingSource) {
                static constexpr std::pair<std::string_view, int> kSources[] = {
                    {"Left", 0}, {"Right", 1}, {"Voice", 2}, {"Instant", 3}};
                for (const auto& [name, value] : kSources) {
                    if (IEquals(text, name)) return IntParam(value);
                }
            }
            if (type == ParamType::kActorValue) {
                if (auto* list = RE::ActorValueList::GetSingleton()) {
                    auto av = list->LookupActorValueByName(text.c_str());
                    if (av != RE::ActorValue::kNone) return IntParam(static_cast<std::int32_t>(av));
                }
            }
            if (IsIntegral(type) || type == ParamType::kSex || type == ParamType::kCastingSource ||
                type == ParamType::kActorValue) {
                if (auto value = ParseInt(text)) return IntParam(*value);
                error = "expected an integer or known name, got '" + text + "'";
                return std::nullopt;
            }

            // Every other parameter type is a form or reference
            if (auto* form = Forms::LookupAny(text)) return static_cast<void*>(form);
            error = "form '" + text + "' not found (use a form string like \"0x12EB7|Skyrim.esm\")";
            return std::nullopt;
        }

        RE::CONDITION_ITEM_DATA::OpCode ToOpCode(ComparisonOperator op) {
            using Op = RE::CONDITION_ITEM_DATA::OpCode;
            switch (op) {
                case ComparisonOperator::Equal: return Op::kEqualTo;
                case ComparisonOperator::NotEqual: return Op::kNotEqualTo;
                case ComparisonOperator::GreaterThan: return Op::kGreaterThan;
                case ComparisonOperator::GreaterEqual: return Op::kGreaterThanOrEqualTo;
                case ComparisonOperator::LessThan: return Op::kLessThan;
                case ComparisonOperator::LessEqual: return Op::kLessThanOrEqualTo;
            }
            return Op::kEqualTo;
        }

        const char* RunOnName(GameCondition::RunOn runOn) {
            switch (runOn) {
                case GameCondition::RunOn::Target: return "Target";
                case GameCondition::RunOn::Player: return "Player";
                default: return "Subject";
            }
        }
    }

    GameCondition::GameCondition(std::string function, std::vector<std::string> params, ComparisonOperator op,
                                 float value, RunOn runOn)
        : function_(std::move(function)), params_(std::move(params)), op_(op), value_(value), runOn_(runOn) {}

    GameCondition::RunOn GameCondition::ParseRunOn(const std::string& text) {
        if (IEquals(text, "Target")) return RunOn::Target;
        if (IEquals(text, "Player")) return RunOn::Player;
        return RunOn::Subject;
    }

    const GameCondition::Compiled& GameCondition::Compile() const {
        std::call_once(compileOnce_, [this]() {
            Compiled result;
            auto* first = RE::SCRIPT_FUNCTION::GetFirstScriptCommand();
            auto* command = RE::SCRIPT_FUNCTION::LocateScriptCommand(function_);
            if (!first || !command) {
                result.error = "unknown condition function '" + function_ + "'";
            } else if (!command->conditionFunction) {
                result.error = "'" + function_ + "' cannot be used as a condition";
            } else {
                result.functionId = static_cast<std::uint16_t>(command - first);
                result.ok = true;
                const auto declared = std::min<std::uint16_t>(command->numParams, 2);
                for (std::uint16_t i = 0; i < declared && result.ok; ++i) {
                    const auto& info = command->params[i];
                    if (i >= params_.size() || params_[i].empty()) {
                        if (!info.optional) {
                            result.ok = false;
                            result.error = function_ + " needs parameter " + std::to_string(i + 1) + " (" +
                                           (info.paramName ? info.paramName : "?") + ")";
                        }
                        continue;
                    }
                    std::string error;
                    auto converted = ConvertParam(info.paramType.get(), params_[i], error);
                    if (!converted) {
                        result.ok = false;
                        result.error = function_ + " parameter " + std::to_string(i + 1) + ": " + error;
                    } else {
                        result.params[i] = *converted;
                    }
                }
            }
            if (!result.ok) SKSE::log::warn("ConditionEngine Game({}): {}", function_, result.error);
            compiled_ = std::move(result);
        });
        return compiled_;
    }

    bool GameCondition::Evaluate(const EvaluationContext& context) const {
        const auto& compiled = Compile();
        if (!compiled.ok) return false;

        auto* player = RE::PlayerCharacter::GetSingleton();
        RE::TESObjectREFR* subject = runOn_ == RunOn::Player ? player : context.GetActor();
        if (!subject) return false;

        RE::TESConditionItem item;
        item.next = nullptr;
        item.data.comparisonValue.f = value_;
        item.data.functionData.function = static_cast<RE::FUNCTION_DATA::FunctionID>(compiled.functionId);
        item.data.functionData.params[0] = compiled.params[0];
        item.data.functionData.params[1] = compiled.params[1];
        item.data.flags.isOR = false;
        item.data.flags.global = false;
        item.data.flags.opCode = ToOpCode(op_);
        item.data.object = runOn_ == RunOn::Target ? RE::CONDITIONITEMOBJECT::kTarget : RE::CONDITIONITEMOBJECT::kSelf;

        RE::ConditionCheckParams params(subject, player);
        return item.IsTrue(params);
    }

    std::string GameCondition::GetDescription() const {
        std::string desc = function_;
        if (!params_.empty()) {
            desc += "(";
            for (std::size_t i = 0; i < params_.size(); ++i) {
                if (i > 0) desc += ", ";
                desc += params_[i];
            }
            desc += ")";
        }
        return desc + " " + ComparisonOpToString(op_) + " " + std::format("{}", value_);
    }

    nlohmann::json GameCondition::ToJson() const {
        nlohmann::json json;
        json["type"] = "Game";
        json["function"] = function_;
        for (std::size_t i = 0; i < params_.size() && i < 2; ++i) {
            json["param" + std::to_string(i + 1)] = params_[i];
        }
        // "!= 0" is the default (true-ish) and is left implicit
        if (!(op_ == ComparisonOperator::NotEqual && value_ == 0.0f)) {
            json["operator"] = ComparisonOpToName(op_);
            json["value"] = value_;
        }
        if (runOn_ != RunOn::Subject) json["runOn"] = RunOnName(runOn_);
        return json;
    }

    std::unique_ptr<Condition> GameCondition::Clone() const {
        return std::make_unique<GameCondition>(function_, params_, op_, value_, runOn_);
    }

    std::vector<std::string> GameCondition::Validate() const {
        if (function_.empty()) return {"Game condition: no function specified"};
        // Parameterized values are resolved later
        if (function_.find("${") != std::string::npos) return {};
        for (const auto& p : params_) {
            if (p.find("${") != std::string::npos) return {};
        }
        const auto& compiled = Compile();
        if (!compiled.ok) return {"Game condition: " + compiled.error};
        return {};
    }

} // namespace ConditionEngine
