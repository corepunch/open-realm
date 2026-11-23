#!/usr/bin/env python3
"""Generate redistributable Warcraft III integration maps.

    wc3fixturegen.py <dir>                 two-player transport map
    wc3fixturegen.py <dir> lobby|direct    five-player map launch fixture
"""

from pathlib import Path
import struct
import sys

TRANSPORT_SCRIPT = (
    "function config takes nothing returns nothing\nendfunction\n"
    "function main takes nothing returns nothing\n"
    "call FogMaskEnable(false)\ncall FogEnable(false)\n"
    "call CreateUnit(Player(0), 'hfoo', -768.0, 0.0, 0.0)\n"
    "call CreateUnit(Player(1), 'hfoo', 768.0, 0.0, 0.0)\nendfunction\n")

# Map launch fixture: W3I players 0-3 are User slots and player 4 is Neutral.
# config() repeats WorldEdit's InitCustomPlayerSlots for the User slots and
# deliberately disagrees with the W3I for player 4 (Neutral vs. Creep).
LAUNCH_CONFIG = (
    "function config takes nothing returns nothing\n"
    "call SetPlayerController(Player(0), MAP_CONTROL_USER)\n"
    "call SetPlayerController(Player(1), MAP_CONTROL_USER)\n"
    "call SetPlayerController(Player(2), MAP_CONTROL_USER)\n"
    "call SetPlayerController(Player(3), MAP_CONTROL_USER)\n"
    "call SetPlayerController(Player(4), MAP_CONTROL_CREEP)\n"
    "endfunction\n")

# What main() must see. The lobby launch uses slots Human (occupied),
# Computer, Closed and Open (unoccupied) for players 0-3 and offers no slot
# for the Neutral player 4; the direct launch has no lobby at all.
LAUNCH_EXPECT = {
    "lobby": (("USER", "PLAYING"), ("COMPUTER", "PLAYING"), ("NONE", "EMPTY"),
              ("NONE", "EMPTY"), ("CREEP", "EMPTY")),
    "direct": (("USER", "PLAYING"), ("USER", "PLAYING"), ("USER", "PLAYING"),
               ("USER", "PLAYING"), ("CREEP", "EMPTY")),
}


def launch_script(kind):
    script = LAUNCH_CONFIG + "function main takes nothing returns nothing\n"
    for number, (control, slot) in enumerate(LAUNCH_EXPECT[kind]):
        script += (f"call BJassAssert(GetPlayerController(Player({number})) == MAP_CONTROL_{control}, "
                   f"\"{kind} main controller {number}\")\n"
                   f"call BJassAssert(GetPlayerSlotState(Player({number})) == PLAYER_SLOT_STATE_{slot}, "
                   f"\"{kind} main slot state {number}\")\n")
    return script + "call MeleeStartingAI()\nendfunction\n"


def generate(directory, name, players, script):
    directory.mkdir(parents=True, exist_ok=True)
    pack = lambda fmt, *values: struct.pack("<" + fmt, *values)
    string = lambda value: value.encode() + b"\0"
    # ROC W3I v18, 32x32 tiles, one team holding every player.
    info = pack("3I", 18, 1, 6060)
    for value in (name, "OpenRealm", "Generated test map", str(len(players))):
        info += string(value)
    info += pack("8f4i2IIcI", -1536, -1536, -1536, 1536, 1536, 1536, 1536, -1536,
                 4, 4, 4, 4, 24, 24, 0x40, b"L", 0xFFFFFFFF)
    info += b"\0" * 3 + pack("I", 0) + b"\0" * 3 + pack("I", len(players))
    for number, player_type in enumerate(players):
        info += pack("4I", number, player_type, 1, 1) + string(f"Player {number + 1}")
        info += pack("2f2I", -256 + number * 512, 0, 0, 0)
    mask = (1 << len(players)) - 1
    info += pack("3I", 1, 3, mask) + string("Team 1") + pack("4I", 0, 0, 0, 0)
    (directory / "war3map.w3i").write_bytes(info)
    terrain = b"W3E!" + pack("IcI", 11, b"L", 0)
    terrain += pack("I", 1) + b"Ldrt" + pack("I", 0)
    terrain += pack("2I2f", 33, 33, -2048, -2048)
    terrain += pack("2H3B", 0x2000, 0x2000, 0, 0, 2) * (33 * 33)
    (directory / "war3map.w3e").write_bytes(terrain)
    for file_name in ("war3map.doo", "war3mapUnits.doo"):
        data = b"W3do" + pack("3I", 8, 11, 0)
        if file_name == "war3map.doo":
            data += pack("2I", 0, 0)
        (directory / file_name).write_bytes(data)
    (directory / "war3map.wpm").write_bytes(b"MP3W" + pack("3I", 0, 128, 128) + bytes(128 * 128))
    (directory / "war3map.j").write_text(script)


if __name__ == "__main__":
    target = Path(sys.argv[1])
    kind = sys.argv[2] if len(sys.argv) > 2 else "transport"
    if kind == "transport":
        generate(target, "Two-player transport fixture", (1, 1), TRANSPORT_SCRIPT)
    elif kind in LAUNCH_EXPECT:
        # W3I player types: 1 human (User slot), 3 neutral.
        generate(target, f"Map launch fixture ({kind})", (1, 1, 1, 1, 3), launch_script(kind))
    else:
        sys.exit(f"unknown fixture {kind}")
