#!/usr/bin/env python3
"""Generate a redistributable two-player Warcraft III integration map."""

from pathlib import Path
import struct
import sys


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    pack = lambda fmt, *values: struct.pack("<" + fmt, *values)
    string = lambda value: value.encode() + b"\0"
    # ROC W3I v18, 32x32 tiles, two human players and one shared team.
    info = pack("3I", 18, 1, 6060)
    for value in ("Two-player transport fixture", "OpenRealm", "Generated test map", "2"):
        info += string(value)
    info += pack("8f4i2IIcI", -1536, -1536, -1536, 1536, 1536, 1536, 1536, -1536,
                 4, 4, 4, 4, 24, 24, 0x40, b"L", 0xFFFFFFFF)
    info += b"\0" * 3 + pack("I", 0) + b"\0" * 3 + pack("I", 2)
    for number in range(2):
        info += pack("4I", number, 1, 1, 1) + string(f"Player {number + 1}")
        info += pack("2f2I", -256 + number * 512, 0, 0, 0)
    info += pack("3I", 1, 3, 3) + string("Team 1") + pack("4I", 0, 0, 0, 0)
    (directory / "war3map.w3i").write_bytes(info)
    terrain = b"W3E!" + pack("IcI", 11, b"L", 0)
    terrain += pack("I", 1) + b"Ldrt" + pack("I", 0)
    terrain += pack("2I2f", 33, 33, -2048, -2048)
    terrain += pack("2H3B", 0x2000, 0x2000, 0, 0, 2) * (33 * 33)
    (directory / "war3map.w3e").write_bytes(terrain)
    for name in ("war3map.doo", "war3mapUnits.doo"):
        data = b"W3do" + pack("3I", 8, 11, 0)
        if name == "war3map.doo":
            data += pack("2I", 0, 0)
        (directory / name).write_bytes(data)
    (directory / "war3map.wpm").write_bytes(b"MP3W" + pack("3I", 0, 128, 128) + bytes(128 * 128))
    (directory / "war3map.j").write_text(
        "function config takes nothing returns nothing\nendfunction\n"
        "function main takes nothing returns nothing\n"
        "call FogMaskEnable(false)\ncall FogEnable(false)\n"
        "call CreateUnit(Player(0), 'hfoo', -768.0, 0.0, 0.0)\n"
        "call CreateUnit(Player(1), 'hfoo', 768.0, 0.0, 0.0)\nendfunction\n")


if __name__ == "__main__":
    generate(Path(sys.argv[1]))
