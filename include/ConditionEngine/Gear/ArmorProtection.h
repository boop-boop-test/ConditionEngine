#pragma once

// ArmorProtection.h - How warm and how covered an actor's worn armor keeps them, for any actor.
//
// The rules are Frostfall's (_Frost_ArmorProtectionDatastoreHandler), the scale most armor mods tag for:
//   - an item's gear type comes from its slots: body (32), head (30 / 31), hands (33), feet (37), cloak (46 or
//     the WAF_ClothingCloak keyword), shield (39) counts as misc; rings, amulets and lone circlets count for nothing;
//   - Frostfall's keywords set the values when the item also has FrostfallEnableKeywordProtection:
//     FrostfallWarmth / FrostfallCoverage Poor ... Max per gear type, the cloak and accessory keywords, and the
//     ExtraHead / ExtraCloak keywords for body or head items that also act as a hood or a cloak;
//   - otherwise Frostfall's defaults by gear type, and an item that also covers the head, hands, feet or cloak slot
//     adds that part's default. Frostfall itself keeps its own table for vanilla armor; in its place the Survival
//     Mode keywords that every SE / AE game has in Update.esm move an item one tier: Survival_ArmorWarm up to
//     Good, Survival_ArmorCold down to Poor.
// Totals: all Poor about 90 warmth, ordinary (Fair) about 165, Good with a cloak about 230, Excellent about 300,
// the most 333. Coverage (protection from rain and snow): about 60 / 110 / 175 / 240, the most 291.
//
// Nothing here touches game state; the ArmorWarmth condition type reads the worn armor.

#include <bitset>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace ConditionEngine::Gear {

    // The keywords the rules look at
    enum class ArmorKeyword : std::uint8_t {
        FrostfallEnable,  // FrostfallEnableKeywordProtection: the Frostfall keywords below apply
        FrostfallIgnore,  // FrostfallIgnore: the item counts for nothing
        WarmthPoor, WarmthFair, WarmthGood, WarmthExcellent, WarmthMax,
        CoveragePoor, CoverageFair, CoverageGood, CoverageExcellent, CoverageMax,
        IsCloakCloth, IsCloakLeather, IsCloakFur, IsWeatherproofAccessory, IsWarmAccessory,
        ExtraHeadCloth, ExtraHeadWeatherproof, ExtraHeadWarm, ExtraCloakCloth, ExtraCloakLeather, ExtraCloakFur,
        ClothingCloak,     // WAF_ClothingCloak
        SurvivalWarm,      // Survival_ArmorWarm (Update.esm)
        SurvivalCold,      // Survival_ArmorCold (Update.esm)
        kTotal
    };
    inline constexpr std::size_t kArmorKeywordCount = static_cast<std::size_t>(ArmorKeyword::kTotal);

    // The editor ID of each keyword ("FrostfallWarmthPoor", "WAF_ClothingCloak", ...)
    std::string_view ArmorKeywordEditorId(ArmorKeyword keyword);

    enum class GearType : std::uint8_t { NotFound, Body, Head, Hands, Feet, Cloak, Misc, Ignore };
    std::string_view GearTypeName(GearType type);

    struct ArmorPiece {
        std::uint32_t slotMask = 0;  // biped slots: bit 0 = slot 30 (head) ... bit 16 = slot 46 (cloak)
        std::bitset<kArmorKeywordCount> keywords;
        bool hoodName = false;       // the name says hood or robes (a hood keeps warm differently from a helmet)

        bool Has(ArmorKeyword k) const { return keywords.test(static_cast<std::size_t>(k)); }
        void Set(ArmorKeyword k) { keywords.set(static_cast<std::size_t>(k)); }
    };

    struct Protection {
        int warmth = 0;
        int coverage = 0;
        GearType type = GearType::NotFound;
        std::string source;  // "Frostfall keywords", "Survival_ArmorWarm", "defaults", "ignored"
    };

    GearType GearTypeOf(const ArmorPiece& piece);

    // One item's warmth and coverage, with every part it covers
    Protection Score(const ArmorPiece& piece);

    // The sum over worn items
    Protection ScoreAll(std::span<const ArmorPiece> pieces);

    // The highest totals the rules can give (every part at Max)
    inline constexpr int kMaxWarmth = 190 + 55 + 24 + 24 + 40;
    inline constexpr int kMaxCoverage = 109 + 72 + 35 + 35 + 40;

} // namespace ConditionEngine::Gear
