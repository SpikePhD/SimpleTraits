#pragma once

#include "TraitRules.h"

#include <cstdint>
#include <optional>

// Dependency-free rules for the trait allocation menu: a transactional
// preview session (mirrors SAL's UIRules::AllocationSession) and the guard
// that owes SAL exactly one ContinueLevelUp per level-up step.
namespace ST::TraitMenuRules {

    enum class SessionState { kIdle, kOpening, kActive, kCommitting, kClosing };

    enum class PreviewResult { kChanged, kInvalidState, kInvalidTrait, kNoPoints, kAtCap, kNothingToRemove };

    enum class CommitStatus { kReady, kInvalidState, kSnapshotDrift };

    struct CommitPlan {
        CommitStatus           status{ CommitStatus::kInvalidState };
        TraitRules::Allocation allocation{};  // committed + previewed points
        std::int64_t           remaining{ 0 };  // unspent points kept for later

        [[nodiscard]] bool Ready() const noexcept { return status == CommitStatus::kReady; }
    };

    // Previews spending `unspent` points on top of the committed allocation.
    // Deallocate only returns points added in this session, so committed
    // points can never go down (no respec).
    class Session {
    public:
        [[nodiscard]] bool BeginOpening() noexcept;
        // From kIdle or kOpening; false (and cancelled) for negative unspent.
        [[nodiscard]] bool Begin(std::int64_t unspent, const TraitRules::Allocation& committed) noexcept;
        [[nodiscard]] PreviewResult Allocate(std::size_t trait) noexcept;
        [[nodiscard]] PreviewResult Deallocate(std::size_t trait) noexcept;
        [[nodiscard]] bool Reset() noexcept;
        // Moves to kCommitting. kSnapshotDrift when the committed allocation
        // changed while the menu was open (for example via the debug page).
        [[nodiscard]] CommitPlan PrepareCommit(const TraitRules::Allocation& currentCommitted) noexcept;
        void MarkClosing() noexcept;
        void Cancel() noexcept;

        [[nodiscard]] SessionState State() const noexcept { return _state; }
        [[nodiscard]] std::int64_t TotalPoints() const noexcept { return _total; }
        [[nodiscard]] std::int64_t RemainingPoints() const noexcept { return _remaining; }
        [[nodiscard]] std::uint32_t Committed(std::size_t trait) const noexcept { return _committed[trait]; }
        [[nodiscard]] std::uint32_t Delta(std::size_t trait) const noexcept { return _deltas[trait]; }
        [[nodiscard]] std::uint32_t Preview(std::size_t trait) const noexcept { return _committed[trait] + _deltas[trait]; }
        [[nodiscard]] bool HasChanges() const noexcept;

    private:
        SessionState           _state{ SessionState::kIdle };
        std::int64_t           _total{ 0 };
        std::int64_t           _remaining{ 0 };
        TraitRules::Allocation _committed{};
        TraitRules::Allocation _deltas{};
    };

    // Scaleform numbers arrive as doubles; accepts only integral trait
    // indexes 0..kTraitCount-1.
    [[nodiscard]] std::optional<std::size_t> ParseTraitIndex(double value) noexcept;

    // SAL's ContinueLevelUp is shared by its level-up steps, so ST must call
    // it exactly once per wait: Arm() when wantsStep returns true, and every
    // exit path asks TakeContinue(), which is true only the first time.
    class ContinuationGuard {
    public:
        void Arm() noexcept { _owed = true; }
        [[nodiscard]] bool TakeContinue() noexcept
        {
            const bool owed = _owed;
            _owed = false;
            return owed;
        }
        // Lifecycle reset (load, revert, new game): SAL resets its own wait,
        // so nothing is owed any more.
        void Reset() noexcept { _owed = false; }
        [[nodiscard]] bool Owed() const noexcept { return _owed; }

    private:
        bool _owed{ false };
    };
}
