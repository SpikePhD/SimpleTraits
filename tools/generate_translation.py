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
    "$ST_EFFECT_STRENGTH": "+{value} Stamina per point",
    "$ST_EFFECT_RESILIENCE": "+{value} Health per point",
    "$ST_EFFECT_WISDOM": "+{value} Magicka per point",
}


def main() -> None:
    lines = [f"{key}\t{value}" for key, value in TEXT.items()]
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(b"\xff\xfe" + ("\r\n".join(lines) + "\r\n").encode("utf-16-le"))


if __name__ == "__main__":
    main()
