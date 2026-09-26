#!/usr/bin/env python3
"""Fail if the shipped configuration contains debug or test values."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


# The shipped JSON is packaged verbatim. Local test values belong in the
# deployed SimpleTraits.user.json instead.
EXPECTED = {
    ("config_version",): 1,
    ("debug", "verbose"): False,
    ("debug", "max_log_files"): 10,
    ("debug", "allocation_page"): False,
    ("points", "starting_points"): 4,
    ("points", "levels_per_point"): 3,
    ("per_point", "stamina_percent"): 0.05,
    ("per_point", "health_percent"): 0.05,
    ("per_point", "magicka_percent"): 0.05,
    ("per_point", "critical_chance"): 1.0,
    ("per_point", "intelligence_xp_percent"): 0.10,
    ("per_point", "charisma_price_improvement"): 0.01,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--defaults", required=True, type=Path)
    args = parser.parse_args()

    config = json.loads(args.defaults.read_text(encoding="utf-8"))
    errors = []
    for path, expected in EXPECTED.items():
        node = config
        for key in path:
            node = node.get(key) if isinstance(node, dict) else None
        if node != expected or type(node) is not type(expected):
            errors.append(f"{'.'.join(path)} is {node!r}; release default is {expected!r}")

    for error in errors:
        print(error)
    if errors:
        print("Shipped config contains non-release values; move test values to the user override file.")
        return 1
    print("Shipped config uses release defaults.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
