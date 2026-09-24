#include "TraitMenuRules.h"

#include <algorithm>
#include <cmath>

namespace ST::TraitMenuRules {

    bool Session::BeginOpening() noexcept
    {
        if (_state != SessionState::kIdle) {
            return false;
        }
        _state = SessionState::kOpening;
        return true;
    }

    bool Session::Begin(std::int64_t unspent, const TraitRules::Allocation& committed) noexcept
    {
        if (_state == SessionState::kIdle) {
            _state = SessionState::kOpening;
        }
        if (_state != SessionState::kOpening) {
            return false;
        }
        if (unspent < 0) {
            Cancel();
            return false;
        }
        _total = unspent;
        _remaining = unspent;
        _committed = committed;
        _deltas.fill(0);
        _state = SessionState::kActive;
        return true;
    }

    PreviewResult Session::Allocate(std::size_t trait) noexcept
    {
        if (_state != SessionState::kActive) {
            return PreviewResult::kInvalidState;
        }
        if (trait >= TraitRules::kTraitCount) {
            return PreviewResult::kInvalidTrait;
        }
        if (_remaining <= 0) {
            return PreviewResult::kNoPoints;
        }
        if (Preview(trait) >= TraitRules::kMaxPointsPerTrait) {
            return PreviewResult::kAtCap;
        }
        ++_deltas[trait];
        --_remaining;
        return PreviewResult::kChanged;
    }

    PreviewResult Session::Deallocate(std::size_t trait) noexcept
    {
        if (_state != SessionState::kActive) {
            return PreviewResult::kInvalidState;
        }
        if (trait >= TraitRules::kTraitCount) {
            return PreviewResult::kInvalidTrait;
        }
        if (_deltas[trait] == 0) {
            return PreviewResult::kNothingToRemove;
        }
        --_deltas[trait];
        ++_remaining;
        return PreviewResult::kChanged;
    }

    bool Session::Reset() noexcept
    {
        if (_state != SessionState::kActive) {
            return false;
        }
        _deltas.fill(0);
        _remaining = _total;
        return true;
    }

    CommitPlan Session::PrepareCommit(const TraitRules::Allocation& currentCommitted) noexcept
    {
        CommitPlan plan;
        plan.remaining = _remaining;
        if (_state != SessionState::kActive) {
            return plan;
        }
        _state = SessionState::kCommitting;
        if (currentCommitted != _committed) {
            plan.status = CommitStatus::kSnapshotDrift;
            return plan;
        }
        for (std::size_t i = 0; i < TraitRules::kTraitCount; ++i) {
            plan.allocation[i] = Preview(i);
        }
        plan.status = CommitStatus::kReady;
        return plan;
    }

    void Session::MarkClosing() noexcept
    {
        if (_state != SessionState::kIdle) {
            _state = SessionState::kClosing;
        }
    }

    void Session::Cancel() noexcept
    {
        _state = SessionState::kIdle;
        _total = 0;
        _remaining = 0;
        _committed.fill(0);
        _deltas.fill(0);
    }

    bool Session::HasChanges() const noexcept
    {
        return std::ranges::any_of(_deltas, [](auto delta) { return delta != 0; });
    }

    std::optional<std::size_t> ParseTraitIndex(double value) noexcept
    {
        if (!std::isfinite(value) || std::trunc(value) != value || value < 0.0 ||
            value >= static_cast<double>(TraitRules::kTraitCount)) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(value);
    }
}
