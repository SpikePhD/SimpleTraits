#!/usr/bin/env python3
"""Generate the English Simple Traits translation file (UTF-16 LE with BOM)."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "data/Interface/Translations/SimpleTraits_ENGLISH.txt"


# Trait allocation menu (ST_TraitMenu.swf). English fallbacks live in
# src/TraitMenu.cpp; keep both in sync. {value} is the per-point amount.
TEXT = {
    "$ST_MENU_TITLE": "Distribute Trait Points",
    "$ST_CONFIRM": "Confirm",
    "$ST_RESET": "Reset",
    "$ST_LEVEL": "Level",
    "$ST_REMAINING": "points remaining",
    "$ST_CARRY_NOTE": "Unspent points are kept for your next level.",
    "$ST_PERMANENT_NOTE": "Trait points are permanent once confirmed.",
    "$ST_EFFECT_INACTIVE": "No effect yet",
    "$ST_HINT": "Up/Down: select    Right, Enter or +: add    Left, Backspace or -: remove    R: reset    C or Esc: confirm",
    "$ST_TRAIT_STRENGTH": "Strength",
    "$ST_TRAIT_RESILIENCE": "Resilience",
    "$ST_TRAIT_AGILITY": "Agility",
    "$ST_TRAIT_INTELLIGENCE": "Intelligence",
    "$ST_TRAIT_WISDOM": "Wisdom",
    "$ST_TRAIT_CHARISMA": "Charisma",
    "$ST_EFFECT_STRENGTH": "+{value}% of base Stamina per point",
    "$ST_EFFECT_RESILIENCE": "+{value}% of base Health per point",
    "$ST_EFFECT_AGILITY": "+{value}% critical hit chance per point",
    "$ST_EFFECT_CHARISMA": "{value}% better buy and sell prices per point",
    "$ST_EFFECT_INTELLIGENCE": "+{value}% XP per point",
    "$ST_EFFECT_WISDOM": "+{value}% of base Magicka per point",
}


# Settings page (SKSE Menu Framework). English fallbacks in src/SettingsPage.cpp.
PAGE = {
    "$ST_PAGE_SETTINGS": "Settings",
    "$ST_PAGE_INTRO": "Changes are saved to SimpleTraits.user.json and take effect immediately. Hover a setting for its description and default.",
    "$ST_PAGE_DEFAULT": "Default",
    "$ST_PAGE_ON": "On",
    "$ST_PAGE_OFF": "Off",
    "$ST_PAGE_INVALID": "out of range; the previous value was kept.",
    "$ST_PAGE_SAVE_FAILED": "Could not save SimpleTraits.user.json; see the log.",
    "$ST_PAGE_RESET_ALL": "Reset all to defaults",
    "$ST_PAGE_CONFIRM": "Click again to confirm",
    "$ST_PAGE_SECTION_POINTS": "Trait points",
    "$ST_PAGE_SECTION_BONUSES": "Bonuses per point",
    "$ST_PAGE_SECTION_DEBUG": "Debug",
}

# One (label, description) per setting key in src/SettingsModel.cpp.
SETTINGS = {
    "points.starting_points": ("Starting points",
        "Trait points every character has before any level-up. They are spent at the first level-up. Lowering this never removes points already spent."),
    "points.levels_per_point": ("Levels per trait point",
        "One trait point is earned every this many levels."),
    "per_point.stamina_percent": ("Strength: Stamina per point (fraction of base)",
        "0.05 = +5% of base Stamina per point. Follows the base value, so the bonus grows when base Stamina grows."),
    "per_point.health_percent": ("Resilience: Health per point (fraction of base)",
        "0.05 = +5% of base Health per point. Follows the base value, so the bonus grows when base Health grows."),
    "per_point.magicka_percent": ("Wisdom: Magicka per point (fraction of base)",
        "0.05 = +5% of base Magicka per point. Follows the base value, so the bonus grows when base Magicka grows."),
    "per_point.critical_chance": ("Agility: critical chance per point (%)",
        "Critical hit chance per point, in percent, for an untempered weapon with a normal critical multiplier (tempering and some weapons raise it). Applied as the CriticalChance actor value."),
    "per_point.intelligence_xp_percent": ("Intelligence: XP per point (fraction)",
        "0.10 = +10% XP per point, so 10 points double every XP award. Applies to XP as it is earned; the XP needed per level is unchanged."),
    "per_point.charisma_price_improvement": ("Charisma: price improvement per point",
        "0.01 = buy about 1% cheaper and sell about 1% dearer per point. Selling never pays more than buying costs."),
    "debug.verbose": ("Verbose logging",
        "Writes detailed trace lines (every hit, every reconcile) to the Simple Traits log."),
    "debug.max_log_files": ("Log files to keep",
        "Session logs kept in the SKSE log folder (0 keeps all). Applies from the next start."),
    "debug.allocation_page": ("Debug allocation page",
        "Shows the temporary Simple Traits / Debug page for spending points. Applies from the next start."),
}


def setting_token(key: str) -> str:
    return key.replace(".", "_").upper()


def main() -> None:
    lines = [f"{key}\t{value}" for key, value in TEXT.items()]
    lines += [f"{key}\t{value}" for key, value in PAGE.items()]
    for key, (label, description) in SETTINGS.items():
        lines.append(f"$ST_SETTING_{setting_token(key)}\t{label}")
        lines.append(f"$ST_DESC_{setting_token(key)}\t{description}")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(b"\xff\xfe" + ("\r\n".join(lines) + "\r\n").encode("utf-16-le"))


if __name__ == "__main__":
    main()
