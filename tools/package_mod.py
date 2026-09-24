#!/usr/bin/env python3
"""Create the deterministic Simple Traits mod archive."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
from zipfile import ZIP_STORED, ZipFile, ZipInfo


FIXED_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
FILE_MODE = (0o100644 & 0xFFFF) << 16


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dll", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--swf", required=True, type=Path)
    parser.add_argument("--translation", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--runtime", required=True)
    return parser.parse_args()


def write_archive(output: Path, entries: dict[str, Path]) -> None:
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.unlink(missing_ok=True)
    with ZipFile(temporary, "w", compression=ZIP_STORED) as archive:
        for archive_name in sorted(entries):
            source = entries[archive_name]
            if not source.is_file():
                raise FileNotFoundError(f"Required package input is missing: {source}")
            info = ZipInfo(archive_name, date_time=FIXED_TIMESTAMP)
            info.compress_type = ZIP_STORED
            info.create_system = 3
            info.external_attr = FILE_MODE
            archive.writestr(info, source.read_bytes())
    os.replace(temporary, output)


def main() -> int:
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    output = args.output_dir / f"SimpleTraits-{args.version}-Skyrim{args.runtime}.zip"
    entries = {
        "Interface/ST_TraitMenu.swf": args.swf,
        "Interface/Translations/SimpleTraits_ENGLISH.txt": args.translation,
        "SKSE/Plugins/SimpleTraits.dll": args.dll,
        "SKSE/Plugins/SimpleTraits.json": args.config,
    }
    write_archive(output, entries)

    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    checksum = output.with_suffix(output.suffix + ".sha256")
    checksum.write_text(f"{digest}  {output.name}\n", encoding="ascii", newline="\n")
    print(output)
    print(checksum)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
