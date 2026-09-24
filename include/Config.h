#pragma once

#include "TraitRules.h"

#include <string>
#include <vector>

namespace ST { class SettingsModel; }

namespace ST::Config {

    // Validated effective values. Written only by Load() (main thread, during
    // plugin load); defaults match the shipped SimpleTraits.json.
    inline bool                      verbose = false;
    inline int                       maxLogFiles = 10;
    inline bool                      allocationPage = false;
    inline TraitRules::TraitSettings traits{};

    struct LoadReport {
        std::string              defaultsPath;
        std::string              userPath;
        bool                     userFilePresent{ false };
        std::vector<std::string> errors;    // whole file unusable
        std::vector<std::string> warnings;  // individual values ignored
    };

    // Reads and validates both files without logging, so it can run before
    // the logger exists (the logger needs debug.verbose/max_log_files).
    [[nodiscard]] LoadReport Load();

    // Logs the report and the effective values. Call after InitializeLog().
    void LogReport(const LoadReport& report);
    void LogValues();

    // The layered settings model, edited by the settings page. Callers
    // serialize access (the settings page holds its own mutex).
    [[nodiscard]] SettingsModel& Settings();

    // Main thread. Writes SimpleTraits.user.json atomically (only values
    // that differ from the shipped defaults) and refreshes the globals above.
    [[nodiscard]] bool SaveAndApply(std::string& error);
}
