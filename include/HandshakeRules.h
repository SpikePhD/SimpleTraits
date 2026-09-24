#pragma once

#include <cstdint>
#include <string_view>

// Dependency-free validation of SAL's interface message (extern/SAL/SAL_API.h).
namespace ST::HandshakeRules {

    enum class InterfaceStatus {
        kAccepted,
        kWrongMessageType,
        kNullData,
        kTooSmall,            // payload shorter than SALInterfaceV1
        kUnsupportedVersion,  // version < 1
        kMissingFunction      // a V1 function pointer is null
    };

    [[nodiscard]] std::string_view StatusName(InterfaceStatus status) noexcept;

    // Later SAL versions only append members, so any version >= 1 whose
    // payload holds at least a complete SALInterfaceV1 is accepted.
    [[nodiscard]] InterfaceStatus ClassifyInterfaceMessage(
        std::uint32_t type, const void* data, std::uint32_t dataLen) noexcept;

    // For an accepted message: true when it is a V2 interface (version >= 2,
    // payload at least sizeof(SALInterfaceV2)) with a non-null
    // RegisterPreSkillMenuStep. Otherwise ST uses the V1 level-up step.
    [[nodiscard]] bool HasPreSkillMenuStep(const void* data, std::uint32_t dataLen) noexcept;

    // For an accepted message: true when it is a V3 interface (version >= 3,
    // payload at least sizeof(SALInterfaceV3)) with a non-null
    // RegisterSkillPointBonus. Without it Intelligence has no effect.
    [[nodiscard]] bool HasSkillPointBonus(const void* data, std::uint32_t dataLen) noexcept;
}
