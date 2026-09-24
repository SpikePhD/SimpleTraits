#include "PCH.h"
#include "Config.h"
#include "DebugPage.h"
#include "DiagnosticSinks.h"
#include "LogPolicy.h"
#include "SALBridge.h"
#include "SettingsPage.h"
#include "TraitMenu.h"
#include "TraitState.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>

namespace {

#ifndef ST_VERSION
#define ST_VERSION "unknown"
#endif

    void InitializeLog(bool verbose, int maxLogFiles)
    {
        auto logDir = logger::log_directory();
        if (!logDir) {
            SKSE::stl::report_and_fail("Failed to find SKSE log directory."sv);
        }

        // Timestamped filename: SimpleTraits_2026-09-24_10-26-21.log
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &time);
        std::ostringstream ts;
        ts << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
        const auto logPath = *logDir / std::format("SimpleTraits_{}.log", ts.str());

        try {
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
            auto log = std::make_shared<spdlog::logger>("ST", std::move(sink));
            const auto level = verbose ? spdlog::level::trace : spdlog::level::info;
            log->set_level(level);
            log->flush_on(level);
            spdlog::set_default_logger(std::move(log));
        } catch (const spdlog::spdlog_ex& error) {
            SKSE::stl::report_and_fail(std::format(
                "Failed to create Simple Traits log '{}': {}", logPath.string(), error.what()));
        }

        if (maxLogFiles == 0) {
            return;
        }

        std::error_code ec;
        std::vector<std::string> names;
        std::filesystem::directory_iterator iterator(*logDir, ec);
        const std::filesystem::directory_iterator end;
        while (!ec && iterator != end) {
            std::error_code entryError;
            if (iterator->is_regular_file(entryError) && !entryError) {
                names.push_back(iterator->path().filename().string());
            }
            iterator.increment(ec);
        }
        if (ec) {
            logger::warn("[ST] Log rotation: could not enumerate '{}': {}.", logDir->string(), ec.message());
            return;
        }

        for (const auto& name : ST::LogPolicy::SelectLogsToDelete(std::move(names), maxLogFiles)) {
            ec.clear();
            if (!std::filesystem::remove(*logDir / name, ec) && ec) {
                logger::warn("[ST] Log rotation: could not remove '{}': {}.", name, ec.message());
            }
        }
    }

    // Save name carried by kPreLoadGame / kSaveGame (not null-terminated).
    std::string MessageText(const SKSE::MessagingInterface::Message* msg)
    {
        if (!msg->data || msg->dataLen == 0) {
            return "unknown";
        }
        std::string text(static_cast<const char*>(msg->data), msg->dataLen);
        if (const auto nul = text.find(char{ 0 }); nul != std::string::npos) {
            text.resize(nul);
        }
        return text;
    }

    void OnDataLoaded()
    {
        static bool handled = false;
        if (handled) {
            logger::debug("[ST] OnDataLoaded: duplicate message ignored.");
            return;
        }
        handled = true;
        ST::SALBridge::Finalize();
        // SAL is a hard requirement; this only guards a broken install.
        switch (ST::SALBridge::GetState()) {
            case ST::SALBridge::State::kMissing:
                RE::DebugMessageBox("Simple Traits: Simple Alternate Levelling NOT LOADED!");
                break;
            case ST::SALBridge::State::kUnsupported:
                RE::DebugMessageBox("Simple Traits: this Simple Alternate Levelling version is NOT SUPPORTED!");
                break;
            default:
                break;
        }
        ST::DiagnosticSinks::Register();
        ST::TraitMenu::Register();
        ST::SALBridge::RegisterSkillPointBonus(ST::TraitState::TakeSkillPointBonus);
        ST::SettingsPage::Register();
        ST::DebugPage::Register();
        logger::info("[ST] All systems initialised (SAL {}).", ST::SALBridge::StateName(ST::SALBridge::GetState()));
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    // Config is read before logging because it holds the log settings; its
    // report is logged as soon as the logger exists.
    const auto configReport = ST::Config::Load();
    InitializeLog(ST::Config::verbose, ST::Config::maxLogFiles);
    logger::info("[ST] SimpleTraits loaded. Version {}", ST_VERSION);
    ST::Config::LogReport(configReport);
    ST::TraitState::SetSettings(ST::Config::traits);

    if (!a_skse) {
        logger::critical("[ST] SKSE LoadInterface is null; plugin load aborted.");
        return false;
    }
    // Keep the timestamped logger from InitializeLog(). CommonLib's default
    // InitInfo creates its own logger and would replace the default logger.
    SKSE::Init(a_skse, { .log = false });

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        logger::critical("[ST] SKSE MessagingInterface is unavailable; plugin load aborted.");
        return false;
    }
    if (!messaging->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
            if (!msg) {
                logger::warn("[ST] SKSE messaging callback received a null message.");
                return;
            }
            switch (msg->type) {
                case SKSE::MessagingInterface::kPostPostLoad:
                    // SAL broadcasts from its own kPostPostLoad handler, which
                    // may run before or after this one.
                    logger::info("[ST] kPostPostLoad: SAL handshake {}.",
                        ST::SALBridge::StateName(ST::SALBridge::GetState()));
                    break;
                case SKSE::MessagingInterface::kDataLoaded:
                    OnDataLoaded();
                    break;
                case SKSE::MessagingInterface::kPreLoadGame:
                    logger::info("[ST] Loading save '{}'.", MessageText(msg));
                    ST::TraitMenu::ResetState();
                    break;
                case SKSE::MessagingInterface::kSaveGame:
                    logger::info("[ST] Saving game '{}'.", MessageText(msg));
                    break;
                case SKSE::MessagingInterface::kPostLoadGame:
                    logger::info("[ST] Save loaded ({}).", msg->data ? "success" : "failed");
                    ST::DiagnosticSinks::Reset();
                    // The main save and the cosave are both loaded here.
                    ST::TraitState::Reconcile("post-load-game");
                    break;
                case SKSE::MessagingInterface::kNewGame:
                    ST::TraitMenu::ResetState();
                    ST::DiagnosticSinks::Reset();
                    ST::TraitState::OnNewGame();
                    break;
                default:
                    break;
            }
        })) {
        logger::critical("[ST] Failed to register the SKSE messaging listener; plugin load aborted.");
        return false;
    }

    if (!ST::TraitState::RegisterSerialization()) {
        return false;
    }

    // Without SAL the plugin keeps running with SAL-dependent features disabled.
    ST::SALBridge::RegisterListener();

    return true;
}
