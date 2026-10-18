#!/usr/bin/env python3
"""Prepare an OpenRealm-compatible copy of Blizzard's s2client-proto schemas.

The script deliberately does not download anything. Pass a local checkout or
source export with s2clientprotocol/ and PROTOCOL_LICENSE. The copied
common.proto keeps Blizzard's Race values 0-4 and appends Warcraft III values
5-8; all other protocol files are copied unchanged.
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

RACE_BLOCK = (
    "enum Race {\n"
    "  NoRace = 0;\n"
    "  Terran = 1;\n"
    "  Zerg = 2;\n"
    "  Protoss = 3;\n"
    "  Random = 4;\n"
    "}\n"
)
RACE_EXTENSION = (
    "enum Race {\n"
    "  NoRace = 0;\n"
    "  Terran = 1;\n"
    "  Zerg = 2;\n"
    "  Protoss = 3;\n"
    "  Random = 4;\n"
    "  // OpenRealm Warcraft III extension. Keep Blizzard values 0-4 unchanged.\n"
    "  Human = 5;\n"
    "  Orc = 6;\n"
    "  Undead = 7;\n"
    "  NightElf = 8;\n"
    "}\n"
)


def prepare(source: Path, output: Path) -> None:
    protocol_source = source / "s2clientprotocol"
    common_source = protocol_source / "common.proto"
    if not common_source.is_file():
        raise SystemExit(f"missing {common_source}")

    protocol_output = output / "s2clientprotocol"
    if protocol_output.exists():
        shutil.rmtree(protocol_output)
    output.mkdir(parents=True, exist_ok=True)
    shutil.copytree(protocol_source, protocol_output)

    common_output = protocol_output / "common.proto"
    text = common_output.read_text(encoding="utf-8")
    if RACE_EXTENSION in text:
        pass
    elif RACE_BLOCK in text:
        if text.count(RACE_BLOCK) != 1:
            raise SystemExit("common.proto has more than one expected Race enum")
        text = text.replace(RACE_BLOCK, RACE_EXTENSION, 1)
        common_output.write_text(text, encoding="utf-8")
    else:
        raise SystemExit("common.proto Race enum no longer matches the expected Blizzard/OpenRealm schema")

    license_source = source / "PROTOCOL_LICENSE"
    if not license_source.is_file():
        raise SystemExit(f"missing {license_source}")
    shutil.copy2(license_source, output / "PROTOCOL_LICENSE")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path, help="local Blizzard s2client-proto checkout/export")
    parser.add_argument("--output", required=True, type=Path, help="generated protocol directory")
    args = parser.parse_args()
    prepare(args.source, args.output)


if __name__ == "__main__":
    main()
