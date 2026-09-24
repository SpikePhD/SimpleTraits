#pragma once

#include "TraitRules.h"

#include <optional>
#include <string_view>

// The player's trait allocations and the bonuses ST has applied, persisted in
// the SKSE cosave (TraitSave). Mutated only on the main thread; Snapshot()
// may be called from any thread (the debug page renders off the main thread).
namespace ST::TraitState {

    struct Snapshot {
        bool                       gameActive{ false };  // a game is loaded or started
        std::int64_t               level{ 0 };
        std::int64_t               earned{ 0 };
        std::int64_t               spent{ 0 };
        std::int64_t               unspent{ 0 };
        TraitRules::Allocation     allocation{};
        TraitRules::AppliedBonuses applied{};
    };

    // Registers the cosave unique ID and save/load/revert callbacks. Call
    // during SKSEPlugin_Load; returns false if serialization is unavailable.
    bool RegisterSerialization();

    // kNewGame: a fresh character with no allocations.
    void OnNewGame();

    // Main thread. Moves each actor-value bonus from what ST has applied to
    // what the allocation and settings call for, on the permanent modifier
    // layer, and records the new applied amounts.
    void Reconcile(std::string_view reason);

    // Main thread. Replaces the allocation with `proposed` when
    // ValidateAllocation allows it (no decreases, never more than unspent),
    // then reconciles. Returns the validation result, or nullopt when no
    // game is loaded. `source` names the caller in the log.
    std::optional<TraitRules::AllocationError> CommitAllocation(
        const TraitRules::Allocation& proposed, std::string_view source);

    // Main thread. CommitAllocation with one more point on `trait`.
    std::optional<TraitRules::AllocationError> SpendPoint(TraitRules::Trait trait);

    [[nodiscard]] Snapshot GetSnapshot();
}
