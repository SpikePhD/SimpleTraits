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

    float TraitBonus(std::uint32_t points, float perPoint) noexcept
    {
        if (!std::isfinite(perPoint) || perPoint <= 0.0f) {
            return 0.0f;
        }
        return static_cast<float>(static_cast<double>(points) * perPoint);
    }

    std::optional<float> ReconcileDelta(float target, float applied) noexcept
    {
        if (!std::isfinite(target) || !std::isfinite(applied)) {
            return std::nullopt;
        }
        return target - applied;
    }

    float IntelligenceThresholdMultiplier(std::uint32_t points, float reductionPerPoint) noexcept
    {
        if (points == 0 || !Positive(reductionPerPoint)) {
            return 1.0f;
        }
        const double multiplier = 1.0 - static_cast<double>(points) * reductionPerPoint;
        return static_cast<float>(std::clamp(multiplier, static_cast<double>(kMinimumThresholdMultiplier), 1.0));
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
