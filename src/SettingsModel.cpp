#include "SettingsModel.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>

namespace ST {
    namespace {
        using Json = SettingsModel::Json;

        // Built-in defaults match data/SKSE/Plugins/SimpleTraits.json and
        // TraitRules::TraitSettings; the settings test checks both.
        // Order is also the settings page order.
        constexpr std::array kRegistry{
            SettingDescriptor{ "points.starting_points", SettingKind::Integer, 0, 100, 4, 1 },
            SettingDescriptor{ "points.levels_per_point", SettingKind::Integer, 1, 100, 3, 1 },
            SettingDescriptor{ "per_point.stamina_percent", SettingKind::Number, 0, 1, 0.05, 0.01 },
            SettingDescriptor{ "per_point.health_percent", SettingKind::Number, 0, 1, 0.05, 0.01 },
            SettingDescriptor{ "per_point.magicka_percent", SettingKind::Number, 0, 1, 0.05, 0.01 },
            SettingDescriptor{ "per_point.critical_chance", SettingKind::Number, 0, 10, 1, 0.5 },
            SettingDescriptor{ "per_point.intelligence_xp_percent", SettingKind::Number, 0, 0.5, 0.1, 0.01, 2 },
            SettingDescriptor{ "per_point.charisma_price_improvement", SettingKind::Number, 0, 0.05, 0.01, 0.005 },
            SettingDescriptor{ "debug.verbose", SettingKind::Toggle, 0, 1, 0, 1 },
            SettingDescriptor{ "debug.max_log_files", SettingKind::Integer, 0, 1000, 10, 1 },
            SettingDescriptor{ "debug.allocation_page", SettingKind::Toggle, 0, 1, 0, 1 },
        };

        const Json* FindPath(const Json& root, std::string_view path)
        {
            const Json* node = &root;
            std::size_t start = 0;
            while (true) {
                const auto end = path.find('.', start);
                const std::string key(path.substr(start, end == std::string_view::npos ? end : end - start));
                if (!node->is_object() || !node->contains(key)) return nullptr;
                node = &(*node)[key];
                if (end == std::string_view::npos) return node;
                start = end + 1;
            }
        }

        void Put(Json& root, std::string_view path, const Json& value)
        {
            Json* node = &root;
            std::size_t start = 0;
            while (true) {
                const auto end = path.find('.', start);
                const std::string key(path.substr(start, end == std::string_view::npos ? end : end - start));
                if (end == std::string_view::npos) {
                    (*node)[key] = value;
                    return;
                }
                node = &(*node)[key];
                start = end + 1;
            }
        }

        // Integer settings stay JSON integers even when written as 20.0.
        // Callers validate first, so the value is integral and in range.
        Json Normalized(const SettingDescriptor& descriptor, const Json& value)
        {
            if (descriptor.kind == SettingKind::Integer && value.is_number_float()) {
                return Json(static_cast<std::int64_t>(value.get<double>()));
            }
            return value;
        }

        Json DefaultValue(const SettingDescriptor& descriptor)
        {
            switch (descriptor.kind) {
                case SettingKind::Toggle: return Json(descriptor.builtInDefault != 0);
                case SettingKind::Integer: return Json(static_cast<std::int64_t>(descriptor.builtInDefault));
                case SettingKind::Number: break;
            }
            return Json(descriptor.builtInDefault);
        }

        // Keys from earlier versions: ignored with an explanation instead of
        // the generic unknown-key warning. Saving drops them from the user file.
        struct RetiredSetting {
            std::string_view key;
            std::string_view reason;
        };
        constexpr std::array kRetired{
            RetiredSetting{ "per_point.intelligence_skill_points",
                "Intelligence now multiplies XP; see per_point.intelligence_xp_percent" },
        };

        const RetiredSetting* FindRetired(std::string_view key)
        {
            for (const auto& retired : kRetired) {
                if (retired.key == key) return &retired;
            }
            return nullptr;
        }

        bool IsRegistered(std::string_view key)
        {
            for (const auto& descriptor : kRegistry) {
                if (descriptor.key == key) return true;
            }
            return false;
        }

        void Warn(std::vector<std::string>* warnings, std::string message)
        {
            if (warnings) warnings->push_back(std::move(message));
        }
    }

    std::span<const SettingDescriptor> SettingsModel::Registry()
    {
        return kRegistry;
    }

    const SettingDescriptor* SettingsModel::Find(std::string_view key)
    {
        for (const auto& descriptor : kRegistry) {
            if (descriptor.key == key) return &descriptor;
        }
        return nullptr;
    }

    const SettingsModel::Json* SettingsModel::Value(const Json& root, std::string_view key)
    {
        return FindPath(root, key);
    }

    bool SettingsModel::Set(std::string_view key, const Json& value)
    {
        const auto* descriptor = Find(key);
        if (!descriptor || !Valid(*descriptor, value)) return false;
        Put(effective_, key, Normalized(*descriptor, value));
        return true;
    }

    void SettingsModel::ResetAll()
    {
        effective_ = shipped_;
    }

    SettingsModel::Json SettingsModel::Overrides() const
    {
        Json result = { { "config_version", kSchemaVersion } };
        for (const auto& descriptor : kRegistry) {
            const auto* value = FindPath(effective_, descriptor.key);
            const auto* base = FindPath(shipped_, descriptor.key);
            if (value && base && *value != *base) Put(result, descriptor.key, *value);
        }
        return result;
    }

    bool SettingsModel::Valid(const SettingDescriptor& descriptor, const Json& value)
    {
        if (descriptor.kind == SettingKind::Toggle) return value.is_boolean();
        if (!value.is_number()) return false;
        double number;
        try { number = value.get<double>(); }
        catch (const Json::exception&) { return false; }
        if (!std::isfinite(number) || number < descriptor.minimum || number > descriptor.maximum) return false;
        return descriptor.kind != SettingKind::Integer || std::trunc(number) == number;
    }

    Json SettingsModel::BuiltInDefaults()
    {
        Json result = { { "config_version", kSchemaVersion } };
        for (const auto& descriptor : kRegistry) {
            Put(result, descriptor.key, DefaultValue(descriptor));
        }
        return result;
    }

    bool SettingsModel::Load(const Json& shipped, const Json& user, std::vector<std::string>* warnings)
    {
        if (!shipped.is_object()) return false;

        Json effective = BuiltInDefaults();
        for (const auto& descriptor : kRegistry) {
            if (const auto* value = FindPath(shipped, descriptor.key)) {
                if (Valid(descriptor, *value)) {
                    Put(effective, descriptor.key, Normalized(descriptor, *value));
                } else {
                    Warn(warnings, "invalid shipped value for " + std::string(descriptor.key) + " ignored; using built-in default");
                }
            }
        }

        bool applyUser = user.is_object();
        if (!user.is_null() && !user.is_object()) {
            Warn(warnings, "user config is not a JSON object; ignored");
        }
        if (applyUser && user.contains("config_version")) {
            const auto& version = user["config_version"];
            if (!version.is_number_integer()) {
                Warn(warnings, "invalid user config_version; user overrides ignored");
                applyUser = false;
            } else if (version.get<std::int64_t>() > kSchemaVersion) {
                Warn(warnings, "user config_version is newer than this plugin supports; user overrides ignored");
                applyUser = false;
            }
        }

        if (applyUser) {
            for (const auto& descriptor : kRegistry) {
                if (const auto* value = FindPath(user, descriptor.key)) {
                    if (Valid(descriptor, *value)) {
                        Put(effective, descriptor.key, Normalized(descriptor, *value));
                    } else {
                        Warn(warnings, "invalid override ignored: " + std::string(descriptor.key));
                    }
                }
            }
            std::function<void(const Json&, const std::string&)> visit = [&](const Json& node, const std::string& prefix) {
                for (auto it = node.begin(); it != node.end(); ++it) {
                    if (it.key().starts_with('_') || (prefix.empty() && it.key() == "config_version")) continue;
                    const auto key = prefix.empty() ? it.key() : prefix + "." + it.key();
                    if (it.value().is_object()) {
                        visit(it.value(), key);
                    } else if (const auto* retired = FindRetired(key)) {
                        Warn(warnings, "retired setting ignored: " + key + " (" + std::string(retired->reason) + ")");
                    } else if (!IsRegistered(key)) {
                        Warn(warnings, "unknown override ignored: " + key);
                    }
                }
            };
            visit(user, "");
        }

        // Shipped defaults are resolved before user overrides are applied;
        // rebuild them from the validated shipped values alone.
        shipped_ = BuiltInDefaults();
        for (const auto& descriptor : kRegistry) {
            if (const auto* value = FindPath(shipped, descriptor.key); value && Valid(descriptor, *value)) {
                Put(shipped_, descriptor.key, Normalized(descriptor, *value));
            }
        }
        effective_ = std::move(effective);
        return true;
    }
}
