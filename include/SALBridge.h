#pragma once

#include "SAL_API.h"

#include <cstdint>
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

    // True when SAL offers the V2 pre-skill-menu step.
    [[nodiscard]] bool HasPreSkillMenuStep() noexcept;

    // Registers ST's level-up step: SAL V2's pre-skill-menu step when
    // available, otherwise V1's post-skill-menu step. Call on kDataLoaded or
    // later. Returns false when SAL is unavailable or rejects it.
    bool RegisterLevelUpStep(bool (*wantsStep)(std::uint32_t level));

    // True when SAL offers the V4 XP multiplier (Intelligence).
    [[nodiscard]] bool HasXPMultiplier() noexcept;

    // Registers the Intelligence XP multiplier provider with SAL V4. Returns
    // false (logged) when SAL does not offer it or rejects it.
    bool RegisterXPMultiplier(float (*provider)(std::uint32_t sourceCategory));

    // Resumes SAL's level-up after ST's step. Call exactly once per wait
    // (TraitMenuRules::ContinuationGuard); SAL does the work on the main thread.
    void ContinueLevelUp();
}
