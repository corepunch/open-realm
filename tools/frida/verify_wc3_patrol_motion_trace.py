#!/usr/bin/env python3
"""Verify retail Patrol leg motion against paired Move legs and render the engine fixture.

The patrol_motion probe (wc3_patrol_motion_probe.j) samples ten Footmen every 0.1s:
paired Move/Patrol subjects on open, walled and enclosed-endpoint lanes plus head-on
pairs. Retail runs each Patrol leg as an ordinary point task, so Patrol and Move
subjects on identical geometry share their trajectories until the probe's idle
Move reissue (one sample late) separates them.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
HASH = "d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"
FIXTURE = ROOT / "tools/ghidra/fixtures/retail-patrol-motion-1.27.json"
HEADER = ROOT / "games/warcraft-3/game/tests/retail_patrol_motion.h"
PROBE = ROOT / "tools/frida/wc3_patrol_motion_probe.j"
UNITS, TICKS = 10, 300
PATROL = (False, True, False, True, True, True, False, False, True, False)
# (Move subject, Patrol subject, Patrol offset from Move, last tick of the shared journey)
PAIRS = ((0, 1, (0, 192), 58), (2, 3, (0, 448), 16), (9, 8, (-256, 0), 33))


def f32(word):
    return struct.unpack("<f", struct.pack("<I", word))[0]


def read_rows(path):
    opener = gzip.open if str(path).endswith(".gz") else open
    with opener(path, "rt") as handle:
        return [json.loads(line) for line in handle]


def decode(rows):
    """Return samples[unit][tick-1] = [order, facing, x, y] as raw 32-bit words."""
    meta = rows[0]
    if meta.get("event") != "metadata" or meta.get("sha256") != HASH:
        raise ValueError("capture is not game.dll 1.27.1.7085")
    exported = [r for r in rows if r.get("event") == "metadata-row"]
    cells = {}
    for r in exported:
        word = r["word"]
        if type(word) is not int or not 0 <= word <= 0xffffffff:
            raise ValueError("export word out of range")
        cells.setdefault(r["parent"], {})[r["child"]] = (r["kind"], word)
    kinds = ["integer"] * 3 + ["real"] * 3 + ["integer"]
    labels = {}
    for r in rows:
        if r.get("event") != "metadata-marker" or " row=" not in r.get("value", ""):
            continue
        m = re.fullmatch(r"PATHMETA patrol_motion row=(\d+) tick=(\d+) case=(\d+) label=(\w+)", r["value"])
        if not m:
            raise ValueError("unexpected patrol_motion marker")
        labels[int(m[1])] = (int(m[2]), int(m[3]), m[4])
    if sorted(labels) != list(range(len(labels))) or sorted(cells) != sorted(labels):
        raise ValueError("incomplete patrol_motion matrix")
    samples = [[None] * TICKS for _ in range(UNITS)]
    for row, (tick, unit, label) in labels.items():
        row_cells = cells[row]
        if sorted(row_cells) != list(range(7)) or any(row_cells[c][0] != kinds[c] for c in range(7)):
            raise ValueError("patrol_motion export words/types differ")
        words = [row_cells[c][1] for c in range(7)]
        if words[0] != tick or words[1] != unit:
            raise ValueError("patrol_motion label/order differs")
        if label == "sample":
            if not 1 <= tick <= TICKS or samples[unit][tick - 1] is not None:
                raise ValueError("duplicate or out-of-range sample")
            samples[unit][tick - 1] = [words[2], words[3], words[4], words[5]]
    if any(s is None for unit in samples for s in unit):
        raise ValueError("incomplete patrol_motion journey")
    if not any(r.get("event") == "metadata-marker" and r.get("value") == "PATHMETA complete" for r in rows):
        raise ValueError("probe did not complete")
    return samples


def claims(samples):
    for unit in range(UNITS):
        order = 851991 if PATROL[unit] else 851986
        if any(s[0] != order for s in samples[unit]):
            raise ValueError(f"subject {unit} left its public head")
    for move, patrol, (dx, dy), last in PAIRS:
        for tick in range(last):
            a, b = samples[move][tick], samples[patrol][tick]
            if abs(f32(a[2]) + dx - f32(b[2])) > 1e-3 or abs(f32(a[3]) + dy - f32(b[3])) > 1e-3:
                raise ValueError(f"Patrol {patrol} diverged from Move {move} at tick {tick + 1}")
        nxt = samples[move][last]
        nb = samples[patrol][last]
        if abs(f32(nxt[2]) + dx - f32(nb[2])) <= 1e-3 and abs(f32(nxt[3]) + dy - f32(nb[3])) <= 1e-3 and move != 2:
            raise ValueError(f"shared journey window for {patrol} is not tight")
    enclosed = [f32(s[3]) for s in samples[8]]
    peak = max(range(40), key=lambda t: enclosed[t])
    if enclosed[peak] < 1080 or min(enclosed[peak:peak + 40]) > 400:
        raise ValueError("enclosed Patrol endpoint did not reverse at its closest reachable cell")
    return {"pairs": [list(p[:2]) + [p[3]] for p in PAIRS], "enclosed_peak": [peak + 1, round(enclosed[peak], 3)]}


def render_header(fixture):
    lines = ["/* Original patrol_motion public samples (tools/frida/verify_wc3_patrol_motion_trace.py).",
             " * [unit][tick-1] = {current order, GetUnitFacing, GetUnitX, GetUnitY} raw words. */",
             f"static uint32_t const patrol_motion_retail[{UNITS}][{TICKS}][4] = {{"]
    for unit in fixture["samples"]:
        lines.append("    {")
        for i in range(0, TICKS, 4):
            lines.append("        " + ",".join("{" + ",".join(f"0x{w:08x}u" for w in s) + "}" for s in unit[i:i + 4]) + ",")
        lines.append("    },")
    lines.append("};")
    # The engine replays the captured probe itself, so its timer, orders and reissues match.
    lines.append("/* The captured probe (tools/frida/wc3_patrol_motion_probe.j). */")
    lines.append("static char const patrol_motion_probe[] =")
    lines.extend("    " + json.dumps(line + "\n") for line in PROBE.read_text().splitlines())
    lines.append("    ;")
    return "\n".join(lines) + "\n"


def verify_contract(fixture):
    if fixture.get("game_sha256") != HASH or len(fixture.get("captures", [])) != 2:
        raise ValueError("fixture requires two game.dll 1.27.1.7085 captures")
    samples = fixture["samples"]
    if len(samples) != UNITS or any(len(u) != TICKS or any(len(s) != 4 for s in u) for u in samples):
        raise ValueError("fixture sample matrix is incomplete")
    if fixture.get("probe_sha256") != hashlib.sha256(PROBE.read_bytes()).hexdigest():
        raise ValueError("probe source differs from the captured probe")
    if claims(samples) != fixture["claims"]:
        raise ValueError("claims differ from the frozen samples")
    return fixture["claims"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="*", help="two complete Frida captures; regenerates fixture and header")
    parser.add_argument("--check-engine-header", action="store_true")
    args = parser.parse_args()
    if args.captures:
        if len(args.captures) != 2:
            raise ValueError("pass exactly two captures")
        decoded = [decode(read_rows(p)) for p in args.captures]
        if decoded[0] != decoded[1]:
            raise ValueError("captures disagree")
        FIXTURE.write_text(json.dumps({
            "version": 1, "task": "PATROL-MOTION", "game_sha256": HASH,
            "probe": "tools/frida/wc3_patrol_motion_probe.j",
            "probe_sha256": hashlib.sha256(PROBE.read_bytes()).hexdigest(),
            "scope": "Patrol legs are ordinary point tasks: paired Move/Patrol public trajectories and "
                     "the enclosed-endpoint reversal; head-on avoidance and facing animation are not certified here.",
            "captures": [{"sha256": hashlib.sha256(Path(p).read_bytes()).hexdigest()} for p in args.captures],
            "claims": claims(decoded[0]),
            "samples": decoded[0],
        }, separators=(",", ":")) + "\n")
        HEADER.write_text(render_header(json.loads(FIXTURE.read_text())))
    fixture = json.loads(FIXTURE.read_text())
    result = verify_contract(fixture)
    if args.check_engine_header and HEADER.read_text() != render_header(fixture):
        raise ValueError("engine header differs from the frozen samples")
    print(json.dumps({"passed": True, "claims": result}))


if __name__ == "__main__":
    main()
