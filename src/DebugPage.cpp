#include "PCH.h"
#include "DebugPage.h"

#include "Config.h"
#include "TraitState.h"

// The vendored framework header declares ImGuiTextFilter as both struct and class.
#pragma warning(push)
#pragma warning(disable : 4099)
#include "SKSEMenuFramework.h"
#pragma warning(pop)

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

// Rendering may happen off the game's main thread. The page only reads
// TraitState snapshots (mutex-guarded) and queues spending to the main thread.
namespace ST::DebugPage {
    namespace {
        namespace ImGui = ImGuiMCP;
        using TraitRules::Trait;

        constexpr ImGui::ImVec4 kWarnColor{ 0.94f, 0.80f, 0.35f, 1.0f };

        std::mutex  s_statusMutex;
        std::string s_status;
        bool        s_registered = false;
        std::atomic<std::chrono::steady_clock::time_point> s_lastRender{};

        void SetStatus(std::string status)
        {
            std::lock_guard lock(s_statusMutex);
            s_status = std::move(status);
        }

        std::string Status()
        {
            std::lock_guard lock(s_statusMutex);
            return s_status;
        }

        const char* TraitLabel(Trait trait)
        {
            switch (trait) {
                case Trait::kStrength: return "Strength";
                case Trait::kResilience: return "Resilience";
                case Trait::kAgility: return "Agility";
                case Trait::kIntelligence: return "Intelligence";
                case Trait::kWisdom: return "Wisdom";
                case Trait::kCharisma: return "Charisma";
            }
            return "?";
        }

        // What a trait currently gives, for the debug table.
        std::string BonusText(Trait trait, const TraitState::Snapshot& snapshot)
        {
            for (std::size_t i = 0; i < TraitRules::kBonusCount; ++i) {
                const auto bonus = static_cast<TraitRules::Bonus>(i);
                if (TraitRules::BonusTrait(bonus) == trait) {
                    return std::format("+{:.1f} {}", snapshot.applied[i], TraitRules::BonusName(bonus));
                }
            }
            if (trait == Trait::kIntelligence) {
                return std::format("{} skill points granted", snapshot.skillPointsGranted);
            }
            if (trait == Trait::kCharisma) {
                return std::format("price factor x{:.3f}", snapshot.priceFactorScale);
            }
            return {};
        }

        void QueueSpend(Trait trait)
        {
            logger::info("[ST] Debug page: +1 {} clicked.", TraitLabel(trait));
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) {
                SetStatus("Task interface unavailable; point not spent.");
                return;
            }
            SetStatus(std::format("Spending a point on {}...", TraitLabel(trait)));
            tasks->AddTask([trait]() {
                const auto result = TraitState::SpendPoint(trait);
                if (!result) {
                    SetStatus("Load a save first.");
                } else if (*result == TraitRules::AllocationError::kNone) {
                    SetStatus(std::format("Spent a point on {}.", TraitLabel(trait)));
                } else {
                    SetStatus(std::format("{}: rejected ({}).", TraitLabel(trait),
                        TraitRules::AllocationErrorName(*result)));
                }
            });
        }

        // Logs when the page is shown after not being rendered for a while,
        // so opening the panel shows up in the log without per-frame spam.
        void LogIfOpened(const TraitState::Snapshot& snapshot)
        {
            const auto now = std::chrono::steady_clock::now();
            const auto last = s_lastRender.exchange(now);
            if (now - last < std::chrono::seconds(2)) {
                return;
            }
            logger::info("[ST] Debug page opened: {}.", snapshot.gameActive
                ? std::format("level {}, earned {}, spent {}, unspent {}", snapshot.level, snapshot.earned,
                      snapshot.spent, snapshot.unspent)
                : std::string("no game loaded"));
        }

        void __stdcall Render()
        {
            const auto snapshot = TraitState::GetSnapshot();
            LogIfOpened(snapshot);
            ImGui::TextColored(kWarnColor, "Temporary debug page. Points are permanent: there is no respec.");
            ImGui::Spacing();

            if (!snapshot.gameActive) {
                ImGui::Text("Load a save or start a new game.");
                return;
            }

            ImGui::Text("Level %lld: earned %lld, spent %lld, unspent %lld",
                snapshot.level, snapshot.earned, snapshot.spent, snapshot.unspent);
            ImGui::Spacing();

            constexpr auto flags = ImGui::ImGuiTableFlags_Borders | ImGui::ImGuiTableFlags_RowBg |
                                   ImGui::ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("traits", 4, flags)) {
                ImGui::TableSetupColumn("Trait");
                ImGui::TableSetupColumn("Points");
                ImGui::TableSetupColumn("Applied bonus");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                for (std::size_t i = 0; i < TraitRules::kTraitCount; ++i) {
                    const auto trait = static_cast<Trait>(i);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", TraitLabel(trait));
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", snapshot.allocation[i]);
                    ImGui::TableNextColumn();
                    const auto bonus = BonusText(trait, snapshot);
                    if (bonus.empty()) {
                        ImGui::TextDisabled("not implemented");
                    } else {
                        ImGui::Text("%s", bonus.c_str());
                    }
                    ImGui::TableNextColumn();
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::BeginDisabled(snapshot.unspent <= 0);
                    if (ImGui::Button("+1")) {
                        QueueSpend(trait);
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            if (const auto status = Status(); !status.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", status.c_str());
            }
        }
    }

    void Register()
    {
        if (s_registered) {
            return;
        }
        if (!Config::allocationPage) {
            logger::info("[ST] Debug page: disabled (debug.allocation_page=false).");
            return;
        }
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::warn("[ST] Debug page: debug.allocation_page is on but SKSE Menu Framework is not installed.");
            return;
        }
        SKSEMenuFramework::SetSection("Simple Traits");
        SKSEMenuFramework::AddSectionItem("Debug", Render);
        s_registered = true;
        logger::info("[ST] Debug page: registered in SKSE Menu Framework (Simple Traits / Debug).");
    }
}
