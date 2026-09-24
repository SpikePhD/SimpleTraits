#include "TraitMenuRules.h"

#include <cmath>
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

    using namespace ST::TraitMenuRules;
    using ST::TraitRules::Allocation;

    void TestSessionLifecycle()
    {
        Session session;
        Check(session.State() == SessionState::kIdle, "starts idle");
        Check(session.Allocate(0) == PreviewResult::kInvalidState, "no allocation before Begin");
        Check(session.BeginOpening(), "opening from idle");
        Check(!session.BeginOpening(), "only one opening at a time");

        const Allocation committed{ 2, 1, 0, 0, 1, 0 };
        Check(session.Begin(3, committed), "begin with 3 unspent");
        Check(session.State() == SessionState::kActive, "active after Begin");
        Check(!session.Begin(3, committed), "no second Begin while active");
        Check(session.TotalPoints() == 3 && session.RemainingPoints() == 3, "remaining starts at unspent");
        Check(session.Preview(0) == 2 && session.Delta(0) == 0, "preview starts at committed");

        session.MarkClosing();
        Check(session.State() == SessionState::kClosing, "closing");
        session.Cancel();
        Check(session.State() == SessionState::kIdle && session.TotalPoints() == 0, "cancel returns to idle");

        Session negative;
        Check(!negative.Begin(-1, committed) && negative.State() == SessionState::kIdle, "negative unspent rejected");
        Session zero;
        Check(zero.Begin(0, committed), "zero unspent is a valid (view-only) session");
        Check(zero.Allocate(1) == PreviewResult::kNoPoints, "nothing to spend with zero points");
    }

    void TestPreview()
    {
        Session session;
        const Allocation committed{ 2, 1, 0, 0, 1, 0 };
        Check(session.Begin(2, committed), "begin");

        Check(session.Allocate(0) == PreviewResult::kChanged, "add to Strength");
        Check(session.Allocate(0) == PreviewResult::kChanged, "add again");
        Check(session.RemainingPoints() == 0 && session.Preview(0) == 4 && session.Delta(0) == 2, "preview adds on top");
        Check(session.Allocate(1) == PreviewResult::kNoPoints, "no points left");
        Check(session.HasChanges(), "has changes");

        Check(session.Deallocate(1) == PreviewResult::kNothingToRemove, "committed points cannot be removed (no respec)");
        Check(session.Deallocate(0) == PreviewResult::kChanged, "session points can be removed");
        Check(session.Preview(0) == 3 && session.RemainingPoints() == 1, "remove restores a point");
        Check(session.Allocate(6) == PreviewResult::kInvalidTrait && session.Deallocate(99) == PreviewResult::kInvalidTrait,
            "out-of-range trait rejected");

        Check(session.Reset(), "reset");
        Check(!session.HasChanges() && session.RemainingPoints() == 2 && session.Preview(0) == 2, "reset restores everything");

        Allocation nearCap{};
        nearCap[3] = ST::TraitRules::kMaxPointsPerTrait;
        Session capped;
        Check(capped.Begin(5, nearCap), "begin at cap");
        Check(capped.Allocate(3) == PreviewResult::kAtCap, "per-trait cap respected");
    }

    void TestCommit()
    {
        const Allocation committed{ 1, 0, 0, 0, 0, 0 };
        Session session;
        Check(session.Begin(3, committed), "begin");
        Check(session.Allocate(1) == PreviewResult::kChanged && session.Allocate(4) == PreviewResult::kChanged, "spend two");

        const auto plan = session.PrepareCommit(committed);
        Check(plan.Ready(), "commit ready");
        Check(plan.allocation == Allocation{ 1, 1, 0, 0, 1, 0 }, "commit is committed + preview");
        Check(plan.remaining == 1, "unspent point kept for later");
        Check(session.State() == SessionState::kCommitting, "committing");
        Check(!session.PrepareCommit(committed).Ready(), "commit only once");
        Check(session.Allocate(0) == PreviewResult::kInvalidState, "no changes while committing");

        Session drift;
        Check(drift.Begin(2, committed), "begin");
        const auto drifted = drift.PrepareCommit(Allocation{ 2, 0, 0, 0, 0, 0 });
        Check(drifted.status == CommitStatus::kSnapshotDrift, "commit rejected when committed points changed meanwhile");

        Session nothing;
        Check(nothing.Begin(3, committed), "begin");
        const auto unchanged = nothing.PrepareCommit(committed);
        Check(unchanged.Ready() && unchanged.allocation == committed && unchanged.remaining == 3,
            "confirming without spending keeps every point");
    }

    void TestParse()
    {
        Check(ParseTraitIndex(0.0) == 0u && ParseTraitIndex(5.0) == 5u, "valid indexes");
        Check(!ParseTraitIndex(6.0) && !ParseTraitIndex(-1.0), "out of range rejected");
        Check(!ParseTraitIndex(1.5), "fractional rejected");
        Check(!ParseTraitIndex(std::numeric_limits<double>::quiet_NaN()), "NaN rejected");
        Check(!ParseTraitIndex(std::numeric_limits<double>::infinity()), "infinity rejected");
    }

    void TestContinuationGuard()
    {
        ContinuationGuard guard;
        Check(!guard.TakeContinue(), "nothing owed initially");
        guard.Arm();
        Check(guard.Owed(), "owed after arming");
        Check(guard.TakeContinue(), "first exit path continues");
        Check(!guard.TakeContinue(), "second exit path (confirm then close) does not continue again");
        guard.Arm();
        guard.Reset();
        Check(!guard.TakeContinue(), "lifecycle reset clears the debt");
    }
}

int main()
{
    TestSessionLifecycle();
    TestPreview();
    TestCommit();
    TestParse();
    TestContinuationGuard();
    if (failures == 0) {
        std::cout << "All trait-menu rules tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
