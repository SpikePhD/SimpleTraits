#pragma once

#include <cstdint>

// The trait allocation menu (Interface/ST_TraitMenu.swf), opened as SAL's
// level-up step: before SAL's skill menu with SAL API V2, after it with V1.
namespace ST::TraitMenu {
    // Call on kDataLoaded: registers the menu with the UI and ST's step with
    // SAL. Returns false when either is unavailable.
    bool Register();

    // SAL's wantsStep callback (main thread). True when the player has
    // unspent trait points and the menu is being opened; ST then owes SAL
    // exactly one ContinueLevelUp.
    bool WantsStep(std::uint32_t level);

    // Load, revert and new game: closes the menu and drops any open session.
    void ResetState();
}
