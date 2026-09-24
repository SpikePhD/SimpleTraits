#include "SettingsModel.h"
#include "TraitRules.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <iostream>

using ST::SettingsModel;
using Json = SettingsModel::Json;

namespace {
    bool HasWarning(const std::vector<std::string>& warnings, std::string_view text)
    {
        for (const auto& warning : warnings)
            if (warning.find(text) != std::string::npos) return true;
        return false;
    }
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ifstream input(argv[1]);
    const Json shipped = Json::parse(input);

    // The shipped file is complete and valid, and matches the built-in defaults.
    SettingsModel model;
    std::vector<std::string> warnings;
    assert(model.Load(shipped, Json(), &warnings));
    assert(warnings.empty());
    SettingsModel builtIn;
    assert(builtIn.Load(Json::object(), Json()));
    assert(model.Effective() == builtIn.Effective());
    for (const auto& descriptor : SettingsModel::Registry()) {
        std::string path = "/" + std::string(descriptor.key);
        for (auto& c : path) if (c == '.') c = '/';
        assert(shipped.contains(Json::json_pointer(path)));
    }

    // Built-in defaults also match TraitRules::TraitSettings.
    const ST::TraitRules::TraitSettings defaults{};
    const auto& e = model.Effective();
    assert(e["points"]["starting_points"] == defaults.points.startingPoints);
    assert(e["points"]["levels_per_point"] == defaults.points.levelsPerPoint);
    assert(e["per_point"]["stamina"].get<float>() == defaults.staminaPerPoint);
    assert(e["per_point"]["health"].get<float>() == defaults.healthPerPoint);
    assert(e["per_point"]["magicka"].get<float>() == defaults.magickaPerPoint);
    assert(e["per_point"]["critical_chance"].get<float>() == defaults.criticalChancePerPoint);
    assert(e["per_point"]["intelligence_threshold_reduction"].get<float>() == defaults.intelligenceThresholdReduction);
    assert(e["per_point"]["charisma_price_improvement"].get<float>() == defaults.charismaPriceImprovement);
    assert(e["config_version"] == SettingsModel::kSchemaVersion);

    // Valid overrides apply; each invalid value is ignored on its own.
    warnings.clear();
    const Json user = {
        { "config_version", 1 },
        { "points", { { "starting_points", 7 }, { "levels_per_point", 0 } } },
        { "per_point", { { "stamina", 12.5 }, { "health", "bad" }, { "critical_chance", 11.0 },
                         { "intelligence_threshold_reduction", 0.03 } } },
        { "typo_section", { { "x", 1 } } },
        { "_comment", "ignored" }
    };
    assert(model.Load(shipped, user, &warnings));
    assert(model.Effective()["points"]["starting_points"] == 7);
    assert(model.Effective()["points"]["levels_per_point"] == shipped["points"]["levels_per_point"]);
    assert(model.Effective()["per_point"]["stamina"] == 12.5);
    assert(model.Effective()["per_point"]["health"] == shipped["per_point"]["health"]);
    assert(model.Effective()["per_point"]["critical_chance"] == shipped["per_point"]["critical_chance"]);
    assert(model.Effective()["per_point"]["intelligence_threshold_reduction"] == 0.03);
    assert(HasWarning(warnings, "points.levels_per_point"));
    assert(HasWarning(warnings, "per_point.health"));
    assert(HasWarning(warnings, "per_point.critical_chance"));
    assert(HasWarning(warnings, "unknown override ignored: typo_section.x"));
    assert(!HasWarning(warnings, "_comment"));
    assert(warnings.size() == 4);

    // Integers: integral doubles are accepted and stored as integers.
    assert(model.Load(shipped, { { "points", { { "starting_points", 6.0 } } } }));
    assert(model.Effective()["points"]["starting_points"].is_number_integer());
    assert(model.Effective()["points"]["starting_points"] == 6);
    warnings.clear();
    assert(model.Load(shipped, { { "points", { { "starting_points", 6.5 } } } }, &warnings));
    assert(model.Effective()["points"]["starting_points"] == 4);
    assert(warnings.size() == 1);

    // Bounds are inclusive; the Intelligence reduction may not exceed 0.5.
    assert(model.Load(shipped, { { "per_point", { { "intelligence_threshold_reduction", 0.5 },
                                                  { "charisma_price_improvement", 0.0 } } } }));
    assert(model.Effective()["per_point"]["intelligence_threshold_reduction"] == 0.5);
    assert(model.Effective()["per_point"]["charisma_price_improvement"] == 0.0);
    assert(model.Load(shipped, { { "per_point", { { "intelligence_threshold_reduction", 0.51 } } } }));
    assert(model.Effective()["per_point"]["intelligence_threshold_reduction"] == 0.02);

    // Schema version: future or malformed versions ignore the whole user file;
    // a missing version is treated as version 1.
    warnings.clear();
    assert(model.Load(shipped, { { "config_version", 2 }, { "points", { { "starting_points", 9 } } } }, &warnings));
    assert(model.Effective()["points"]["starting_points"] == 4);
    assert(HasWarning(warnings, "newer"));
    warnings.clear();
    assert(model.Load(shipped, { { "config_version", "1" }, { "points", { { "starting_points", 9 } } } }, &warnings));
    assert(model.Effective()["points"]["starting_points"] == 4);
    assert(HasWarning(warnings, "config_version"));
    assert(model.Load(shipped, { { "points", { { "starting_points", 9 } } } }));
    assert(model.Effective()["points"]["starting_points"] == 9);

    // Invalid shipped values fall back to built-in defaults with a warning.
    Json damaged = shipped;
    damaged["per_point"]["magicka"] = -1.0;
    damaged["debug"]["verbose"] = "yes";
    warnings.clear();
    assert(model.Load(damaged, Json(), &warnings));
    assert(model.Effective()["per_point"]["magicka"] == 5.0);
    assert(model.Effective()["debug"]["verbose"] == false);
    assert(warnings.size() == 2);

    // Non-object inputs.
    assert(!model.Load(Json::array(), Json()));
    warnings.clear();
    assert(model.Load(shipped, Json::array(), &warnings));
    assert(HasWarning(warnings, "not a JSON object"));

    std::cout << "settings model tests passed\n";
}
