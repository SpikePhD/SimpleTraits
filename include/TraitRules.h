#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// Dependency-free trait rules. No CommonLib types: everything here is covered
// by the portable tests and must stay buildable on any development OS.
namespace ST::TraitRules {

    enum class Trait : std::uint8_t {
        kStrength,      // +Stamina
        kResilience,    // +Health
        kAgility,       // +critical hit chance
        kIntelligence,  // XP multiplier for every SAL XP award
        kWisdom,        // +Magicka
        kCharisma       // fBarterMin/fBarterMax
    };
    inline constexpr std::size_t kTraitCount = 6;

    [[nodiscard]] std::string_view TraitKey(Trait trait) noexcept;

    // Points allocated per trait, indexed by Trait.
    using Allocation = std::array<std::uint32_t, kTraitCount>;

    // Rejects corrupt or absurd per-trait totals; far above anything reachable.
    inline constexpr std::uint32_t kMaxPointsPerTrait = 10000;

    struct PointRules {
        int startingPoints{ 4 };
        int levelsPerPoint{ 3 };
    };

    // Validated values from SimpleTraits.json. Defaults match the shipped file.
    struct TraitSettings {
        PointRules points{};
        // Fraction of the base actor value added per point (0.05 = 5%).
        float      staminaPercent{ 0.05f };
        float      healthPercent{ 0.05f };
        float      magickaPercent{ 0.05f };
        // Critical hit chance per point, in percent (1 = +1%). Not the raw
        // CriticalChance actor value: vanilla needs 10 of it per 1% (see AGENTS.md).
        float      criticalChancePerPoint{ 1.0f };
        // Extra XP per Intelligence point, as a fraction (0.10: ten points
        // double every XP award).
        float      intelligenceXPPercent{ 0.10f };
        // Fraction by which the barter price factor drops per Charisma point.
        float      charismaPriceImprovement{ 0.01f };
    };

    // -------------------------------------------------------------------
    // Points. Unspent points are derived, never stored:
    //   earned(level) = startingPoints + floor(level / levelsPerPoint)
    //   unspent       = max(0, earned - sum(allocations))
    // Negative levels or starting points count as 0, levels above UINT32_MAX
    // count as UINT32_MAX, and levelsPerPoint < 1 grants no level points.
    // Allocations are never reduced here, even when settings lower earned
    // below spent.
    // -------------------------------------------------------------------
    [[nodiscard]] std::int64_t EarnedPoints(std::int64_t level, const PointRules& rules) noexcept;
    [[nodiscard]] std::int64_t SpentPoints(const Allocation& allocation) noexcept;
    [[nodiscard]] std::int64_t UnspentPoints(
        std::int64_t level, const PointRules& rules, const Allocation& allocation) noexcept;

    enum class AllocationError {
        kNone,
        kDecrease,         // v1 has no respec: no trait may go down
        kExceedsTraitCap,  // a trait above kMaxPointsPerTrait
        kExceedsUnspent    // the added points exceed the unspent points
    };

    [[nodiscard]] std::string_view AllocationErrorName(AllocationError error) noexcept;

    // Validates committing `proposed` over `current` when `earned` points are
    // available. Unchanged allocations are always valid, even if overspent.
    [[nodiscard]] AllocationError ValidateAllocation(
        const Allocation& current, const Allocation& proposed, std::int64_t earned) noexcept;

    // -------------------------------------------------------------------
    // Bonuses
    // -------------------------------------------------------------------

    // base * percentPerPoint * points. Nothing for a non-finite or negative
    // base or percentage, so a bad reading never produces a bonus.
    [[nodiscard]] float PercentBonus(float base, std::uint32_t points, float percentPerPoint) noexcept;

    // Change needed on the permanent modifier layer to move the plugin's own
    // applied bonus to `target`. Applying it and recording `target` as the new
    // applied amount makes the next call return 0 (idempotent reconcile).
    // nullopt when either value is non-finite.
    [[nodiscard]] std::optional<float> ReconcileDelta(float target, float applied) noexcept;

    // -------------------------------------------------------------------
    // Actor-value bonuses applied on the permanent modifier layer.
    // Strength -> Stamina, Resilience -> Health, Wisdom -> Magicka, each a
    // percentage of the actor value's BASE (never of other modifiers, so
    // the plugin's own bonus cannot compound).
    // Agility -> CriticalChance, a flat amount: see CriticalChancePerPercent.
    // -------------------------------------------------------------------
    enum class Bonus : std::uint8_t { kStamina, kHealth, kMagicka, kCriticalChance };
    inline constexpr std::size_t kBonusCount = 4;

    // Amount of each bonus ST has applied, indexed by Bonus. Persisted in the
    // cosave so reconciling never double-applies.
    using AppliedBonuses = std::array<float, kBonusCount>;

    [[nodiscard]] std::string_view BonusName(Bonus bonus) noexcept;
    [[nodiscard]] Trait            BonusTrait(Bonus bonus) noexcept;

    // RE::ActorValue ids (kHealth 24, kMagicka 25, kStamina 26,
    // kCriticalChance 33). Kept numeric so the cosave codec stays
    // dependency-free; the plugin static_asserts them.
    [[nodiscard]] std::uint32_t        BonusActorValueId(Bonus bonus) noexcept;
    [[nodiscard]] std::optional<Bonus> BonusFromActorValueId(std::uint32_t id) noexcept;

    // Base actor values, indexed by Bonus (the CriticalChance entry is unused).
    using BaseValues = std::array<float, kBonusCount>;

    // CriticalChance actor value per 1% crit chance. The engine computes
    //   crit% = fWeaponConditionCriticalChanceMult * weapon condition
    //           * CriticalChance * weapon crit mult   (then crit perks)
    // (confirmed in game; see AGENTS.md), so on an untempered weapon with
    // crit mult 1.0, 1% needs 1 / fWeaponConditionCriticalChanceMult: 10 in
    // vanilla (0.1). A non-finite or non-positive setting falls back to 10.
    inline constexpr float kVanillaCriticalChancePerPercent = 10.0f;
    [[nodiscard]] float CriticalChancePerPercent(float weaponConditionCriticalChanceMult) noexcept;

    struct BonusPlan {
        float target{ 0.0f };  // bonus the allocation should give
        float delta{ 0.0f };   // change to apply: target - applied
        bool  valid{ false };  // false when applied or base is unusable; do not touch
    };

    [[nodiscard]] float BonusPercent(Bonus bonus, const TraitSettings& settings) noexcept;

    // Target and delta per bonus for the given allocation, settings and base
    // values. A base changing (level-up attribute choice) moves the target,
    // so the bonus follows it retroactively. Agility's target is
    // points * critical_chance (%) * criticalChancePerPercent.
    [[nodiscard]] std::array<BonusPlan, kBonusCount> PlanReconcile(const Allocation& allocation,
        const TraitSettings& settings, const BaseValues& bases, const AppliedBonuses& applied,
        float criticalChancePerPercent = kVanillaCriticalChancePerPercent) noexcept;

    // Intelligence: multiplier SAL applies to each XP award as it is earned,
    //   1 + points * percentPerPoint   (0.10 per point: 10 points = 2x).
    // Linear and uncapped. 1.0 for a non-finite or negative percentage, so a
    // bad setting never reduces XP. Nothing is granted or stored: the value
    // only affects XP earned while it is in force.
    [[nodiscard]] float XPMultiplier(std::uint32_t points, float percentPerPoint) noexcept;

    // -------------------------------------------------------------------
    // Charisma: barter game settings.
    //
    // Source: UESP, Skyrim:Speech, "Prices" (https://en.uesp.net/wiki/Skyrim:Speech):
    //   price factor = fBarterMax - (fBarterMax - fBarterMin) * min(skill, 100) / 100
    //   buy price    = round(value * buy price modifier  * price factor)
    //   sell price   = round(value * sell price modifier / price factor)
    // Defaults: fBarterMax = 3.3, fBarterMin = 2.0. The modifiers come from
    // perks (Haggling, Allure) and Fortify Barter.
    //
    // The factor is linear in (fBarterMin, fBarterMax), so scaling both by
    // m = 1 - points * improvementPerPoint scales the factor by exactly m at
    // every Speech level: buy prices are multiplied by m and sell prices by
    // 1/m, so both improve. Each value is floored at kMinimumBarterFactor so
    // the factor stays >= 1 and an item never sells for more than it costs to
    // buy; a value already below the floor is left unchanged, never worsened.
    // Always pass the captured original values: the result is not meant to be
    // fed back in.
    // -------------------------------------------------------------------
    struct BarterSettings {
        float barterMin{ 2.0f };  // fBarterMin: factor at Speech 100
        float barterMax{ 3.3f };  // fBarterMax: factor at Speech 0
    };
    inline constexpr float kMinimumBarterFactor = 1.0f;

    [[nodiscard]] double BarterPriceFactor(const BarterSettings& settings, float speechSkill) noexcept;

    // Returns `original` unchanged when either value is non-finite or <= 0,
    // or when improvementPerPoint is non-finite or <= 0.
    [[nodiscard]] BarterSettings ApplyCharisma(
        const BarterSettings& original, std::uint32_t points, float improvementPerPoint) noexcept;
}
