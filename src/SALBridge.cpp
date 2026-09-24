#include "PCH.h"
#include "SALBridge.h"

#include "HandshakeRules.h"

#include <atomic>

namespace ST::SALBridge {
    namespace {
        std::atomic<State>                      s_state{ State::kPending };
        std::atomic<const SAL::SALInterfaceV1*> s_interface{ nullptr };
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
            s_state = State::kReady;
            logger::info("[ST] SAL: interface V{} received ({} bytes). No callbacks registered yet.",
                api->version, msg->dataLen);
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
}
