#include "PCH.h"
#include "Config.h"

#include "SettingsModel.h"

#include <filesystem>
#include <fstream>
#include <optional>

namespace ST::Config {
    namespace {
        using Json = SettingsModel::Json;

        SettingsModel s_settings;

        // Resolve config files next to our own DLL. REX::W32::GetCurrentModule()
        // is &__ImageBase, so this follows MO2's virtual file system to the mod
        // folder rather than assuming the game's Data directory.
        std::filesystem::path PluginsDir()
        {
            wchar_t buf[260] = {};
            REX::W32::GetModuleFileNameW(REX::W32::GetCurrentModule(), buf, static_cast<std::uint32_t>(std::size(buf)));
            return std::filesystem::path(buf).parent_path();
        }

        // nullopt with an empty error means the file does not exist.
        std::optional<Json> ReadJson(const std::filesystem::path& path, std::string& error)
        {
            std::ifstream file(path);
            if (!file.is_open()) {
                std::error_code ec;
                if (std::filesystem::exists(path, ec)) error = "could not open file";
                return std::nullopt;
            }
            try {
                Json result;
                file >> result;
                return result;
            } catch (const Json::exception& e) {
                error = e.what();
                return std::nullopt;
            }
        }

        double Number(const Json& root, const char* section, const char* key)
        {
            return root[section][key].get<double>();
        }

        void ApplyEffective()
        {
            const auto& j = s_settings.Effective();
            verbose = j["debug"]["verbose"].get<bool>();
            maxLogFiles = j["debug"]["max_log_files"].get<int>();
            traits.points.startingPoints = j["points"]["starting_points"].get<int>();
            traits.points.levelsPerPoint = j["points"]["levels_per_point"].get<int>();
            traits.staminaPerPoint = static_cast<float>(Number(j, "per_point", "stamina"));
            traits.healthPerPoint = static_cast<float>(Number(j, "per_point", "health"));
            traits.magickaPerPoint = static_cast<float>(Number(j, "per_point", "magicka"));
            traits.criticalChancePerPoint = static_cast<float>(Number(j, "per_point", "critical_chance"));
            traits.intelligenceThresholdReduction =
                static_cast<float>(Number(j, "per_point", "intelligence_threshold_reduction"));
            traits.charismaPriceImprovement =
                static_cast<float>(Number(j, "per_point", "charisma_price_improvement"));
        }
    }

    LoadReport Load()
    {
        LoadReport report;
        const auto defaultsPath = PluginsDir() / "SimpleTraits.json";
        const auto userPath = PluginsDir() / "SimpleTraits.user.json";
        report.defaultsPath = defaultsPath.string();
        report.userPath = userPath.string();

        std::string error;
        auto shipped = ReadJson(defaultsPath, error);
        if (!shipped) {
            report.errors.push_back(error.empty()
                ? "defaults file not found; using built-in defaults"
                : "defaults file unreadable (" + error + "); using built-in defaults");
            shipped = Json::object();
        } else if (!shipped->is_object()) {
            report.errors.push_back("defaults file is not a JSON object; using built-in defaults");
            shipped = Json::object();
        }

        error.clear();
        auto user = ReadJson(userPath, error);
        report.userFilePresent = user.has_value() || !error.empty();
        if (!user && !error.empty()) {
            report.errors.push_back("user overrides unreadable (" + error + "); ignored");
        }

        try {
            s_settings.Load(*shipped, user.value_or(Json()), &report.warnings);
        } catch (const Json::exception& e) {
            report.errors.push_back(std::string("settings validation failed: ") + e.what());
        }
        ApplyEffective();
        return report;
    }

    void LogReport(const LoadReport& report)
    {
        for (const auto& error : report.errors) {
            logger::error("[ST] Config: {}.", error);
        }
        for (const auto& warning : report.warnings) {
            logger::warn("[ST] Config: {}.", warning);
        }
        logger::info("[ST] Config: defaults '{}', user overrides '{}' ({}).",
            report.defaultsPath, report.userPath, report.userFilePresent ? "present" : "absent");
        logger::info("[ST] Config: verbose={}, max_log_files={}", verbose, maxLogFiles);
        logger::info("[ST] Config: points - starting_points={}, levels_per_point={}",
            traits.points.startingPoints, traits.points.levelsPerPoint);
        logger::info("[ST] Config: per point - stamina={:.2f}, health={:.2f}, magicka={:.2f}, critical_chance={:.2f}%",
            traits.staminaPerPoint, traits.healthPerPoint, traits.magickaPerPoint, traits.criticalChancePerPoint);
        logger::info("[ST] Config: per point - intelligence_threshold_reduction={:.4f}, charisma_price_improvement={:.4f}",
            traits.intelligenceThresholdReduction, traits.charismaPriceImprovement);
    }
}
