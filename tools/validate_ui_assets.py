#!/usr/bin/env python3
"""Dependency-free validation of the native/SWF/translation contract of the trait menu."""

from __future__ import annotations

import argparse
import re
from pathlib import Path
import struct
import zlib


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--actionscript", required=True, type=Path)
    parser.add_argument("--translation", required=True, type=Path)
    parser.add_argument("--swf", required=True, type=Path)
    return parser.parse_args()


def require(text: str, token: str, source: Path) -> None:
    if token not in text:
        raise RuntimeError(f"{source} is missing required UI contract token: {token}")


def root_tags(path: Path):
    data = path.read_bytes()
    if data[:3] == b"CWS":
        data = data[:8] + zlib.decompress(data[8:])
    elif data[:3] != b"FWS":
        raise RuntimeError(f"Unsupported SWF format: {path}")
    if len(data) < 9:
        raise RuntimeError(f"Truncated SWF: {path}")

    rect_bits = data[8] >> 3
    offset = 8 + (5 + 4 * rect_bits + 7) // 8 + 4  # RECT, frame rate, frame count
    while offset + 2 <= len(data):
        header = struct.unpack_from("<H", data, offset)[0]
        offset += 2
        tag = header >> 6
        length = header & 0x3F
        if length == 0x3F:
            if offset + 4 > len(data):
                break
            length = struct.unpack_from("<I", data, offset)[0]
            offset += 4
        if offset + length > len(data):
            break
        yield tag, data[offset:offset + length]
        offset += length
        if tag == 0:
            return
    raise RuntimeError(f"Truncated SWF tag stream: {path}")


def main() -> int:
    args = parse_args()
    native = args.native.read_text(encoding="utf-8")
    actionscript = args.actionscript.read_text(encoding="utf-8")
    raw_translation = args.translation.read_bytes()
    if not raw_translation.startswith(b"\xff\xfe"):
        raise RuntimeError("Translation must be UTF-16 LE with a BOM.")
    translation = raw_translation[2:].decode("utf-16-le")
    translated = {line.split("\t", 1)[0] for line in translation.splitlines() if "\t" in line}

    # Plugin <-> menu function names must match on both sides.
    for token in ("ST_Init", "ST_UpdateTrait", "ST_SetClosing",
                  "ST_OnAllocate", "ST_OnDeallocate", "ST_OnConfirm", "ST_OnReset"):
        require(native, token, args.native)
        require(actionscript, token, args.actionscript)
    require(native, "std::array<RE::GFxValue, 3> args{ traits,", args.native)
    require(native, "std::array<RE::GFxValue, 4> args{", args.native)
    require(actionscript, "function ST_Init(traits, totalPoints, info)", args.actionscript)
    require(actionscript, "function ST_UpdateTrait(id, points, delta, remaining)", args.actionscript)

    # Every translation key the plugin reads must exist in the file.
    keys = set(re.findall(r'"(\$ST_[A-Z_]+)"', native))
    keys |= {f"$ST_TRAIT_{name}" for name in re.findall(r'"([A-Z]+)"', native.split("kTraitTokens", 1)[1].split("};", 1)[0])}
    keys.discard("$ST_TRAIT_")
    missing = sorted(key for key in keys if key not in translated)
    if missing:
        raise RuntimeError(f"Translation keys missing: {missing}")
    if len(keys) < 15:
        raise RuntimeError(f"Expected at least 15 translation keys in {args.native}, found {sorted(keys)}")

    tags = list(root_tags(args.swf))
    if any(tag in (4, 26, 70) for tag, _ in tags):
        raise RuntimeError("Trait menu SWF has a static main-stage object; the menu must be drawn by ST_Init only.")
    if not any(tag == 12 and b"ST_Init" in payload for tag, payload in tags):
        raise RuntimeError("Trait menu SWF is missing the ST_Init frame script.")
    if any(tag == 12 and b"ExternalInterface" in payload for tag, payload in tags):
        raise RuntimeError("Trait menu SWF contains the browser preview harness; rebuild from assets/swf_src.")
    print("Trait menu UI contract is consistent.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
