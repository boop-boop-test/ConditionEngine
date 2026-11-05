// GearTests.cpp - Out-of-game tests for armor warmth and coverage (Gear/ArmorProtection.h): Frostfall's rules
// over worn items, the Survival Mode keyword tiers, and the ArmorWarmth type. Nothing here touches game state.

#include "ConditionEngine/ConditionEngine.h"
#include "ConditionEngine/Expression.h"
#include "ConditionEngine/Gear/ArmorProtection.h"

#include <initializer_list>
#include <vector>

namespace {
    using namespace ConditionEngine;
    using Gear::ArmorKeyword;
    using Gear::ArmorPiece;

    using CheckFn = void (*)(bool ok, const std::string& what);
    CheckFn Check = nullptr;

    // Biped slot masks: bit (slot - 30)
    constexpr std::uint32_t Slot(int slot) { return 1u << (slot - 30); }
    constexpr std::uint32_t kHead = Slot(30), kHair = Slot(31), kBody = Slot(32), kHands = Slot(33), kAmulet = Slot(35),
                            kRing = Slot(36), kFeet = Slot(37), kShield = Slot(39), kCirclet = Slot(42), kCloak = Slot(46);

    ArmorPiece Piece(std::uint32_t mask, std::initializer_list<ArmorKeyword> keywords = {}, bool hood = false) {
        ArmorPiece p;
        p.slotMask = mask;
        for (auto k : keywords) p.Set(k);
        p.hoodName = hood;
        return p;
    }

    void Expect(const ArmorPiece& piece, int warmth, int coverage, const std::string& what) {
        const auto p = Gear::Score(piece);
        Check(p.warmth == warmth && p.coverage == coverage,
              what + ": warmth " + std::to_string(p.warmth) + " coverage " + std::to_string(p.coverage) + " (" +
                  p.source + "), expected " + std::to_string(warmth) + " / " + std::to_string(coverage));
    }
}

void RunGearTests(CheckFn check) {
    Check = check;
    using K = ArmorKeyword;

    for (std::size_t i = 0; i < Gear::kArmorKeywordCount; ++i) {
        Check(!Gear::ArmorKeywordEditorId(static_cast<K>(i)).empty(), "every protection keyword has an editor ID");
    }

    // ── Defaults (Frostfall's analysis of items it knows nothing about) ──────
    Expect(Piece(kBody), 110, 54, "plain body armor");
    Expect(Piece(kHead | kHair), 30, 29, "plain helmet");
    Expect(Piece(kHead, {}, true), 25, 43, "a hood keeps less warm and more dry than a helmet");
    Expect(Piece(kHands), 12, 14, "plain gloves");
    Expect(Piece(kFeet), 12, 14, "plain boots");
    Expect(Piece(kCloak), 20, 20, "plain cloak (slot 46)");
    Expect(Piece(0, {K::ClothingCloak}), 20, 20, "a cloak by WAF_ClothingCloak alone");
    Expect(Piece(kShield), 0, 20, "a shield keeps rain off, not cold");
    Expect(Piece(kBody | kHead | kHair, {}, true), 110 + 25, 54 + 43, "hooded robes: body plus a hood");
    Expect(Piece(kBody | kHands | kFeet), 110 + 12 + 12, 54 + 14 + 14, "a body item that covers hands and feet");
    Expect(Piece(kRing), 0, 0, "a ring counts for nothing");
    Expect(Piece(kAmulet), 0, 0, "an amulet counts for nothing");
    Expect(Piece(kCirclet), 0, 0, "a lone circlet counts for nothing");
    Expect(Piece(Slot(44)), 0, 0, "an item in no protecting slot");

    // ── Survival Mode keywords move vanilla gear one tier ────────────────────
    Expect(Piece(kBody, {K::SurvivalWarm}), 140, 72, "Survival_ArmorWarm body: Good");
    Expect(Piece(kBody, {K::SurvivalCold}), 60, 35, "Survival_ArmorCold body: Poor");
    Expect(Piece(kFeet, {K::SurvivalCold}), 7, 6, "Survival_ArmorCold boots: Poor");
    Expect(Piece(kHead, {K::SurvivalWarm}, true), 40, 43, "a warm hood: Good head");

    // ── Frostfall keywords (with FrostfallEnableKeywordProtection) ──────────
    Expect(Piece(kBody, {K::FrostfallEnable, K::WarmthExcellent, K::CoverageGood}), 175, 72, "Frostfall body Excellent / Good");
    Expect(Piece(kBody, {K::FrostfallEnable, K::WarmthExcellent, K::CoverageGood, K::ExtraCloakFur}), 175 + 40, 72 + 12,
           "a body item that is also a fur cloak");
    Expect(Piece(kHead, {K::FrostfallEnable, K::WarmthGood, K::CoverageFair, K::ExtraHeadWeatherproof}), 40 + 30, 29 + 43,
           "a helmet with a weatherproof hood");
    Expect(Piece(kBody, {K::WarmthMax, K::CoverageMax}), 110, 54, "Frostfall tiers without the enable keyword are ignored");
    Expect(Piece(kBody, {K::FrostfallEnable}), 0, 0, "the enable keyword with no values counts for nothing, as in Frostfall");
    Expect(Piece(kBody, {K::FrostfallEnable, K::FrostfallIgnore, K::WarmthMax}), 0, 0, "FrostfallIgnore");
    Expect(Piece(kBody, {K::FrostfallEnable, K::WarmthPoor}), 60, 0, "a warmth tier alone leaves coverage at 0");
    Expect(Piece(kCloak, {K::FrostfallEnable, K::IsCloakFur}), 40, 12, "fur cloak: warm, not waterproof");
    Expect(Piece(kCloak, {K::FrostfallEnable, K::IsCloakLeather}), 12, 40, "leather cloak: waterproof, not warm");
    Expect(Piece(kCloak, {K::FrostfallEnable, K::IsCloakCloth}), 20, 20, "cloth cloak");
    Expect(Piece(kHands, {K::FrostfallEnable, K::IsWarmAccessory}), 12, 6, "a warm accessory");
    Expect(Piece(kCloak, {K::FrostfallEnable, K::IsCloakFur, K::ExtraHeadWarm}), 40, 12,
           "extra parts only count on body and head items");
    Expect(Piece(kBody, {K::FrostfallEnable, K::WarmthFair, K::WarmthGood}), 140, 0, "the highest tier keyword wins");

    // ── Totals ───────────────────────────────────────────────────────────────
    {
        std::vector<ArmorPiece> outfit = {Piece(kBody), Piece(kHead | kHair), Piece(kHands), Piece(kFeet)};
        const auto total = Gear::ScoreAll(outfit);
        Check(total.warmth == 164 && total.coverage == 111, "an ordinary armor set: 164 / 111, got " +
                                                                std::to_string(total.warmth) + " / " + std::to_string(total.coverage));
        Check(Gear::ScoreAll(std::vector<ArmorPiece>{}).warmth == 0, "naked: 0");

        std::vector<ArmorPiece> best;
        for (auto mask : {kBody, kHead, kHands, kFeet, kCloak}) best.push_back(Piece(mask, {K::FrostfallEnable, K::WarmthMax, K::CoverageMax}));
        const auto max = Gear::ScoreAll(best);
        Check(max.warmth == Gear::kMaxWarmth && max.coverage == Gear::kMaxCoverage,
              "every part at Max: " + std::to_string(max.warmth) + " / " + std::to_string(max.coverage));
    }

    // ── The ArmorWarmth type ─────────────────────────────────────────────────
    {
        Expression::ParseError error;
        auto parsed = Expression::ParseToJson("ArmorWarmth(Coverage) >= 110", &error);
        Check(parsed && (*parsed)["type"] == "ArmorWarmth" && (*parsed)["measure"] == "Coverage" && (*parsed)["value"] == 110,
              "ArmorWarmth(Coverage) >= 110 parses: " + (parsed ? parsed->dump() : error.message));

        std::string compileError;
        auto bare = Expression::CompileAny("ArmorWarmth", &compileError);
        Check(bare && bare->ToJson()["measure"] == "Warmth", "a bare ArmorWarmth measures warmth: " + compileError);
        Check(Expression::CompileAny("ArmorWarmth(Hotness)", &compileError) == nullptr, "an unknown measure does not compile");

        EvaluationContext context(nullptr);
        auto value = Expression::CompileValueAny("ArmorWarmth(Warmth)", &compileError);
        Check(value != nullptr, "ArmorWarmth compiles as a value: " + compileError);
        if (value) {
            auto r = value->Read(context);
            Check(r.status == ValueStatus::Unavailable, "no actor: ArmorWarmth is unavailable (" + r.reason + ")");
            Check(value->ReadsActor(), "ArmorWarmth is per actor");
        }
    }
}
