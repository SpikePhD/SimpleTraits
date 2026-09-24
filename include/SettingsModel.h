#pragma once

#include <nlohmann/json.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ST {
    enum class SettingKind { Number, Integer, Toggle };

    struct SettingDescriptor {
        std::string_view key;  // dotted JSON path, e.g. "per_point.stamina"
        SettingKind      kind;
        double           minimum;
        double           maximum;
        double           builtInDefault;  // Toggle: 0 or 1
    };

    // Layers SimpleTraits.json (shipped defaults) and SimpleTraits.user.json
    // (overrides) over a fixed registry of validated settings. Every value is
    // checked on its own, so one damaged setting cannot poison the others:
    // - a missing shipped value uses the built-in default silently;
    // - an invalid shipped value uses the built-in default with a warning;
    // - an invalid or unknown override is ignored with a warning;
    // - a user file with a malformed or future config_version is ignored.
    class SettingsModel {
    public:
        using Json = nlohmann::json;
        static constexpr int kSchemaVersion = 1;

        // Returns false (and keeps the previous state) only when `shipped`
        // is not a JSON object.
        bool Load(const Json& shipped, const Json& user, std::vector<std::string>* warnings = nullptr);

        [[nodiscard]] const Json& Effective() const { return effective_; }
        [[nodiscard]] static std::span<const SettingDescriptor> Registry();
        [[nodiscard]] static bool Valid(const SettingDescriptor& descriptor, const Json& value);

    private:
        Json effective_ = BuiltInDefaults();

        [[nodiscard]] static Json BuiltInDefaults();
    };
}
