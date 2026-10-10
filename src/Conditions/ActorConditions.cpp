#include "ConditionEngine/Conditions/ActorConditions.h"
#include "ConditionEngine/Conditions/EquipmentConditions.h"
#include "ConditionEngine/EvaluationContext.h"
#include "ConditionEngine/Forms.h"
#include "ConditionEngine/Registry.h"

namespace ConditionEngine {

    // ============================================================================
    // ActorFlagCondition
    // ============================================================================

    ActorFlagCondition::ActorFlagCondition(std::string type, std::string description, Predicate predicate,
                                           EventDependency dependencies, bool expectedState)
        : type_(std::move(type)), description_(std::move(description)), predicate_(predicate),
          dependencies_(dependencies), expectedState_(expectedState) {}

    bool ActorFlagCondition::Evaluate(const EvaluationContext& context) const {
        auto* actor = context.GetActor();
        if (!actor || !predicate_) return false;
        return predicate_(actor) == expectedState_;
    }

    std::string ActorFlagCondition::GetDescription() const {
        return (expectedState_ ? "Actor " : "Actor not: ") + description_;
    }

    nlohmann::json ActorFlagCondition::ToJson() const {
        return {{"type", type_}, {"expectedState", expectedState_}};
    }

    std::unique_ptr<Condition> ActorFlagCondition::Clone() const {
        return std::make_unique<ActorFlagCondition>(type_, description_, predicate_, dependencies_, expectedState_);
    }

    // ============================================================================
    // ActorFormCondition
    // ============================================================================

    ActorFormCondition::ActorFormCondition(std::string type, std::string field, std::string description,
                                           Predicate predicate, EventDependency dependencies,
                                           std::vector<std::string> forms, bool matchAny)
        : type_(std::move(type)), field_(std::move(field)), description_(std::move(description)),
          predicate_(predicate), dependencies_(dependencies), forms_(std::move(forms)), matchAny_(matchAny) {}

    const std::vector<RE::TESForm*>& ActorFormCondition::Resolve() const {
        std::call_once(resolveOnce_, [this]() {
            for (const auto& id : forms_) {
                if (auto* form = Forms::LookupAny(id)) {
                    resolved_.push_back(form);
                } else {
                    SKSE::log::warn("ConditionEngine {}: form '{}' not found", type_, id);
                }
            }
        });
        return resolved_;
    }

    bool ActorFormCondition::Evaluate(const EvaluationContext& context) const {
        auto* actor = context.GetActor();
        if (!actor || !predicate_ || forms_.empty()) return false;
        const auto& forms = Resolve();
        // An unresolved form can never match
        if (forms.empty()) return false;
        if (!matchAny_ && forms.size() != forms_.size()) return false;

        auto matches = [&](RE::TESForm* form) { return predicate_(actor, form); };
        return matchAny_ ? std::any_of(forms.begin(), forms.end(), matches)
                         : std::all_of(forms.begin(), forms.end(), matches);
    }

    std::string ActorFormCondition::GetDescription() const {
        std::string list;
        for (const auto& f : forms_) {
            if (!list.empty()) list += matchAny_ ? " or " : " and ";
            list += f;
        }
        return description_ + " " + list;
    }

    nlohmann::json ActorFormCondition::ToJson() const {
        nlohmann::json json{{"type", type_}};
        if (forms_.size() == 1) {
            json[field_] = forms_.front();
        } else {
            json[field_] = forms_;
        }
        if (!matchAny_) json["matchAny"] = false;
        return json;
    }

    std::unique_ptr<Condition> ActorFormCondition::Clone() const {
        return std::make_unique<ActorFormCondition>(type_, field_, description_, predicate_, dependencies_, forms_,
                                                    matchAny_);
    }

    std::vector<std::string> ActorFormCondition::Validate() const {
        if (forms_.empty()) return {type_ + " condition: no " + field_ + " specified"};
        for (const auto& f : forms_) {
            if (f.find("${") != std::string::npos) return {};
        }
        std::vector<std::string> errors;
        if (Resolve().size() != forms_.size()) {
            errors.push_back(type_ + " condition: one or more forms could not be found");
        }
        return errors;
    }

    // ============================================================================
    // LevelCondition / SexCondition
    // ============================================================================

    LevelCondition::LevelCondition(int level, ComparisonOperator op) : level_(level), op_(op) {}

    bool LevelCondition::Evaluate(const EvaluationContext& context) const {
        auto* actor = context.GetActor();
        return actor && CompareInt(actor->GetLevel(), level_, op_);
    }

    std::string LevelCondition::GetDescription() const {
        return "Level " + ComparisonOpToString(op_) + " " + std::to_string(level_);
    }

    nlohmann::json LevelCondition::ToJson() const {
        return {{"type", "Level"}, {"level", level_}, {"operator", ComparisonOpToString(op_)}};
    }

    std::unique_ptr<Condition> LevelCondition::Clone() const {
        return std::make_unique<LevelCondition>(level_, op_);
    }

    SexCondition::SexCondition(bool female) : female_(female) {}

    bool SexCondition::Evaluate(const EvaluationContext& context) const {
        auto* actor = context.GetActor();
        auto* base = actor ? actor->GetActorBase() : nullptr;
        return base && (base->GetSex() == RE::SEX::kFemale) == female_;
    }

    nlohmann::json SexCondition::ToJson() const {
        return {{"type", "Sex"}, {"sex", female_ ? "Female" : "Male"}};
    }

    // ============================================================================
    // Predicates
    // ============================================================================

    namespace {
        using json = nlohmann::json;

        bool IsSneaking(RE::Actor* a) { return a->IsSneaking(); }
        bool IsFollower(RE::Actor* a) { return a->IsPlayerRef() || a->IsPlayerTeammate(); }
        bool IsPlayer(RE::Actor* a) { return a->IsPlayerRef(); }

        bool IsSitting(RE::Actor* a) {
            auto* state = a->AsActorState();
            if (!state) return false;
            auto sit = state->GetSitSleepState();
            return sit >= RE::SIT_SLEEP_STATE::kWantToSit && sit <= RE::SIT_SLEEP_STATE::kWantToStand && !a->IsOnMount();
        }

        bool IsBleedout(RE::Actor* a) {
            auto* state = a->AsActorState();
            return state && state->IsBleedingOut();
        }

        bool IsWeaponDrawn(RE::Actor* a) {
            auto* state = a->AsActorState();
            return state && state->IsWeaponDrawn();
        }

        bool IsInDialogue(RE::Actor* a) {
            auto* topics = RE::MenuTopicManager::GetSingleton();
            if (!topics) return false;
            if (a->IsPlayerRef()) {
                auto* ui = RE::UI::GetSingleton();
                return ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
            }
            const auto handle = a->GetHandle();
            return topics->IsCurrentSpeaker(handle) || topics->lastSpeaker == handle;
        }

        bool IsActorForm(RE::Actor* a, RE::TESForm* form) {
            if (form == a) return true;
            auto* base = a->GetActorBase();
            if (base && form == base) return true;
            // Leveled/templated NPCs: compare against the root template
            auto* root = base ? base->GetRootFaceNPC() : nullptr;
            return root && form == root;
        }

        bool HasKeywordForm(RE::Actor* a, RE::TESForm* form) {
            auto* keyword = form->As<RE::BGSKeyword>();
            return keyword && a->HasKeyword(keyword);
        }

        bool HasMagicEffectForm(RE::Actor* a, RE::TESForm* form) {
            auto* target = a->AsMagicTarget();
            if (!target) return false;
            if (auto* effect = form->As<RE::EffectSetting>()) return target->HasMagicEffect(effect);
            // A keyword matches any active effect carrying it
            if (auto* keyword = form->As<RE::BGSKeyword>()) return target->HasMagicEffectWithKeyword(keyword, nullptr);
            return false;
        }

        bool HasPerkForm(RE::Actor* a, RE::TESForm* form) {
            auto* perk = form->As<RE::BGSPerk>();
            return perk && a->HasPerk(perk);
        }

        bool HasSpellForm(RE::Actor* a, RE::TESForm* form) {
            auto* spell = form->As<RE::SpellItem>();
            return spell && a->HasSpell(spell);
        }

        bool WornHasKeywordForm(RE::Actor* a, RE::TESForm* form) {
            auto* keyword = form->As<RE::BGSKeyword>();
            if (!keyword) return false;
            for (std::uint32_t slot = 30; slot <= 61; ++slot) {
                auto bipedSlot = static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(1u << (slot - 30));
                auto* armor = a->GetWornArmor(bipedSlot);
                if (armor && armor->HasKeyword(keyword)) return true;
            }
            return false;
        }

        ConditionFactory Flag(std::string type, std::string description, ActorFlagCondition::Predicate predicate,
                              EventDependency deps) {
            return [=](const json& j) {
                return std::make_unique<ActorFlagCondition>(type, description, predicate, deps,
                                                            j.value("expectedState", true));
            };
        }

        ConditionFactory FormList(std::string type, std::string field, std::string description,
                                  ActorFormCondition::Predicate predicate, EventDependency deps) {
            return [=](const json& j) {
                return std::make_unique<ActorFormCondition>(type, field, description, predicate, deps,
                                                            ReadStringList(j, field), j.value("matchAny", true));
            };
        }

        void Add(Registry& r, std::string name, std::vector<std::string> aliases, ConditionFactory factory,
                 std::vector<std::string> positional, std::string compareField, std::string description) {
            TypeInfo info;
            info.name = std::move(name);
            info.aliases = std::move(aliases);
            info.factory = std::move(factory);
            info.positional = std::move(positional);
            info.compareField = std::move(compareField);
            info.description = std::move(description);
            r.RegisterType(std::move(info));
        }
    }

    std::uint32_t ComputeActorStateFlags(RE::Actor* actor) {
        if (!actor) return 0;
        std::uint32_t flags = 0;
        if (IsSneaking(actor)) flags |= 1u << 0;
        if (IsSitting(actor)) flags |= 1u << 1;
        if (IsInDialogue(actor)) flags |= 1u << 2;
        if (IsBleedout(actor)) flags |= 1u << 3;
        if (IsWeaponDrawn(actor)) flags |= 1u << 4;
        if (IsFollower(actor)) flags |= 1u << 5;
        return flags;
    }

    void RegisterActorConditionTypes(Registry& r) {
        const auto state = EventDependency::ActorState;
        Add(r, "Sneaking", {"IsSneaking"}, Flag("Sneaking", "is sneaking", IsSneaking, state), {"expectedState"}, {},
            "Actor is sneaking");
        Add(r, "Sitting", {"IsSitting"}, Flag("Sitting", "is sitting", IsSitting, state), {"expectedState"}, {},
            "Actor is sitting in furniture");
        Add(r, "InDialogue", {"IsInDialogue", "Talking"}, Flag("InDialogue", "is in dialogue", IsInDialogue, state),
            {"expectedState"}, {}, "Actor is in a dialogue conversation");
        Add(r, "Bleedout", {"IsBleedingOut"}, Flag("Bleedout", "is bleeding out", IsBleedout, state),
            {"expectedState"}, {}, "Actor is in bleedout");
        Add(r, "WeaponDrawn", {"IsWeaponDrawn"}, Flag("WeaponDrawn", "has weapon drawn", IsWeaponDrawn, state),
            {"expectedState"}, {}, "Actor has weapons/spells drawn");
        Add(r, "IsFollower", {"Follower", "IsPlayerTeammate"},
            Flag("IsFollower", "is a follower", IsFollower, state), {"expectedState"}, {},
            "Actor is the player or a player teammate");
        Add(r, "IsPlayer", {}, Flag("IsPlayer", "is the player", IsPlayer, EventDependency::None), {"expectedState"},
            {}, "Actor is the player");

        Add(r, "IsActor", {"ActorIs"},
            FormList("IsActor", "actor", "Actor is", IsActorForm, EventDependency::None), {"actor", "matchAny"}, {},
            "Actor is a specific reference or base NPC");
        Add(r, "HasKeyword", {"ActorHasKeyword"},
            FormList("HasKeyword", "keywords", "Actor has keyword", HasKeywordForm, EventDependency::None),
            {"keywords", "matchAny"}, {}, "Actor (race or base) has keyword");
        Add(r, "HasMagicEffect", {},
            FormList("HasMagicEffect", "effects", "Actor has effect", HasMagicEffectForm, EventDependency::MagicEffect),
            {"effects", "matchAny"}, {}, "Active magic effect (or an effect with the keyword)");
        Add(r, "HasPerk", {},
            FormList("HasPerk", "perks", "Actor has perk", HasPerkForm, EventDependency::MagicEffect),
            {"perks", "matchAny"}, {}, "Actor has perk");
        Add(r, "HasSpell", {},
            FormList("HasSpell", "spells", "Actor has spell", HasSpellForm, EventDependency::MagicEffect),
            {"spells", "matchAny"}, {}, "Actor knows spell/ability");
        Add(r, "WornHasKeyword", {},
            FormList("WornHasKeyword", "keywords", "Worn armor has keyword", WornHasKeywordForm,
                     EventDependency::Equipment),
            {"keywords", "matchAny"}, {}, "Any worn armor has the keyword (e.g. ArmorHeavy, ClothingRich)");

        Add(r, "Level", {"ActorLevel"},
            [](const json& j) {
                const bool hasOp = j.contains("operator") || j.contains("operation");
                return std::make_unique<LevelCondition>(j.value("level", 1),
                                                        hasOp ? ParseComparisonOperator(j) : ComparisonOperator::GreaterEqual);
            },
            {"level"}, "level", "Actor level comparison (Level(20) means >= 20)");

        Add(r, "Sex", {"Gender"},
            [](const json& j) -> std::unique_ptr<Condition> {
                if (j.contains("sex") && j["sex"].is_number()) return std::make_unique<SexCondition>(j["sex"].get<int>() == 1);
                std::string sex = j.contains("sex") && j["sex"].is_string() ? j["sex"].get<std::string>() : "Female";
                return std::make_unique<SexCondition>(_stricmp(sex.c_str(), "female") == 0 || sex == "1");
            },
            {"sex"}, {}, "Actor sex (Male/Female)");
    }

} // namespace ConditionEngine
