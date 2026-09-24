#pragma once

#include "SAL_API.h"

#include <string_view>

// Simple Traits side of the SAL handshake (extern/SAL/SAL_API.h).
namespace ST::SALBridge {

    enum class State {
        kPending,      // listener registered, interface not received yet
        kReady,        // valid SALInterfaceV1 received
        kMissing,      // SAL not loaded, or never broadcast by kDataLoaded
        kUnsupported   // SAL broadcast an interface this plugin cannot use
    };

    [[nodiscard]] std::string_view StateName(State state) noexcept;

    // Call during SKSEPlugin_Load. Returns false if the listener could not be
    // registered (SAL is not loaded); the state is then kMissing.
    bool RegisterListener();

    // Call on kDataLoaded. A still-pending handshake becomes kMissing, and
    // the outcome is logged once.
    void Finalize();

    [[nodiscard]] State GetState() noexcept;

    // True only in kReady: SAL-dependent features may run.
    [[nodiscard]] bool IsAvailable() noexcept;

    // The received interface in kReady, nullptr otherwise.
    [[nodiscard]] const SAL::SALInterfaceV1* Interface() noexcept;
}
