#include "ConditionEngine/Conditions/GearConditions.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Forms.h"
#include "ConditionEngine/Registry.h"

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
        auto* actor = context.GetActor();
        auto* form = Resolve();
        if (!actor || !form) return false;

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
            return false;
        }
        return CompareInt(total, count_, op_);
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
