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
        double           step{ 1 };       // settings page +/- step
        int              decimals{ -1 };  // settings page: fixed decimals, rounded on commit; -1 = as typed
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

        // Settings page edit: validates and stores one value in Effective().
        // Returns false (nothing changed) for unknown keys or invalid values.
        bool Set(std::string_view key, const Json& value);
        // Every value back to the shipped default.
        void ResetAll();

        // What SimpleTraits.user.json should contain: config_version plus
        // every value that differs from the shipped default.
        [[nodiscard]] Json Overrides() const;

        [[nodiscard]] const Json& Effective() const { return effective_; }
        // Built-in defaults overlaid with the valid shipped values.
        [[nodiscard]] const Json& Shipped() const { return shipped_; }
        [[nodiscard]] static std::span<const SettingDescriptor> Registry();
        [[nodiscard]] static const SettingDescriptor* Find(std::string_view key);
        [[nodiscard]] static bool Valid(const SettingDescriptor& descriptor, const Json& value);
        // The value at a dotted key, or nullptr.
        [[nodiscard]] static const Json* Value(const Json& root, std::string_view key);

    private:
        Json effective_ = BuiltInDefaults();
        Json shipped_ = BuiltInDefaults();

        [[nodiscard]] static Json BuiltInDefaults();
    };
}
