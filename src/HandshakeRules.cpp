#include "HandshakeRules.h"

#include "SAL_API.h"

#include <cstring>

namespace ST::HandshakeRules {

    std::string_view StatusName(InterfaceStatus status) noexcept
    {
        switch (status) {
            case InterfaceStatus::kAccepted: return "accepted";
            case InterfaceStatus::kWrongMessageType: return "wrong-message-type";
            case InterfaceStatus::kNullData: return "null-data";
            case InterfaceStatus::kTooSmall: return "payload-too-small";
            case InterfaceStatus::kUnsupportedVersion: return "unsupported-version";
            case InterfaceStatus::kMissingFunction: return "missing-function";
        }
        return "unknown";
    }

    InterfaceStatus ClassifyInterfaceMessage(
        std::uint32_t type, const void* data, std::uint32_t dataLen) noexcept
    {
        if (type != SAL::kMessageInterface) {
            return InterfaceStatus::kWrongMessageType;
        }
        if (!data) {
            return InterfaceStatus::kNullData;
        }
        // Read the version first so a short payload from an unknown older
        // layout reports its version problem rather than only its size.
        if (dataLen < sizeof(std::uint32_t)) {
            return InterfaceStatus::kTooSmall;
        }
        std::uint32_t version = 0;
        std::memcpy(&version, data, sizeof(version));
        if (version < SAL::kInterfaceVersion1) {
            return InterfaceStatus::kUnsupportedVersion;
        }
        if (dataLen < sizeof(SAL::SALInterfaceV1)) {
            return InterfaceStatus::kTooSmall;
        }
        const auto* api = static_cast<const SAL::SALInterfaceV1*>(data);
        if (!api->RegisterThresholdMultiplier || !api->RequestThresholdRefresh ||
            !api->RegisterLevelUpStep || !api->ContinueLevelUp || !api->RegisterCharacterCreated) {
            return InterfaceStatus::kMissingFunction;
        }
        return InterfaceStatus::kAccepted;
    }

    bool HasPreSkillMenuStep(const void* data, std::uint32_t dataLen) noexcept
    {
        if (!data || dataLen < sizeof(SAL::SALInterfaceV2)) {
            return false;
        }
        const auto* api = static_cast<const SAL::SALInterfaceV2*>(data);
        return api->v1.version >= SAL::kInterfaceVersion2 && api->RegisterPreSkillMenuStep != nullptr;
    }

    bool HasSkillPointBonus(const void* data, std::uint32_t dataLen) noexcept
    {
        if (!data || dataLen < sizeof(SAL::SALInterfaceV3)) {
            return false;
        }
        const auto* api = static_cast<const SAL::SALInterfaceV3*>(data);
        return api->v2.v1.version >= SAL::kInterfaceVersion3 && api->RegisterSkillPointBonus != nullptr;
    }
}
