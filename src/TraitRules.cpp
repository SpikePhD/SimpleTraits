#include "TraitRules.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ST::TraitRules {
    namespace {
        bool Positive(float value) noexcept
        {
            return std::isfinite(value) && value > 0.0f;
        }
    }

    std::string_view TraitKey(Trait trait) noexcept
    {
        switch (trait) {
            case Trait::kStrength: return "strength";
            case Trait::kResilience: return "resilience";
            case Trait::kAgility: return "agility";
            case Trait::kIntelligence: return "intelligence";
            case Trait::kWisdom: return "wisdom";
            case Trait::kCharisma: return "charisma";
        }
        return "unknown";
    }

    std::int64_t EarnedPoints(std::int64_t level, const PointRules& rules) noexcept
    {
        const std::int64_t starting = std::max(rules.startingPoints, 0);
        if (level <= 0 || rules.levelsPerPoint < 1) {
            return starting;
        }
        // Clamping keeps the sum far from int64 overflow for any input.
        const std::int64_t clampedLevel = std::min<std::int64_t>(level, std::numeric_limits<std::uint32_t>::max());
        return starting + clampedLevel / rules.levelsPerPoint;
    }

    std::int64_t SpentPoints(const Allocation& allocation) noexcept
    {
        std::int64_t total = 0;
        for (const auto points : allocation) {
            total += points;
        }
        return total;
    }

    std::int64_t UnspentPoints(
        std::int64_t level, const PointRules& rules, const Allocation& allocation) noexcept
    {
        return std::max<std::int64_t>(0, EarnedPoints(level, rules) - SpentPoints(allocation));
    }

    std::string_view AllocationErrorName(AllocationError error) noexcept
    {
        switch (error) {
            case AllocationError::kNone: return "none";
            case AllocationError::kDecrease: return "decrease";
            case AllocationError::kExceedsTraitCap: return "exceeds-trait-cap";
            case AllocationError::kExceedsUnspent: return "exceeds-unspent";
        }
        return "unknown";
    }

    AllocationError ValidateAllocation(
        const Allocation& current, const Allocation& proposed, std::int64_t earned) noexcept
    {
        for (std::size_t i = 0; i < kTraitCount; ++i) {
            if (proposed[i] < current[i]) {
                return AllocationError::kDecrease;
            }
        }
        for (std::size_t i = 0; i < kTraitCount; ++i) {
            if (proposed[i] > kMaxPointsPerTrait && proposed[i] != current[i]) {
                return AllocationError::kExceedsTraitCap;
            }
        }
        const auto added = SpentPoints(proposed) - SpentPoints(current);
        const auto unspent = std::max<std::int64_t>(0, earned - SpentPoints(current));
        if (added > unspent) {
            return AllocationError::kExceedsUnspent;
        }
        return AllocationError::kNone;
    }

    float PercentBonus(float base, std::uint32_t points, float percentPerPoint) noexcept
    {
        if (!Positive(percentPerPoint) || !Positive(base)) {
            return 0.0f;
        }
        return static_cast<float>(static_cast<double>(base) * percentPerPoint * static_cast<double>(points));
    }

    std::optional<float> ReconcileDelta(float target, float applied) noexcept
    {
        if (!std::isfinite(target) || !std::isfinite(applied)) {
            return std::nullopt;
        }
        return target - applied;
    }

    std::string_view BonusName(Bonus bonus) noexcept
    {
        switch (bonus) {
            case Bonus::kStamina: return "Stamina";
            case Bonus::kHealth: return "Health";
            case Bonus::kMagicka: return "Magicka";
            case Bonus::kCriticalChance: return "CriticalChance";
        }
        return "unknown";
    }

    Trait BonusTrait(Bonus bonus) noexcept
    {
        switch (bonus) {
            case Bonus::kStamina: return Trait::kStrength;
            case Bonus::kHealth: return Trait::kResilience;
            case Bonus::kMagicka: return Trait::kWisdom;
            case Bonus::kCriticalChance: return Trait::kAgility;
        }
        return Trait::kStrength;
    }

    std::uint32_t BonusActorValueId(Bonus bonus) noexcept
    {
        switch (bonus) {
            case Bonus::kStamina: return 26;
            case Bonus::kHealth: return 24;
            case Bonus::kMagicka: return 25;
            case Bonus::kCriticalChance: return 33;
        }
        return 0;
    }

    std::optional<Bonus> BonusFromActorValueId(std::uint32_t id) noexcept
    {
        for (std::size_t i = 0; i < kBonusCount; ++i) {
            const auto bonus = static_cast<Bonus>(i);
            if (BonusActorValueId(bonus) == id) {
                return bonus;
            }
        }
        return std::nullopt;
    }

    float BonusPercent(Bonus bonus, const TraitSettings& settings) noexcept
    {
        switch (bonus) {
            case Bonus::kStamina: return settings.staminaPercent;
            case Bonus::kHealth: return settings.healthPercent;
            case Bonus::kMagicka: return settings.magickaPercent;
            case Bonus::kCriticalChance: break;  // flat, not a percentage of base
        }
        return 0.0f;
    }

    float CriticalChancePerPercent(float weaponConditionCriticalChanceMult) noexcept
    {
        if (!Positive(weaponConditionCriticalChanceMult)) {
            return kVanillaCriticalChancePerPercent;
        }
        return 1.0f / weaponConditionCriticalChanceMult;
    }

    std::array<BonusPlan, kBonusCount> PlanReconcile(const Allocation& allocation,
        const TraitSettings& settings, const BaseValues& bases, const AppliedBonuses& applied,
        float criticalChancePerPercent) noexcept
    {
        std::array<BonusPlan, kBonusCount> plans{};
        for (std::size_t i = 0; i < kBonusCount; ++i) {
            const auto bonus = static_cast<Bonus>(i);
            const auto points = allocation[static_cast<std::size_t>(BonusTrait(bonus))];
            float target = 0.0f;
            if (bonus == Bonus::kCriticalChance) {
                const float perPoint = Positive(settings.criticalChancePerPoint) && Positive(criticalChancePerPercent)
                    ? settings.criticalChancePerPoint * criticalChancePerPercent
                    : 0.0f;
                target = static_cast<float>(static_cast<double>(points) * perPoint);
            } else {
                if (!std::isfinite(bases[i])) {
                    continue;  // unusable base: leave the applied bonus untouched
                }
                target = PercentBonus(bases[i], points, BonusPercent(bonus, settings));
            }
            if (const auto delta = ReconcileDelta(target, applied[i])) {
                plans[i] = { target, *delta, true };
            }
        }
        return plans;
    }

    std::int32_t SkillPointsOwed(
        std::uint32_t points, float perPoint, std::int64_t level, std::uint32_t granted) noexcept
    {
        if (points == 0 || !Positive(perPoint) || level <= 1) {
            return 0;
        }
        const auto levelUps = std::min<std::int64_t>(level - 1, std::numeric_limits<std::uint32_t>::max());
        const double earned = std::floor(static_cast<double>(points) * perPoint * static_cast<double>(levelUps));
        const double owed = earned - static_cast<double>(granted);
        if (!(owed > 0.0)) {
            return 0;
        }
        return static_cast<std::int32_t>(std::min(owed, static_cast<double>(kMaxSkillPointBonus)));
    }

    double BarterPriceFactor(const BarterSettings& settings, float speechSkill) noexcept
    {
        const double skill = std::isfinite(speechSkill) ? std::clamp(static_cast<double>(speechSkill), 0.0, 100.0) : 0.0;
        const double max = settings.barterMax;
        const double min = settings.barterMin;
        return max - (max - min) * skill / 100.0;
    }

    BarterSettings ApplyCharisma(
        const BarterSettings& original, std::uint32_t points, float improvementPerPoint) noexcept
    {
        if (points == 0 || !Positive(improvementPerPoint) ||
            !Positive(original.barterMin) || !Positive(original.barterMax)) {
            return original;
        }
        const double scale = std::max(0.0, 1.0 - static_cast<double>(points) * improvementPerPoint);
        const auto improve = [scale](float value) {
            const double floor = std::min(static_cast<double>(value), static_cast<double>(kMinimumBarterFactor));
            return static_cast<float>(std::max(value * scale, floor));
        };
        return { improve(original.barterMin), improve(original.barterMax) };
    }
}
