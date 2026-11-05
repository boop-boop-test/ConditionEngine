#include "ConditionEngine/Gear/ArmorProtection.h"

#include <array>

namespace ConditionEngine::Gear {

    namespace {
        // Frostfall 3.4.1, _Frost_ArmorProtectionDatastoreHandler: WARMTH_* / COVERAGE_* by gear type, Poor ... Max
        struct Tiers {
            std::array<int, 5> warmth;
            std::array<int, 5> coverage;
        };
        constexpr Tiers kBody{{60, 110, 140, 175, 190}, {35, 54, 72, 91, 109}};
        constexpr Tiers kHead{{15, 30, 40, 50, 55}, {14, 29, 43, 58, 72}};
        constexpr Tiers kHands{{9, 12, 15, 21, 24}, {6, 14, 21, 29, 35}};
        constexpr Tiers kFeet{{7, 12, 15, 21, 24}, {6, 14, 21, 29, 35}};
        constexpr Tiers kCloak{{6, 12, 20, 30, 40}, {6, 12, 20, 30, 40}};
        constexpr Tiers kMisc{{6, 12, 20, 30, 40}, {6, 12, 20, 30, 40}};
        enum Tier { kPoor, kFair, kGood, kExcellent, kMax };

        // DEFAULT_*: items Frostfall knows nothing about
        // (equal to the Fair tier except for cloaks, hoods and shields)
        constexpr int kDefaultHoodWarmth = 25, kDefaultHoodCoverage = 43;
        constexpr int kDefaultShieldWarmth = 0, kDefaultShieldCoverage = 20;
        constexpr int kDefaultCloakWarmth = 20, kDefaultCloakCoverage = 20;

        // Slot masks (biped slot 30 = bit 0)
        constexpr std::uint32_t kHeadSlot = 0x1, kHairSlot = 0x2, kBodySlot = 0x4, kHandsSlot = 0x8, kAmuletSlot = 0x20,
                                kRingSlot = 0x40, kFeetSlot = 0x80, kShieldSlot = 0x200, kCircletSlot = 0x1000,
                                kCloakSlot = 0x10000;

        const Tiers* TiersOf(GearType type) {
            switch (type) {
                case GearType::Body: return &kBody;
                case GearType::Head: return &kHead;
                case GearType::Hands: return &kHands;
                case GearType::Feet: return &kFeet;
                case GearType::Cloak: return &kCloak;
                case GearType::Misc: return &kMisc;
                default: return nullptr;
            }
        }

        constexpr std::array<std::string_view, kArmorKeywordCount> kEditorIds = {
            "FrostfallEnableKeywordProtection", "FrostfallIgnore",
            "FrostfallWarmthPoor", "FrostfallWarmthFair", "FrostfallWarmthGood", "FrostfallWarmthExcellent", "FrostfallWarmthMax",
            "FrostfallCoveragePoor", "FrostfallCoverageFair", "FrostfallCoverageGood", "FrostfallCoverageExcellent",
            "FrostfallCoverageMax",
            "FrostfallIsCloakCloth", "FrostfallIsCloakLeather", "FrostfallIsCloakFur", "FrostfallIsWeatherproofAccessory",
            "FrostfallIsWarmAccessory",
            "FrostfallExtraHeadCloth", "FrostfallExtraHeadWeatherproof", "FrostfallExtraHeadWarm", "FrostfallExtraCloakCloth",
            "FrostfallExtraCloakLeather", "FrostfallExtraCloakFur",
            "WAF_ClothingCloak", "Survival_ArmorWarm", "Survival_ArmorCold"};

        struct Pair {
            int warmth;
            int coverage;
        };
        // Combination keywords (OverrideValues): the item's own value
        constexpr std::pair<ArmorKeyword, Pair> kOverrides[] = {
            {ArmorKeyword::IsCloakCloth, {20, 20}},           // cloak Good / Good
            {ArmorKeyword::IsCloakLeather, {12, 40}},         // cloak Fair / Max
            {ArmorKeyword::IsCloakFur, {40, 12}},             // cloak Max / Fair
            {ArmorKeyword::IsWeatherproofAccessory, {6, 12}}, // misc Poor / Fair
            {ArmorKeyword::IsWarmAccessory, {12, 6}},         // misc Fair / Poor
        };
        // Extra parts of a body or head item
        constexpr std::pair<ArmorKeyword, Pair> kExtraHead[] = {
            {ArmorKeyword::ExtraHeadCloth, {30, 29}},
            {ArmorKeyword::ExtraHeadWeatherproof, {30, 43}},
            {ArmorKeyword::ExtraHeadWarm, {40, 29}},
        };
        constexpr std::pair<ArmorKeyword, Pair> kExtraCloak[] = {
            {ArmorKeyword::ExtraCloakCloth, {12, 12}},
            {ArmorKeyword::ExtraCloakLeather, {12, 40}},
            {ArmorKeyword::ExtraCloakFur, {40, 12}},
        };

        Protection Ignored(GearType type, std::string why) { return {0, 0, type == GearType::NotFound ? GearType::Ignore : type, std::move(why)}; }

        // GetArmorProtectionDataByKeyword. Returns false when the item has no Frostfall keyword data.
        bool ScoreByKeyword(const ArmorPiece& piece, GearType type, Protection& out) {
            if (!piece.Has(ArmorKeyword::FrostfallEnable)) return false;
            if (piece.Has(ArmorKeyword::FrostfallIgnore) || type == GearType::NotFound) {
                out = Ignored(type, "FrostfallIgnore");
                return true;
            }
            out = {0, 0, type, "Frostfall keywords"};
            const Tiers* tiers = TiersOf(type);
            bool found = false;
            for (int t = kPoor; t <= kMax; ++t) {
                if (piece.Has(static_cast<ArmorKeyword>(static_cast<int>(ArmorKeyword::WarmthPoor) + t))) {
                    out.warmth = tiers->warmth[t];
                    found = true;
                }
            }
            for (int t = kPoor; t <= kMax; ++t) {
                if (piece.Has(static_cast<ArmorKeyword>(static_cast<int>(ArmorKeyword::CoveragePoor) + t))) {
                    out.coverage = tiers->coverage[t];
                    found = true;
                }
            }
            if (!found) {
                for (const auto& [keyword, value] : kOverrides) {
                    if (!piece.Has(keyword)) continue;
                    out.warmth = value.warmth;
                    out.coverage = value.coverage;
                    found = true;
                }
            }
            // Frostfall keeps an item with the enable keyword and no values at zero
            if (found && (type == GearType::Body || type == GearType::Head)) {
                Pair head{0, 0}, cloak{0, 0};
                for (const auto& [keyword, value] : kExtraHead) {
                    if (piece.Has(keyword)) head = value;
                }
                for (const auto& [keyword, value] : kExtraCloak) {
                    if (piece.Has(keyword)) cloak = value;
                }
                out.warmth += head.warmth + cloak.warmth;
                out.coverage += head.coverage + cloak.coverage;
            }
            return true;
        }
    }

    std::string_view ArmorKeywordEditorId(ArmorKeyword keyword) {
        const auto i = static_cast<std::size_t>(keyword);
        return i < kArmorKeywordCount ? kEditorIds[i] : std::string_view{};
    }

    std::string_view GearTypeName(GearType type) {
        switch (type) {
            case GearType::Body: return "body";
            case GearType::Head: return "head";
            case GearType::Hands: return "hands";
            case GearType::Feet: return "feet";
            case GearType::Cloak: return "cloak";
            case GearType::Misc: return "misc";
            case GearType::Ignore: return "ignored";
            default: return "none";
        }
    }

    GearType GearTypeOf(const ArmorPiece& piece) {
        const auto m = piece.slotMask;
        if (m & kBodySlot) return GearType::Body;
        if (m & (kHairSlot | kHeadSlot)) return GearType::Head;
        if (m & kHandsSlot) return GearType::Hands;
        if (m & kFeetSlot) return GearType::Feet;
        if ((m & kCloakSlot) || piece.Has(ArmorKeyword::ClothingCloak)) return GearType::Cloak;
        if (m & kShieldSlot) return GearType::Misc;
        return GearType::NotFound;
    }

    Protection Score(const ArmorPiece& piece) {
        const auto type = GearTypeOf(piece);
        Protection out;
        if (ScoreByKeyword(piece, type, out)) return out;

        // GetArmorProtectionDataByAnalysis, with the Survival Mode keywords for Frostfall's vanilla table
        const auto m = piece.slotMask;
        if (m == kCircletSlot) return Ignored(GearType::Ignore, "circlet");
        if (m & (kRingSlot | kAmuletSlot)) return Ignored(GearType::Ignore, "jewelry");
        if (type == GearType::NotFound) return Ignored(GearType::Ignore, "no protecting slot");

        const Tiers* tiers = TiersOf(type);
        int tier = kFair;
        out = {0, 0, type, "defaults"};
        if (piece.Has(ArmorKeyword::SurvivalWarm)) tier = kGood, out.source = "Survival_ArmorWarm";
        else if (piece.Has(ArmorKeyword::SurvivalCold)) tier = kPoor, out.source = "Survival_ArmorCold";

        if (type == GearType::Head && piece.hoodName && tier == kFair) {
            out.warmth = kDefaultHoodWarmth;
            out.coverage = kDefaultHoodCoverage;
        } else if (type == GearType::Cloak && tier == kFair) {
            out.warmth = kDefaultCloakWarmth;
            out.coverage = kDefaultCloakCoverage;
        } else if (type == GearType::Misc && (m & kShieldSlot) && tier == kFair) {
            out.warmth = kDefaultShieldWarmth;
            out.coverage = kDefaultShieldCoverage;
        } else {
            out.warmth = tiers->warmth[tier];
            out.coverage = tiers->coverage[tier];
        }

        // Parts the item also covers, at their defaults
        if (type != GearType::Head && (m & (kHairSlot | kHeadSlot))) {
            out.warmth += piece.hoodName ? kDefaultHoodWarmth : kHead.warmth[kFair];
            out.coverage += piece.hoodName ? kDefaultHoodCoverage : kHead.coverage[kFair];
        }
        if (type != GearType::Hands && (m & kHandsSlot)) {
            out.warmth += kHands.warmth[kFair];
            out.coverage += kHands.coverage[kFair];
        }
        if (type != GearType::Feet && (m & kFeetSlot)) {
            out.warmth += kFeet.warmth[kFair];
            out.coverage += kFeet.coverage[kFair];
        }
        if (type != GearType::Cloak && ((m & kCloakSlot) || piece.Has(ArmorKeyword::ClothingCloak))) {
            out.warmth += kDefaultCloakWarmth;
            out.coverage += kDefaultCloakCoverage;
        }
        return out;
    }

    Protection ScoreAll(std::span<const ArmorPiece> pieces) {
        Protection total{0, 0, GearType::NotFound, {}};
        for (const auto& piece : pieces) {
            const auto p = Score(piece);
            total.warmth += p.warmth;
            total.coverage += p.coverage;
        }
        return total;
    }

} // namespace ConditionEngine::Gear
