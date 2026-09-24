#pragma once

// Log-only event sinks. They never change game state.
namespace ST::DiagnosticSinks {
    // Registers every diagnostic sink once. Call on kDataLoaded.
    void Register();

    // Clears session counters. Call on kPostLoadGame and kNewGame.
    void Reset();
}
