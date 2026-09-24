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
        constexpr std::array kRegistry{
            SettingDescriptor{ "debug.verbose", SettingKind::Toggle, 0, 1, 0 },
            SettingDescriptor{ "debug.max_log_files", SettingKind::Integer, 0, 1000, 10 },
            SettingDescriptor{ "debug.allocation_page", SettingKind::Toggle, 0, 1, 0 },
            SettingDescriptor{ "points.starting_points", SettingKind::Integer, 0, 100, 4 },
            SettingDescriptor{ "points.levels_per_point", SettingKind::Integer, 1, 100, 3 },
            SettingDescriptor{ "per_point.stamina", SettingKind::Number, 0, 100, 5 },
            SettingDescriptor{ "per_point.health", SettingKind::Number, 0, 100, 5 },
            SettingDescriptor{ "per_point.magicka", SettingKind::Number, 0, 100, 5 },
            SettingDescriptor{ "per_point.critical_chance", SettingKind::Number, 0, 10, 1 },
            SettingDescriptor{ "per_point.intelligence_threshold_reduction", SettingKind::Number, 0, 0.5, 0.02 },
            SettingDescriptor{ "per_point.charisma_price_improvement", SettingKind::Number, 0, 0.05, 0.01 },
        };

        const Json* Find(const Json& root, std::string_view path)
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
            if (const auto* value = Find(shipped, descriptor.key)) {
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
                if (const auto* value = Find(user, descriptor.key)) {
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
                    } else if (!IsRegistered(key)) {
                        Warn(warnings, "unknown override ignored: " + key);
                    }
                }
            };
            visit(user, "");
        }

        effective_ = std::move(effective);
        return true;
    }
}
