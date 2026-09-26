#include "PCH.h"
#include "SettingsPage.h"

#include "Config.h"
#include "SettingsModel.h"
#include "TraitState.h"

#include <SKSE/Translation.h>

// The vendored framework header declares ImGuiTextFilter as both struct and class.
#pragma warning(push)
#pragma warning(disable : 4099)
#include "SKSEMenuFramework.h"
#pragma warning(pop)

#include <array>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>

// ImGui rendering may run off the main thread. Every settings-model access
// takes s_mutex; saving and applying (which rewrites the Config globals and
// reconciles actor values) is queued to the main thread.
namespace ST::SettingsPage {
    namespace {
        using Json = SettingsModel::Json;
        namespace ImGui = ImGuiMCP;

        constexpr ImGui::ImVec4 kChangedColor{ 0.94f, 0.80f, 0.35f, 1.0f };
        constexpr ImGui::ImVec4 kErrorColor{ 0.95f, 0.40f, 0.40f, 1.0f };
        constexpr auto          kConfirmWindow = std::chrono::seconds(4);

        struct Section {
            const char*      titleKey;
            const char*      fallback;
            std::string_view prefix;
        };
        constexpr std::array kSections{
            Section{ "$ST_PAGE_SECTION_POINTS", "Trait points", "points." },
            Section{ "$ST_PAGE_SECTION_BONUSES", "Bonuses per point", "per_point." },
            Section{ "$ST_PAGE_SECTION_DEBUG", "Debug", "debug." },
        };

        std::mutex                              s_mutex;
        std::unordered_map<std::string, double> s_editing;  // in-progress numeric edits
        std::string                             s_status;
        bool                                    s_statusIsError{ false };
        std::chrono::steady_clock::time_point   s_resetArmedUntil{};
        bool                                    s_registered{ false };
        // Filled once on the main thread in Register(); rendering reads only this.
        std::unordered_map<std::string, std::string> s_text;

        constexpr std::array<std::string_view, 9> kPageKeys{
            "$ST_PAGE_SETTINGS", "$ST_PAGE_INTRO", "$ST_PAGE_DEFAULT", "$ST_PAGE_ON", "$ST_PAGE_OFF",
            "$ST_PAGE_INVALID", "$ST_PAGE_SAVE_FAILED", "$ST_PAGE_RESET_ALL", "$ST_PAGE_CONFIRM"
        };

        // Translated text, with the English fallback when the key is missing.
        std::string T(std::string_view key, std::string_view fallback)
        {
            const auto it = s_text.find(std::string(key));
            return it != s_text.end() ? it->second : std::string(fallback);
        }

        void CacheTranslation(const std::string& key)
        {
            std::string result;
            if (SKSE::Translation::Translate(key, result) && !result.empty()) {
                s_text[key] = std::move(result);
            }
        }

        std::string Token(std::string_view key);

        void CacheAllTranslations()
        {
            SKSE::Translation::ParseTranslation("SimpleTraits");
            for (const auto key : kPageKeys) CacheTranslation(std::string(key));
            for (const auto& section : kSections) CacheTranslation(section.titleKey);
            for (const auto& descriptor : SettingsModel::Registry()) {
                CacheTranslation("$ST_SETTING_" + Token(descriptor.key));
                CacheTranslation("$ST_DESC_" + Token(descriptor.key));
            }
        }

        std::string Token(std::string_view key)
        {
            std::string token;
            for (const char c : key) {
                token += c == '.' ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            return token;
        }

        std::string Label(std::string_view key) { return T("$ST_SETTING_" + Token(key), key); }
        std::string Description(std::string_view key) { return T("$ST_DESC_" + Token(key), ""); }

        std::string Format(const Json* value, int decimals = -1)
        {
            if (!value) return "?";
            if (value->is_boolean()) return value->get<bool>() ? T("$ST_PAGE_ON", "On") : T("$ST_PAGE_OFF", "Off");
            if (value->is_number_integer()) return std::to_string(value->get<std::int64_t>());
            if (value->is_number()) {
                return decimals >= 0 ? std::format("{:.{}f}", value->get<double>(), decimals)
                                     : std::format("{:g}", value->get<double>());
            }
            return value->dump();
        }

        void SetStatus(std::string text, bool error)
        {
            s_status = std::move(text);
            s_statusIsError = error;
        }

        // Saves SimpleTraits.user.json and applies the new values on the main thread.
        void QueueSave(std::string reason)
        {
            auto apply = [reason = std::move(reason)]() {
                std::lock_guard lock(s_mutex);
                std::string error;
                if (!Config::SaveAndApply(error)) {
                    logger::error("[ST] Settings page: save failed ({}): {}.", reason, error);
                    SetStatus(T("$ST_PAGE_SAVE_FAILED", "Could not save SimpleTraits.user.json; see the log."), true);
                    return;
                }
                TraitState::SetSettings(Config::traits);
                TraitState::ReconcileIfActive("settings-page");
                logger::info("[ST] Settings page: saved and applied ({}).", reason);
                SetStatus({}, false);
            };
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddTask(std::move(apply));
            } else {
                logger::warn("[ST] Settings page: task interface unavailable; change not saved.");
            }
        }

        // Caller holds s_mutex.
        void Commit(std::string_view key, const Json& value)
        {
            auto& model = Config::Settings();
            if (const auto* current = SettingsModel::Value(model.Effective(), key); current && *current == value) {
                return;
            }
            if (!model.Set(key, value)) {
                logger::info("[ST] Settings page: '{}' = {} rejected (out of range).", key, value.dump());
                SetStatus(Label(key) + ": " + T("$ST_PAGE_INVALID", "out of range; the previous value was kept."), true);
                return;
            }
            logger::info("[ST] Settings page: '{}' = {}.", key, value.dump());
            QueueSave(std::string(key));
        }

        void Tooltip(std::string_view key)
        {
            if (!ImGui::IsItemHovered()) return;
            const auto* descriptor = SettingsModel::Find(key);
            std::string text = Description(key);
            if (!text.empty()) text += "\n\n";
            text += T("$ST_PAGE_DEFAULT", "Default") + ": " +
                    Format(SettingsModel::Value(Config::Settings().Shipped(), key), descriptor ? descriptor->decimals : -1);
            ImGui::SetTooltip("%s", text.c_str());
        }

        // Numeric input that commits when editing finishes (Enter, focus
        // loss, or a +/- click), so partial typing is never saved.
        void DrawNumber(const SettingDescriptor& descriptor)
        {
            const std::string id(descriptor.key);
            const auto* current = SettingsModel::Value(Config::Settings().Effective(), descriptor.key);
            double value = s_editing.contains(id) ? s_editing[id] : (current && current->is_number() ? current->get<double>() : 0.0);
            ImGui::SetNextItemWidth(-1.0f);
            bool changed;
            const bool integer = descriptor.kind == SettingKind::Integer;
            if (integer) {
                int asInt = static_cast<int>(std::llround(value));
                changed = ImGui::InputInt("##value", &asInt, 1, 10);
                value = asInt;
            } else {
                const auto format = descriptor.decimals >= 0 ? std::format("%.{}f", descriptor.decimals) : std::string("%g");
                changed = ImGui::InputDouble("##value", &value, descriptor.step, descriptor.step * 10.0, format.c_str());
            }
            if (changed) s_editing[id] = value;
            if (ImGui::IsItemDeactivatedAfterEdit() && s_editing.contains(id)) {
                const double edited = s_editing[id];
                s_editing.erase(id);
                // Six significant digits drop step noise (0.1 + 0.2 style) so
                // the user file stores 0.03, not 0.030000000000000002.
                // Fixed-decimal settings are rounded to what the page shows.
                const double tidy = descriptor.decimals >= 0
                    ? std::stod(std::format("{:.{}f}", edited, descriptor.decimals))
                    : std::stod(std::format("{:.6g}", edited));
                Commit(descriptor.key, integer ? Json(static_cast<std::int64_t>(std::llround(edited))) : Json(tidy));
            }
            Tooltip(descriptor.key);
        }

        void DrawSetting(const SettingDescriptor& descriptor)
        {
            const auto& model = Config::Settings();
            const auto* current = SettingsModel::Value(model.Effective(), descriptor.key);
            const auto* shipped = SettingsModel::Value(model.Shipped(), descriptor.key);

            ImGui::PushID(std::string(descriptor.key).c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const auto label = Label(descriptor.key);
            if (current && shipped && *current != *shipped) {
                ImGui::TextColored(kChangedColor, "%s", label.c_str());
            } else {
                ImGui::Text("%s", label.c_str());
            }
            Tooltip(descriptor.key);
            ImGui::TableNextColumn();
            if (descriptor.kind == SettingKind::Toggle) {
                bool value = current && current->is_boolean() && current->get<bool>();
                if (ImGui::Checkbox("##value", &value)) Commit(descriptor.key, Json(value));
                Tooltip(descriptor.key);
            } else {
                DrawNumber(descriptor);
            }
            ImGui::PopID();
        }

        void DrawSection(const Section& section)
        {
            if (!ImGui::CollapsingHeader(T(section.titleKey, section.fallback).c_str(),
                    ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
                return;
            }
            ImGui::PushID(section.titleKey);
            if (ImGui::BeginTable("settings", 2, ImGui::ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("label", ImGui::ImGuiTableColumnFlags_WidthStretch, 0.6f);
                ImGui::TableSetupColumn("value", ImGui::ImGuiTableColumnFlags_WidthStretch, 0.4f);
                for (const auto& descriptor : SettingsModel::Registry()) {
                    if (descriptor.key.starts_with(section.prefix)) {
                        DrawSetting(descriptor);
                    }
                }
                ImGui::EndTable();
            }
            ImGui::PopID();
            ImGui::Spacing();
        }

        void __stdcall Render()
        {
            std::lock_guard lock(s_mutex);
            ImGui::TextWrapped("%s", T("$ST_PAGE_INTRO",
                "Changes are saved to SimpleTraits.user.json and take effect immediately. "
                "Hover a setting for its description and default.").c_str());
            ImGui::Spacing();
            for (const auto& section : kSections) {
                DrawSection(section);
            }

            const bool armed = std::chrono::steady_clock::now() < s_resetArmedUntil;
            if (ImGui::Button(armed ? T("$ST_PAGE_CONFIRM", "Click again to confirm").c_str()
                                    : T("$ST_PAGE_RESET_ALL", "Reset all to defaults").c_str())) {
                if (armed) {
                    s_resetArmedUntil = {};
                    s_editing.clear();
                    Config::Settings().ResetAll();
                    logger::info("[ST] Settings page: all settings reset to defaults.");
                    QueueSave("reset-all");
                } else {
                    s_resetArmedUntil = std::chrono::steady_clock::now() + kConfirmWindow;
                }
            }
            if (!s_status.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(s_statusIsError ? kErrorColor : kChangedColor, "%s", s_status.c_str());
            }
        }
    }

    bool Register()
    {
        if (s_registered) {
            return true;
        }
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::info("[ST] Settings page: SKSE Menu Framework not installed; edit SimpleTraits.user.json instead.");
            return false;
        }
        CacheAllTranslations();
        SKSEMenuFramework::SetSection("Simple Traits");
        SKSEMenuFramework::AddSectionItem(T("$ST_PAGE_SETTINGS", "Settings"), Render);
        s_registered = true;
        logger::info("[ST] Settings page: registered in SKSE Menu Framework (Simple Traits / Settings).");
        return true;
    }
}
