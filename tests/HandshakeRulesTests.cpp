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
    bool RegisterBonus(std::int32_t (*)(std::uint32_t)) { return true; }
    bool RegisterMultiplier(float (*)(std::uint32_t)) { return true; }

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

    // V2: pre-skill-menu step detection, with V1 fallback.
    Check(!HasPreSkillMenuStep(&api, size), "V1 payload has no pre-step");
    const auto v2size = static_cast<std::uint32_t>(sizeof(SAL::SALInterfaceV2));
    SAL::SALInterfaceV2 v2{ ValidInterface(), RegisterStep };
    v2.v1.version = SAL::kInterfaceVersion2;
    Check(ClassifyInterfaceMessage(SAL::kMessageInterface, &v2, v2size) == InterfaceStatus::kAccepted,
        "V2 payload accepted as an interface");
    Check(HasPreSkillMenuStep(&v2, v2size), "V2 payload has the pre-step");
    Check(!HasPreSkillMenuStep(&v2, v2size - 1), "truncated V2 payload falls back to V1");
    Check(!HasPreSkillMenuStep(nullptr, v2size), "null data has no pre-step");
    auto versionOne = v2;
    versionOne.v1.version = SAL::kInterfaceVersion1;
    Check(!HasPreSkillMenuStep(&versionOne, v2size), "version 1 with a larger payload has no pre-step");
    auto nullStep = v2;
    nullStep.RegisterPreSkillMenuStep = nullptr;
    Check(!HasPreSkillMenuStep(&nullStep, v2size), "null pre-step function falls back to V1");

    // V4: XP multiplier detection. V3's skill point bonus is no longer used.
    const auto v3size = static_cast<std::uint32_t>(sizeof(SAL::SALInterfaceV3));
    SAL::SALInterfaceV3 v3{ v2, RegisterBonus };
    v3.v2.v1.version = SAL::kInterfaceVersion3;
    const auto v4size = static_cast<std::uint32_t>(sizeof(SAL::SALInterfaceV4));
    SAL::SALInterfaceV4 v4{ v3, RegisterMultiplier };
    v4.v3.v2.v1.version = SAL::kInterfaceVersion4;
    Check(ClassifyInterfaceMessage(SAL::kMessageInterface, &v4, v4size) == InterfaceStatus::kAccepted,
        "V4 payload accepted as an interface");
    Check(HasPreSkillMenuStep(&v4, v4size), "V4 payload still has the V2 pre-step");
    Check(HasXPMultiplier(&v4, v4size), "V4 payload has the XP multiplier");
    Check(!HasXPMultiplier(&v3, v3size), "V3 payload has no XP multiplier");
    Check(!HasXPMultiplier(&v4, v4size - 1), "truncated V4 payload has no XP multiplier");
    Check(!HasXPMultiplier(nullptr, v4size), "null data has no XP multiplier");
    auto v4AsV3 = v4;
    v4AsV3.v3.v2.v1.version = SAL::kInterfaceVersion3;
    Check(!HasXPMultiplier(&v4AsV3, v4size), "version 3 with a larger payload has no XP multiplier");
    auto nullMultiplier = v4;
    nullMultiplier.RegisterXPMultiplier = nullptr;
    Check(!HasXPMultiplier(&nullMultiplier, v4size), "null multiplier function is not usable");
    Check(SAL::kXPSourceQuest == 0 && SAL::kXPSourcePickpocket == 5, "vendored XP source categories");

    Check(StatusName(InterfaceStatus::kAccepted) == "accepted", "status name");
    Check(std::string_view(SAL::kSenderName) == "SimpleAlternateLevelling", "vendored sender name");

    if (failures == 0) {
        std::cout << "All handshake tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
