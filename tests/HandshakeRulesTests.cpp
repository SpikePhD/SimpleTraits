#include "HandshakeRules.h"

#include "SAL_API.h"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace {
    int failures = 0;

    void Check(bool condition, std::string_view message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    bool RegisterMultiplier(float (*)()) { return true; }
    void RequestRefresh() {}
    bool RegisterStep(bool (*)(std::uint32_t)) { return true; }
    void Continue() {}
    bool RegisterCreated(void (*)()) { return true; }

    SAL::SALInterfaceV1 ValidInterface()
    {
        return { SAL::kInterfaceVersion1, RegisterMultiplier, RequestRefresh, RegisterStep, Continue, RegisterCreated };
    }

    // A later SAL version: V1 layout followed by appended members.
    struct FutureInterface {
        SAL::SALInterfaceV1 v1;
        void (*appended)();
    };
}

int main()
{
    using namespace ST::HandshakeRules;
    constexpr auto size = static_cast<std::uint32_t>(sizeof(SAL::SALInterfaceV1));
    auto api = ValidInterface();

    Check(ClassifyInterfaceMessage(SAL::kMessageInterface, &api, size) == InterfaceStatus::kAccepted,
        "valid V1 accepted");
    Check(ClassifyInterfaceMessage(SAL::kMessageInterface + 1, &api, size) == InterfaceStatus::kWrongMessageType,
        "other message type rejected");
    Check(ClassifyInterfaceMessage(SAL::kMessageInterface, nullptr, size) == InterfaceStatus::kNullData,
        "null data rejected");
    Check(ClassifyInterfaceMessage(SAL::kMessageInterface, &api, 2) == InterfaceStatus::kTooSmall,
        "payload smaller than the version field rejected");
    Check(ClassifyInterfaceMessage(SAL::kMessageInterface, &api, size - 1) == InterfaceStatus::kTooSmall,
        "truncated V1 payload rejected");

    auto versionZero = ValidInterface();
    versionZero.version = 0;
    Check(ClassifyInterfaceMessage(SAL::kMessageInterface, &versionZero, size) == InterfaceStatus::kUnsupportedVersion,
        "version 0 rejected");

    auto missing = ValidInterface();
    missing.ContinueLevelUp = nullptr;
    Check(ClassifyInterfaceMessage(SAL::kMessageInterface, &missing, size) == InterfaceStatus::kMissingFunction,
        "null function pointer rejected");

    FutureInterface future{ ValidInterface(), Continue };
    future.v1.version = 2;
    Check(ClassifyInterfaceMessage(SAL::kMessageInterface, &future, static_cast<std::uint32_t>(sizeof(future))) ==
              InterfaceStatus::kAccepted,
        "later version with appended members accepted");

    Check(StatusName(InterfaceStatus::kAccepted) == "accepted", "status name");
    Check(std::string_view(SAL::kSenderName) == "SimpleAlternateLevelling", "vendored sender name");

    if (failures == 0) {
        std::cout << "All handshake tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
