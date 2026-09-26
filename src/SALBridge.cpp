#include "PCH.h"
#include "SALBridge.h"

#include "HandshakeRules.h"

#include <atomic>

namespace ST::SALBridge {
    namespace {
        std::atomic<State>                      s_state{ State::kPending };
        std::atomic<const SAL::SALInterfaceV1*> s_interface{ nullptr };
        std::atomic<const SAL::SALInterfaceV2*> s_interfaceV2{ nullptr };  // set only for a usable V2
        std::atomic<const SAL::SALInterfaceV4*> s_interfaceV4{ nullptr };  // set only for a usable V4
        bool                                    s_finalized{ false };

        void OnSALMessage(SKSE::MessagingInterface::Message* msg)
        {
            if (!msg) {
                logger::warn("[ST] SAL: listener received a null message.");
                return;
            }
            if (msg->type != SAL::kMessageInterface) {
                logger::debug("[ST] SAL: ignoring message type {:#010x}.", msg->type);
                return;
            }
            if (s_state.load() == State::kReady) {
                logger::warn("[ST] SAL: duplicate interface message ignored; keeping the first.");
                return;
            }

            const auto status = HandshakeRules::ClassifyInterfaceMessage(msg->type, msg->data, msg->dataLen);
            if (status != HandshakeRules::InterfaceStatus::kAccepted) {
                s_state = State::kUnsupported;
                logger::error("[ST] SAL: interface rejected ({}; payload {} bytes, V1 needs {}). "
                              "SAL-dependent features are disabled; update Simple Alternate Levelling.",
                    HandshakeRules::StatusName(status), msg->dataLen, sizeof(SAL::SALInterfaceV1));
                return;
            }

            const auto* api = static_cast<const SAL::SALInterfaceV1*>(msg->data);
            s_interface = api;
            const bool v2 = HandshakeRules::HasPreSkillMenuStep(msg->data, msg->dataLen);
            if (v2) {
                s_interfaceV2 = static_cast<const SAL::SALInterfaceV2*>(msg->data);
            }
            const bool v4 = HandshakeRules::HasXPMultiplier(msg->data, msg->dataLen);
            if (v4) {
                s_interfaceV4 = static_cast<const SAL::SALInterfaceV4*>(msg->data);
            }
            s_state = State::kReady;
            logger::info("[ST] SAL: interface V{} received ({} bytes); pre-skill-menu step {}; XP multiplier {}.",
                api->version, msg->dataLen, v2 ? "available" : "unavailable (V1 step fallback)",
                v4 ? "available" : "unavailable (Intelligence has no effect; needs SAL API V4)");
        }
    }

    std::string_view StateName(State state) noexcept
    {
        switch (state) {
            case State::kPending: return "pending";
            case State::kReady: return "ready";
            case State::kMissing: return "missing";
            case State::kUnsupported: return "unsupported";
        }
        return "unknown";
    }

    bool RegisterListener()
    {
        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            s_state = State::kMissing;
            logger::error("[ST] SAL: MessagingInterface unavailable; SAL-dependent features are disabled.");
            return false;
        }
        // SKSE refuses a listener for a sender that is not loaded.
        if (!messaging->RegisterListener(SAL::kSenderName, OnSALMessage)) {
            s_state = State::kMissing;
            logger::warn("[ST] SAL: '{}' is not loaded; SAL-dependent features (Intelligence) are disabled.",
                SAL::kSenderName);
            return false;
        }
        logger::info("[ST] SAL: listening for the '{}' interface.", SAL::kSenderName);
        return true;
    }

    void Finalize()
    {
        if (s_finalized) {
            return;
        }
        s_finalized = true;
        auto expected = State::kPending;
        if (s_state.compare_exchange_strong(expected, State::kMissing)) {
            logger::warn("[ST] SAL: no interface received by kDataLoaded (SAL too old or failed to load); "
                         "SAL-dependent features (Intelligence) are disabled.");
        }
        logger::info("[ST] SAL: handshake {}.", StateName(s_state.load()));
    }

    State GetState() noexcept
    {
        return s_state.load();
    }

    bool IsAvailable() noexcept
    {
        return s_state.load() == State::kReady;
    }

    const SAL::SALInterfaceV1* Interface() noexcept
    {
        return IsAvailable() ? s_interface.load() : nullptr;
    }

    bool HasPreSkillMenuStep() noexcept
    {
        return IsAvailable() && s_interfaceV2.load() != nullptr;
    }

    bool RegisterLevelUpStep(bool (*wantsStep)(std::uint32_t level))
    {
        const auto* api = Interface();
        if (!api) {
            logger::warn("[ST] SAL: level-up step not registered; SAL is {}.", StateName(GetState()));
            return false;
        }
        if (const auto* v2 = HasPreSkillMenuStep() ? s_interfaceV2.load() : nullptr) {
            const bool ok = v2->RegisterPreSkillMenuStep(wantsStep);
            if (ok) {
                logger::info("[ST] SAL: pre-skill-menu step registered (trait menu opens before SAL's skill menu).");
            } else {
                logger::error("[ST] SAL: pre-skill-menu step rejected by SAL; the trait menu will not open on level-up.");
            }
            return ok;
        }
        const bool ok = api->RegisterLevelUpStep(wantsStep);
        if (ok) {
            logger::info("[ST] SAL: V1 level-up step registered (trait menu opens after SAL's skill menu).");
        } else {
            logger::error("[ST] SAL: V1 level-up step rejected by SAL; the trait menu will not open on level-up.");
        }
        return ok;
    }

    bool HasXPMultiplier() noexcept
    {
        return IsAvailable() && s_interfaceV4.load() != nullptr;
    }

    bool RegisterXPMultiplier(float (*provider)(std::uint32_t sourceCategory))
    {
        const auto* v4 = HasXPMultiplier() ? s_interfaceV4.load() : nullptr;
        if (!v4) {
            logger::warn("[ST] SAL: no XP multiplier hook (SAL API V4); Intelligence has no effect.");
            return false;
        }
        if (!v4->RegisterXPMultiplier(provider)) {
            logger::error("[ST] SAL: XP multiplier rejected by SAL; Intelligence has no effect.");
            return false;
        }
        logger::info("[ST] SAL: XP multiplier registered (Intelligence).");
        return true;
    }

    void ContinueLevelUp()
    {
        if (const auto* api = Interface()) {
            api->ContinueLevelUp();
        } else {
            logger::warn("[ST] SAL: ContinueLevelUp skipped; SAL is {}.", StateName(GetState()));
        }
    }
}
