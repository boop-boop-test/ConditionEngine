#include "ConditionEngine/Conditions/GearConditions.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Forms.h"
#include "ConditionEngine/Registry.h"
#include "ConditionEngine/Gear/ArmorProtection.h"

#include <array>
#include <format>
#include <mutex>

namespace ConditionEngine {

    namespace {
        using json = nlohmann::json;

        bool IEquals(std::string_view a, std::string_view b) {
            return a.size() == b.size() && _strnicmp(a.data(), b.data(), a.size()) == 0;
        }

        constexpr std::string_view kWeaponTypes[] = {
            "Unarmed", "Sword", "Dagger", "WarAxe", "Mace", "Greatsword", "Battleaxe",
            "Warhammer", "Bow", "Crossbow", "Staff", "Spell", "Shield", "Torch",
        };

        bool IsKnownWeaponType(const std::string& type) {
            return std::any_of(std::begin(kWeaponTypes), std::end(kWeaponTypes),
                               [&](std::string_view known) { return IEquals(known, type); });
        }

        // The protection keywords, by editor ID (the game keeps keyword editor IDs). Frostfall's exist only
        // while Frostfall is loaded; Survival_ArmorWarm / Cold are in every SE / AE Update.esm.
        const std::array<RE::BGSKeyword*, Gear::kArmorKeywordCount>& ProtectionKeywords() {
            static std::array<RE::BGSKeyword*, Gear::kArmorKeywordCount> keywords{};
            static std::once_flag once;
            std::call_once(once, [] {
                for (std::size_t i = 0; i < Gear::kArmorKeywordCount; ++i) {
                    const std::string id(Gear::ArmorKeywordEditorId(static_cast<Gear::ArmorKeyword>(i)));
                    keywords[i] = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(id);
                }
            });
            return keywords;
        }

        bool NameSaysHood(const char* name) {
            if (!name) return false;
            std::string lower(name);
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return lower.find("hood") != std::string::npos || lower.find("robe") != std::string::npos;
        }

        // Every worn armor item, from one pass over the inventory
        std::vector<RE::TESObjectARMO*> WornArmor(RE::Actor* actor) {
            std::vector<RE::TESObjectARMO*> worn;
            if (!actor) return worn;
            for (const auto& [item, data] : actor->GetInventory([](RE::TESBoundObject& o) { return o.IsArmor(); })) {
                if (data.first > 0 && data.second && data.second->IsWorn()) {
                    if (auto* armor = item->As<RE::TESObjectARMO>()) worn.push_back(armor);
                }
            }
            return worn;
        }

        Gear::ArmorPiece PieceOf(RE::TESObjectARMO* armor) {
            Gear::ArmorPiece piece;
            piece.slotMask = armor->GetSlotMask().underlying();
            const auto& keywords = ProtectionKeywords();
            for (std::size_t i = 0; i < keywords.size(); ++i) {
                if (keywords[i] && armor->HasKeyword(keywords[i])) piece.keywords.set(i);
            }
            piece.hoodName = NameSaysHood(armor->GetName());
            return piece;
        }
    }

    // ============================================================================
    // ArmorWarmthCondition
    // ============================================================================

    ArmorWarmthCondition::ArmorWarmthCondition(bool coverage, float value, ComparisonOperator op)
        : coverage_(coverage), value_(value), op_(op) {}

    ValueResult ArmorWarmthCondition::ReadValue(const EvaluationContext& context) const {
        auto* actor = context.GetActor();
        if (!actor) return ValueResult::Fail(ValueStatus::Unavailable, "no actor");
        std::vector<Gear::ArmorPiece> pieces;
        for (auto* armor : WornArmor(actor)) pieces.push_back(PieceOf(armor));
        const auto total = Gear::ScoreAll(pieces);
        return ValueResult::Of(coverage_ ? total.coverage : total.warmth);
    }

    bool ArmorWarmthCondition::Evaluate(const EvaluationContext& context) const {
        const auto r = ReadValue(context);
        return r.Ok() && CompareFloat(static_cast<float>(r.value), value_, op_);
    }

    std::vector<std::string> ArmorWarmthCondition::Breakdown(RE::Actor* actor) {
        std::vector<std::string> lines;
        for (auto* armor : WornArmor(actor)) {
            const auto p = Gear::Score(PieceOf(armor));
            lines.push_back(std::format("{} ({}, {}): warmth {}, coverage {}", armor->GetName() ? armor->GetName() : "?",
                                        Gear::GearTypeName(p.type), p.source, p.warmth, p.coverage));
        }
        return lines;
    }

    std::string ArmorWarmthCondition::GetDescription() const {
        return std::string("Armor ") + (coverage_ ? "coverage " : "warmth ") + ComparisonOpToString(op_) + " " +
               std::to_string(static_cast<int>(value_));
    }

    nlohmann::json ArmorWarmthCondition::ToJson() const {
        return {{"type", "ArmorWarmth"}, {"measure", coverage_ ? "Coverage" : "Warmth"},
                {"operator", ComparisonOpToName(op_)}, {"value", value_}};
    }

    std::unique_ptr<Condition> ArmorWarmthCondition::Clone() const {
        return std::make_unique<ArmorWarmthCondition>(coverage_, value_, op_);
    }

    // ============================================================================
    // ItemCountCondition
    // ============================================================================

    ItemCountCondition::ItemCountCondition(std::string item, int count, ComparisonOperator op)
        : item_(std::move(item)), count_(count), op_(op) {}

    RE::TESForm* ItemCountCondition::Resolve() const {
        std::call_once(resolveOnce_, [this]() {
            resolved_ = Forms::LookupAny(item_);
            if (!resolved_) SKSE::log::warn("ConditionEngine ItemCount: form '{}' not found", item_);
        });
        return resolved_;
    }

    bool ItemCountCondition::Evaluate(const EvaluationContext& context) const {
        const auto total = ReadValue(context);
        return total.Ok() && CompareInt(static_cast<int>(total.value), count_, op_);
    }

    ValueResult ItemCountCondition::ReadValue(const EvaluationContext& context) const {
        auto* actor = context.GetActor();
        if (!actor) return ValueResult::Fail(ValueStatus::Unavailable, "no actor");
        auto* form = Resolve();
        if (!form) return ValueResult::Fail(ValueStatus::Unavailable, "item not found: " + item_);

        std::int32_t total = 0;
        if (auto* keyword = form->As<RE::BGSKeyword>()) {
            // Count every item carrying the keyword
            auto counts = actor->GetInventoryCounts([keyword](RE::TESBoundObject& object) {
                auto* keywords = object.As<RE::BGSKeywordForm>();
                return keywords && keywords->HasKeyword(keyword);
            });
            for (const auto& [object, count] : counts) total += count;
        } else if (auto* bound = form->As<RE::TESBoundObject>()) {
            auto counts = actor->GetInventoryCounts([bound](RE::TESBoundObject& object) { return &object == bound; });
            for (const auto& [object, count] : counts) total += count;
        } else {
            return ValueResult::Fail(ValueStatus::Error, item_ + " is not an item or keyword");
        }
        return ValueResult::Of(total);
    }

    std::string ItemCountCondition::GetDescription() const {
        return "Count of " + item_ + " " + ComparisonOpToString(op_) + " " + std::to_string(count_);
    }

    nlohmann::json ItemCountCondition::ToJson() const {
        return {{"type", "ItemCount"}, {"item", item_}, {"count", count_}, {"operator", ComparisonOpToName(op_)}};
    }

    std::unique_ptr<Condition> ItemCountCondition::Clone() const {
        return std::make_unique<ItemCountCondition>(item_, count_, op_);
    }

    std::vector<std::string> ItemCountCondition::Validate() const {
        if (item_.empty()) return {"ItemCount condition: no item specified"};
        if (item_.find("${") != std::string::npos) return {};
        if (!Resolve()) return {"ItemCount condition: item '" + item_ + "' not found"};
        return {};
    }

    // ============================================================================
    // EquippedWeaponTypeCondition
    // ============================================================================

    EquippedWeaponTypeCondition::EquippedWeaponTypeCondition(std::vector<std::string> types, Hand hand)
        : types_(std::move(types)), hand_(hand) {}

    EquippedWeaponTypeCondition::Hand EquippedWeaponTypeCondition::ParseHand(const std::string& text) {
        if (IEquals(text, "Right")) return Hand::Right;
        if (IEquals(text, "Left")) return Hand::Left;
        if (IEquals(text, "Both")) return Hand::Both;
        return Hand::Either;
    }

    std::string EquippedWeaponTypeCondition::Classify(RE::Actor* actor, bool leftHand) {
        auto* object = actor->GetEquippedObject(leftHand);
        if (!object) return "Unarmed";
        if (object->Is(RE::FormType::Spell)) return "Spell";
        if (object->Is(RE::FormType::Armor)) return "Shield";
        if (object->Is(RE::FormType::Light)) return "Torch";

        auto* weapon = object->As<RE::TESObjectWEAP>();
        if (!weapon) return "Unarmed";
        switch (weapon->GetWeaponType()) {
            case RE::WEAPON_TYPE::kHandToHandMelee: return "Unarmed";
            case RE::WEAPON_TYPE::kOneHandSword: return "Sword";
            case RE::WEAPON_TYPE::kOneHandDagger: return "Dagger";
            case RE::WEAPON_TYPE::kOneHandAxe: return "WarAxe";
            case RE::WEAPON_TYPE::kOneHandMace: return "Mace";
            case RE::WEAPON_TYPE::kTwoHandSword: return "Greatsword";
            case RE::WEAPON_TYPE::kTwoHandAxe:
                // Battleaxes and warhammers share an animation type; vanilla tells them apart by keyword
                return weapon->HasKeywordString("WeapTypeWarhammer") ? "Warhammer" : "Battleaxe";
            case RE::WEAPON_TYPE::kBow: return "Bow";
            case RE::WEAPON_TYPE::kStaff: return "Staff";
            case RE::WEAPON_TYPE::kCrossbow: return "Crossbow";
            default: return "Unarmed";
        }
    }

    bool EquippedWeaponTypeCondition::HandMatches(RE::Actor* actor, bool leftHand) const {
        const auto held = Classify(actor, leftHand);
        return std::any_of(types_.begin(), types_.end(), [&](const std::string& t) { return IEquals(t, held); });
    }

    bool EquippedWeaponTypeCondition::Evaluate(const EvaluationContext& context) const {
        auto* actor = context.GetActor();
        if (!actor || types_.empty()) return false;
        switch (hand_) {
            case Hand::Right: return HandMatches(actor, false);
            case Hand::Left: return HandMatches(actor, true);
            case Hand::Both: return HandMatches(actor, false) && HandMatches(actor, true);
            default: return HandMatches(actor, false) || HandMatches(actor, true);
        }
    }

    std::string EquippedWeaponTypeCondition::GetDescription() const {
        std::string list;
        for (const auto& t : types_) {
            if (!list.empty()) list += "/";
            list += t;
        }
        return "Holding " + list;
    }

    nlohmann::json EquippedWeaponTypeCondition::ToJson() const {
        json j{{"type", "EquippedWeaponType"}};
        if (types_.size() == 1) {
            j["weaponTypes"] = types_.front();
        } else {
            j["weaponTypes"] = types_;
        }
        switch (hand_) {
            case Hand::Right: j["hand"] = "Right"; break;
            case Hand::Left: j["hand"] = "Left"; break;
            case Hand::Both: j["hand"] = "Both"; break;
            default: break;
        }
        return j;
    }

    std::unique_ptr<Condition> EquippedWeaponTypeCondition::Clone() const {
        return std::make_unique<EquippedWeaponTypeCondition>(types_, hand_);
    }

    std::vector<std::string> EquippedWeaponTypeCondition::Validate() const {
        if (types_.empty()) return {"EquippedWeaponType condition: no weapon type specified"};
        std::vector<std::string> errors;
        for (const auto& t : types_) {
            if (t.find("${") == std::string::npos && !IsKnownWeaponType(t)) {
                errors.push_back("EquippedWeaponType condition: unknown weapon type '" + t + "'");
            }
        }
        return errors;
    }

    // ============================================================================
    // Registration
    // ============================================================================

    void RegisterGearConditionTypes(Registry& r) {
        using namespace Field;

        auto itemCount = [](bool defaultHasItem) {
            return [defaultHasItem](const json& j) -> std::unique_ptr<Condition> {
                const bool hasOp = j.contains("operator") || j.contains("operation");
                std::string item = j.contains("item") && j["item"].is_string() ? j["item"].get<std::string>() : "";
                int count = j.value("count", 1);
                auto op = hasOp ? ParseComparisonOperator(j) : ComparisonOperator::GreaterEqual;
                if (defaultHasItem && !j.contains("count")) count = 1;
                return std::make_unique<ItemCountCondition>(std::move(item), count, op);
            };
        };

        TypeBuilder("ItemCount", "Item Count", "gear")
            .Aliases({"GetItemCount"})
            .Icon("🎒")
            .Describe("Inventory count of an item, or of all items with a keyword (ItemCount(x, 3) means >= 3)")
            .Fields({Form("item", "Item or Keyword", {"Armor", "Weapon", "Ammo", "Ingredient", "Misc", "Potion",
                                                       "Book", "Scroll", "SoulGem", "Key", "Light", "Keyword"})
                         .Required(),
                     Comparison("operator", "Operator", "GreaterOrEqual"),
                     Int("count", "Count", 1).Range(0, 65535)})
            .Positional({"item", "count"})
            .Compare("count")
            .Value({.min = 0, .integer = true})
            .Factory(itemCount(false))
            .Register(r);

        TypeBuilder("HasItem", "Has Item", "gear")
            .Icon("🎒")
            .Describe("Actor carries at least one of the item (or of items with a keyword)")
            .Fields({Form("item", "Item or Keyword", {"Armor", "Weapon", "Ammo", "Ingredient", "Misc", "Potion",
                                                       "Book", "Scroll", "SoulGem", "Key", "Light", "Keyword"})
                         .Required()})
            .Positional({"item", "count"})
            .Compare("count")
            .Factory(itemCount(true))
            .Register(r);

        TypeBuilder("ArmorWarmth", "Armor Warmth", "gear")
            .Aliases({"Warmth", "ArmorCoverage"})
            .Icon("🧥")
            .Describe("How warm (or covered against rain and snow) the worn armor keeps the actor, on Frostfall's scale: "
                      "about 90 poorly dressed, 165 ordinary, 230 warm, 300 very warm. Works for any actor, with or "
                      "without Frostfall")
            .Fields({Enum("measure", "Measure",
                          {{"Warmth", "Warmth", "Frostfall warmth points, 0-333"},
                           {"Coverage", "Coverage", "Frostfall coverage points, 0-291 (keeps rain and snow off)"}},
                          "Warmth"),
                     Comparison("operator", "Operator", "GreaterOrEqual"),
                     Int("value", "Points", 165).Range(0, 400)})
            .Positional({"measure", "value"})
            .Compare("value")
            .Value({.min = 0, .max = 400, .integer = true})
            .Factory([](const json& j) -> std::unique_ptr<Condition> {
                const bool hasOp = j.contains("operator") || j.contains("operation");
                const auto measure = j.value("measure", std::string("Warmth"));
                if (!IEquals(measure, "Warmth") && !IEquals(measure, "Coverage")) {
                    throw std::invalid_argument("measure must be Warmth or Coverage, not '" + measure + "'");
                }
                return std::make_unique<ArmorWarmthCondition>(IEquals(measure, "Coverage"), j.value("value", 0.0f),
                                                              hasOp ? ParseComparisonOperator(j) : ComparisonOperator::GreaterEqual);
            })
            .Register(r);

        TypeBuilder("EquippedWeaponType", "Held Weapon Type", "gear")
            .Aliases({"Holding", "WeaponType"})
            .Icon("🏹")
            .Describe("What the actor holds in a hand")
            .Fields({EnumList("weaponTypes", "Weapon Types",
                              Options({"Unarmed", "Sword", "Dagger", "WarAxe", "Mace", "Greatsword", "Battleaxe",
                                       "Warhammer", "Bow", "Crossbow", "Staff", "Spell", "Shield", "Torch"}))
                         .Required(),
                     Enum("hand", "Hand", Options({"Either", "Right", "Left", "Both"}), "Either")})
            .Factory([](const json& j) {
                return std::make_unique<EquippedWeaponTypeCondition>(
                    ReadStringList(j, "weaponTypes"),
                    EquippedWeaponTypeCondition::ParseHand(j.contains("hand") && j["hand"].is_string()
                                                               ? j["hand"].get<std::string>()
                                                               : "Either"));
            })
            .Register(r);
    }

} // namespace ConditionEngine
