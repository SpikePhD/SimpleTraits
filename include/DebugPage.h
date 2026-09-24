#pragma once

// Temporary "Simple Traits / Debug" page in SKSE Menu Framework for spending
// trait points before the real allocation menu exists.
namespace ST::DebugPage {
    // Call on kDataLoaded. Registers the page when debug.allocation_page is on
    // and the framework is installed; otherwise logs why and does nothing.
    void Register();
}
