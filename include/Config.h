#pragma once

#include "TraitRules.h"

#include <string>
#include <vector>

namespace ST::Config {

    // Validated effective values. Written only by Load() (main thread, during
    // plugin load); defaults match the shipped SimpleTraits.json.
    inline bool                      verbose = false;
    inline int                       maxLogFiles = 10;
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
}
