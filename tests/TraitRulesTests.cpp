#include "TraitRules.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
    int failures = 0;

    void Check(bool condition, std::string_view message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    bool Near(double left, double right, double tolerance = 1e-5)
    {
        return std::fabs(left - right) <= tolerance;
    }

    using namespace ST::TraitRules;

    void TestPoints()
    {
        const PointRules rules{ 4, 3 };
        Check(EarnedPoints(0, rules) == 4, "level 0 earns starting points only");
        Check(EarnedPoints(1, rules) == 4, "level 1 earns starting points only");
        Check(EarnedPoints(2, rules) == 4, "level 2 still below first level point");
        Check(EarnedPoints(3, rules) == 5, "level 3 earns first level point");
        Check(EarnedPoints(5, rules) == 5, "floor division");
        Check(EarnedPoints(6, rules) == 6, "level 6 earns second level point");
        Check(EarnedPoints(81, rules) == 31, "level 81");
        Check(EarnedPoints(-5, rules) == 4, "negative level counts as zero");
        Check(EarnedPoints(10, { -3, 3 }) == 3, "negative starting points count as zero");
        Check(EarnedPoints(10, { 2, 0 }) == 2, "levels_per_point below 1 grants no level points");
        Check(EarnedPoints(10, { 0, 1 }) == 10, "one point per level");
        Check(EarnedPoints(std::numeric_limits<std::int64_t>::max(), { 100, 1 }) > 0,
            "huge level does not overflow into negative");

        Allocation allocation{ 1, 2, 0, 0, 0, 1 };
        Check(SpentPoints(allocation) == 4, "spent is the sum of allocations");
        Check(UnspentPoints(6, rules, allocation) == 2, "unspent = earned - spent");
        Check(UnspentPoints(0, { 0, 3 }, allocation) == 0, "overspent clamps unspent at zero");
        const Allocation huge{ UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
        Check(SpentPoints(huge) == 6LL * UINT32_MAX, "spent sum does not overflow");
        Check(UnspentPoints(81, rules, huge) == 0, "corrupt allocation yields zero unspent");
    }

    void TestAllocation()
    {
        const Allocation current{ 1, 1, 0, 0, 0, 0 };
        Allocation proposed = current;
        Check(ValidateAllocation(current, proposed, 2) == AllocationError::kNone, "unchanged allocation is valid");
        Check(ValidateAllocation(current, proposed, 0) == AllocationError::kNone,
            "unchanged allocation is valid even when overspent");

        proposed = { 1, 1, 2, 0, 0, 1 };
        Check(ValidateAllocation(current, proposed, 5) == AllocationError::kNone, "spending exactly the unspent points");
        Check(ValidateAllocation(current, proposed, 4) == AllocationError::kExceedsUnspent, "spending one too many");
        Check(ValidateAllocation(current, { 1, 1, 1, 0, 0, 0 }, 1) == AllocationError::kExceedsUnspent,
            "overspent character cannot add points");

        Check(ValidateAllocation(current, { 0, 1, 1, 0, 0, 0 }, 10) == AllocationError::kDecrease,
            "no respec: moving a point is rejected");
        Check(ValidateAllocation(current, { 0, 0, 0, 0, 0, 0 }, 10) == AllocationError::kDecrease,
            "no respec: removing points is rejected");

        Allocation capped = current;
        capped[0] = kMaxPointsPerTrait + 1;
        Check(ValidateAllocation(current, capped, 1000000) == AllocationError::kExceedsTraitCap,
            "per-trait sanity cap");
        Allocation atCap = current;
        atCap[0] = kMaxPointsPerTrait;
        Check(ValidateAllocation(current, atCap, 1000000) == AllocationError::kNone, "per-trait cap is inclusive");
    }

    void TestBonuses()
    {
        Check(TraitBonus(0, 5.0f) == 0.0f, "zero points");
        Check(TraitBonus(3, 5.0f) == 15.0f, "stamina/health/magicka per point");
        Check(Near(TraitBonus(7, 1.0f), 7.0), "critical chance percentage points");
        Check(Near(TraitBonus(3, 0.5f), 1.5), "fractional per-point value");
        Check(TraitBonus(3, -1.0f) == 0.0f, "negative per-point grants nothing");
        Check(TraitBonus(3, std::numeric_limits<float>::quiet_NaN()) == 0.0f, "NaN per-point grants nothing");
        Check(TraitBonus(3, std::numeric_limits<float>::infinity()) == 0.0f, "infinite per-point grants nothing");

        Check(ReconcileDelta(15.0f, 0.0f) == 15.0f, "first apply adds the full bonus");
        Check(ReconcileDelta(15.0f, 15.0f) == 0.0f, "reconcile is idempotent");
        Check(ReconcileDelta(10.0f, 15.0f) == -5.0f, "lower target removes only the plugin's excess");
        Check(ReconcileDelta(0.0f, 15.0f) == -15.0f, "zero target removes exactly what was applied");
        Check(!ReconcileDelta(std::numeric_limits<float>::quiet_NaN(), 0.0f), "NaN target rejected");
        Check(!ReconcileDelta(5.0f, std::numeric_limits<float>::infinity()), "infinite applied rejected");
    }

    void TestIntelligence()
    {
        Check(IntelligenceThresholdMultiplier(0, 0.02f) == 1.0f, "no points means no change");
        Check(Near(IntelligenceThresholdMultiplier(1, 0.02f), 0.98), "one point");
        Check(Near(IntelligenceThresholdMultiplier(10, 0.02f), 0.80), "ten points");
        Check(Near(IntelligenceThresholdMultiplier(25, 0.02f), 0.50), "exactly at the floor");
        Check(IntelligenceThresholdMultiplier(26, 0.02f) == kMinimumThresholdMultiplier, "clamped to 0.5");
        Check(IntelligenceThresholdMultiplier(UINT32_MAX, 0.5f) == kMinimumThresholdMultiplier, "huge input clamped");
        Check(IntelligenceThresholdMultiplier(5, 0.0f) == 1.0f, "zero reduction");
        Check(IntelligenceThresholdMultiplier(5, -0.1f) == 1.0f, "negative reduction ignored");
        Check(IntelligenceThresholdMultiplier(5, std::numeric_limits<float>::quiet_NaN()) == 1.0f, "NaN ignored");
    }

    void TestBarter()
    {
        const BarterSettings vanilla{};
        Check(Near(BarterPriceFactor(vanilla, 0.0f), 3.3), "factor at Speech 0 is fBarterMax");
        Check(Near(BarterPriceFactor(vanilla, 100.0f), 2.0), "factor at Speech 100 is fBarterMin");
        Check(Near(BarterPriceFactor(vanilla, 50.0f), 2.65), "factor at Speech 50");
        Check(Near(BarterPriceFactor(vanilla, 150.0f), 2.0), "Speech above 100 has no effect");

        const auto none = ApplyCharisma(vanilla, 0, 0.01f);
        Check(none.barterMin == vanilla.barterMin && none.barterMax == vanilla.barterMax, "zero points unchanged");

        const auto ten = ApplyCharisma(vanilla, 10, 0.01f);
        Check(Near(ten.barterMin, 1.8) && Near(ten.barterMax, 2.97), "ten points scale both by 0.9");
        for (const float skill : { 0.0f, 15.0f, 50.0f, 100.0f }) {
            const double before = BarterPriceFactor(vanilla, skill);
            const double after = BarterPriceFactor(ten, skill);
            Check(Near(after, before * 0.9, 1e-4), "factor scales by m at every Speech level");
            const double value = 100.0;
            Check(value * after < value * before, "buying is cheaper");
            Check(value / after > value / before, "selling pays more");
        }

        // Floor: at 60 points m = 0.4 -> min 0.8 floored to 1.0, max 1.32.
        const auto deep = ApplyCharisma(vanilla, 60, 0.01f);
        Check(deep.barterMin == kMinimumBarterFactor && Near(deep.barterMax, 1.32), "min floored at 1.0");
        const auto extreme = ApplyCharisma(vanilla, 1000, 0.05f);
        Check(extreme.barterMin == kMinimumBarterFactor && extreme.barterMax == kMinimumBarterFactor,
            "both floored when scale reaches zero");
        for (const float skill : { 0.0f, 100.0f }) {
            Check(BarterPriceFactor(extreme, skill) >= 1.0, "sell never exceeds buy");
        }
        Check(deep.barterMin <= deep.barterMax, "ordering preserved");

        const BarterSettings modded{ 0.9f, 1.5f };
        const auto moddedResult = ApplyCharisma(modded, 30, 0.01f);
        Check(moddedResult.barterMin == 0.9f, "value already below the floor is never worsened");
        Check(Near(moddedResult.barterMax, 1.05), "other value still improves");

        const BarterSettings invalid{ std::numeric_limits<float>::quiet_NaN(), 3.3f };
        const auto invalidResult = ApplyCharisma(invalid, 10, 0.01f);
        Check(std::isnan(invalidResult.barterMin) && invalidResult.barterMax == 3.3f, "invalid originals unchanged");
        const auto zeroResult = ApplyCharisma({ 0.0f, 3.3f }, 10, 0.01f);
        Check(zeroResult.barterMin == 0.0f && zeroResult.barterMax == 3.3f, "non-positive originals unchanged");
        const auto badPerPoint = ApplyCharisma(vanilla, 10, std::numeric_limits<float>::quiet_NaN());
        Check(badPerPoint.barterMin == vanilla.barterMin && badPerPoint.barterMax == vanilla.barterMax,
            "NaN per-point unchanged");

        // Pure function of the originals: re-applying from originals is stable.
        const auto again = ApplyCharisma(vanilla, 10, 0.01f);
        Check(again.barterMin == ten.barterMin && again.barterMax == ten.barterMax, "deterministic from originals");
    }

    void TestReconcilePlan()
    {
        TraitSettings settings{};  // 5 stamina/health/magicka per point
        // Strength 2, Resilience 1, Agility 3, Intelligence 4, Wisdom 0, Charisma 5.
        const Allocation allocation{ 2, 1, 3, 4, 0, 5 };
        const AppliedBonuses none{};

        const auto first = PlanReconcile(allocation, settings, none);
        const auto stamina = static_cast<std::size_t>(Bonus::kStamina);
        const auto health = static_cast<std::size_t>(Bonus::kHealth);
        const auto magicka = static_cast<std::size_t>(Bonus::kMagicka);
        Check(first[stamina].valid && first[stamina].target == 10.0f && first[stamina].delta == 10.0f,
            "Strength 2 -> +10 Stamina");
        Check(first[health].target == 5.0f && first[health].delta == 5.0f, "Resilience 1 -> +5 Health");
        Check(first[magicka].target == 0.0f && first[magicka].delta == 0.0f, "Wisdom 0 -> no Magicka");

        // Applying the plan and recording the targets makes the next plan a no-op.
        const AppliedBonuses applied{ first[0].target, first[1].target, first[2].target };
        const auto second = PlanReconcile(allocation, settings, applied);
        for (const auto& plan : second) {
            Check(plan.valid && plan.delta == 0.0f, "reconcile is idempotent");
        }

        // Lowering a per-point setting removes only ST's own excess.
        settings.staminaPerPoint = 3.0f;
        const auto lowered = PlanReconcile(allocation, settings, applied);
        Check(lowered[stamina].target == 6.0f && lowered[stamina].delta == -4.0f, "lower setting shrinks the bonus");
        Check(lowered[health].delta == 0.0f, "other bonuses unaffected");

        // A setting of 0 removes exactly what was applied.
        settings.healthPerPoint = 0.0f;
        const auto zeroed = PlanReconcile(allocation, settings, applied);
        Check(zeroed[health].target == 0.0f && zeroed[health].delta == -5.0f, "zero setting removes the bonus");

        // A non-finite recorded amount is never touched.
        AppliedBonuses corrupt = applied;
        corrupt[magicka] = std::numeric_limits<float>::quiet_NaN();
        const auto guarded = PlanReconcile(allocation, TraitSettings{}, corrupt);
        Check(!guarded[magicka].valid, "non-finite applied amount is not reconciled");
        Check(guarded[stamina].valid, "other bonuses still reconciled");

        Check(BonusTrait(Bonus::kStamina) == Trait::kStrength, "Stamina comes from Strength");
        Check(BonusTrait(Bonus::kHealth) == Trait::kResilience, "Health comes from Resilience");
        Check(BonusTrait(Bonus::kMagicka) == Trait::kWisdom, "Magicka comes from Wisdom");
        Check(BonusActorValueId(Bonus::kHealth) == 24 && BonusActorValueId(Bonus::kMagicka) == 25 &&
                  BonusActorValueId(Bonus::kStamina) == 26,
            "actor value ids match RE::ActorValue");
        Check(BonusFromActorValueId(25) == Bonus::kMagicka && !BonusFromActorValueId(33), "id lookup and whitelist");
    }

    void TestKeys()
    {
        Check(TraitKey(Trait::kStrength) == "strength", "strength key");
        Check(TraitKey(Trait::kCharisma) == "charisma", "charisma key");
        Check(static_cast<std::size_t>(Trait::kCharisma) + 1 == kTraitCount, "trait count matches enum");
    }
}

int main()
{
    TestPoints();
    TestAllocation();
    TestBonuses();
    TestIntelligence();
    TestBarter();
    TestReconcilePlan();
    TestKeys();
    if (failures == 0) {
        std::cout << "All trait-rules tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
