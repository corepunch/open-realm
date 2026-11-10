#!/usr/bin/env python3
"""Generate a retail-compatible Warcraft III map containing every retail item."""

import argparse
import csv
import re
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FOCUSED_ITEM_RAWCODES = ("rreb", "rspl", "lhst", "shdt", "clfm", "pnvu", "skrt", "wneu")


def run(args, *, cwd=None, text=True):
    return subprocess.run(args, cwd=cwd, check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=text).stdout


def slk(path, data_dir, mpqtool):
    raw = run([str(mpqtool), "-mpq", str(data_dir / "War3x.mpq"), "cat", path])
    cells = {}
    x = y = 1
    for line in raw.splitlines():
        if not line.startswith("C;"):
            continue
        mx = re.search(r"(?:^|;)X(\d+)", line)
        my = re.search(r"(?:^|;)Y(\d+)", line)
        if mx:
            x = int(mx.group(1))
        if my:
            y = int(my.group(1))
        mk = re.search(r"(?:^|;)K(.*)$", line)
        if not mk:
            continue
        value = mk.group(1)
        if value.startswith('"') and value.endswith('"'):
            value = value[1:-1]
        cells.setdefault(y, {})[x] = value
    headers = cells.get(1, {})
    return {
        row[1]: {headers.get(col, f"col{col}"): val for col, val in row.items()}
        for row in cells.values() if row.get(1) and row is not headers
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", type=Path, default=ROOT / "data/Warcraft III",
                        help="retail Warcraft III data directory (default: %(default)s)")
    parser.add_argument("--source-map", type=Path,
                        default=Path("Maps/FrozenThrone/(4)LostTemple.w3x"),
                        help="source melee map, relative to --data-dir")
    parser.add_argument("--output-dir", type=Path, default=None,
                        help="output directory (default: <data-dir>/Maps/FrozenThrone/Scenario)")
    parser.add_argument("--mpqtool", type=Path, default=ROOT / "build/bin/mpqtool",
                        help="path to repository mpqtool executable")
    parser.add_argument("--smpq", default="smpq", help="path to SMPQ command-line tool")
    args = parser.parse_args()

    data_dir = args.data_dir.resolve()
    source = args.source_map if args.source_map.is_absolute() else data_dir / args.source_map
    source = source.resolve()
    output_dir = args.output_dir or data_dir / "Maps/FrozenThrone/Scenario"
    output_dir = output_dir.resolve()
    mpqtool = args.mpqtool.resolve()
    output = output_dir / "Item Pickup Sandbox.w3x"
    if not (data_dir / "War3x.mpq").is_file():
        raise SystemExit(f"retail TFT archive not found: {data_dir / 'War3x.mpq'}")
    if not source.is_file():
        raise SystemExit(f"source map not found: {source}")
    if not mpqtool.is_file():
        raise SystemExit(f"mpqtool not found: {mpqtool}; build it with make build/bin/mpqtool")
    output_dir.mkdir(parents=True, exist_ok=True)

    item_data = slk("Units/ItemData.slk", data_dir, mpqtool)
    roster = []
    for rawcode, data in item_data.items():
        if len(rawcode) != 4 or not rawcode.isascii():
            continue
        roster.append({
            "rawcode": rawcode,
            "name": data.get("comment", rawcode),
            "class": data.get("class", ""),
            "powerup": data.get("powerup", "0"),
            "ability_aliases": data.get("abilList", ""),
        })
    roster.sort(key=lambda row: (row["class"].casefold(), row["name"].casefold(), row["rawcode"]))
    if not roster:
        raise SystemExit("retail ItemData.slk has no four-character item rawcodes")

    pathing = run([str(mpqtool), "-mpq", str(source), "cat", "war3map.wpm"], text=False)
    if len(pathing) < 16 or pathing[:4] != b"MP3W":
        raise SystemExit("source map has no valid WPM pathing map")
    path_width, path_height = struct.unpack_from("<II", pathing, 8)
    path_cells = pathing[16:16 + path_width * path_height]
    if len(path_cells) != path_width * path_height:
        raise SystemExit("source map pathing map is truncated")

    terrain = run([str(mpqtool), "-mpq", str(source), "cat", "war3map.w3e"], text=False)
    offset = 8 + 1 + 4
    ground_tiles = struct.unpack_from("<I", terrain, offset)[0]
    offset += 4 + ground_tiles * 4
    cliff_tiles = struct.unpack_from("<I", terrain, offset)[0]
    offset += 4 + cliff_tiles * 4
    terrain_width, terrain_height, terrain_x, terrain_y = struct.unpack_from("<IIff", terrain, offset)
    offset += 16

    def dry(px, py):
        vx, vy = round(px / 4.0), round(py / 4.0)
        for y in range(max(0, vy - 2), min(terrain_height, vy + 3)):
            for x in range(max(0, vx - 2), min(terrain_width, vx + 3)):
                if terrain[offset + (y * terrain_width + x) * 7 + 4] & 0x40:
                    return False
        return True

    # Candidate locations sit in small, fully walkable, dry patches. Greedy
    # farthest-point placement spreads hundreds of pickups over the whole map.
    window = 4
    integral = [[0] * (path_width + 1) for _ in range(path_height + 1)]
    for y in range(path_height):
        row_blocked = 0
        for x in range(path_width):
            row_blocked += bool(path_cells[y * path_width + x] & 0x02)
            integral[y + 1][x + 1] = integral[y][x + 1] + row_blocked

    def blocked(left, top):
        right, bottom = left + window, top + window
        return (integral[bottom][right] - integral[top][right]
                - integral[bottom][left] + integral[top][left])

    candidates = []
    for top in range(8, path_height - window - 8, 3):
        for left in range(8, path_width - window - 8, 3):
            px, py = left + window // 2, top + window // 2
            if blocked(left, top) == 0 and dry(px, py):
                candidates.append((px, py))
    if len(candidates) < len(roster):
        raise SystemExit(f"not enough clear dry map locations ({len(candidates)} for {len(roster)} items)")

    # Seed from a coarse map-wide grid, then maximize distance from already
    # chosen points. Each iteration updates one distance per candidate.
    selected = []
    min_distance = [float("inf")] * len(candidates)
    while len(selected) < len(roster):
        if not selected:
            target_x, target_y = path_width / 2, path_height / 2
            index = min(range(len(candidates)), key=lambda i:
                        (candidates[i][0] - target_x) ** 2 + (candidates[i][1] - target_y) ** 2)
        else:
            index = max(range(len(candidates)), key=lambda i: min_distance[i])
        point = candidates[index]
        selected.append(point)
        for i, candidate in enumerate(candidates):
            dx, dy = candidate[0] - point[0], candidate[1] - point[1]
            min_distance[i] = min(min_distance[i], dx * dx + dy * dy)
        min_distance[index] = -1

    positions = [(terrain_x + x * 32.0, terrain_y + y * 32.0) for x, y in selected]
    jass = ["function ItemSandboxSpawn takes nothing returns nothing"]
    for row, (x, y) in zip(roster, positions):
        jass.append(f"    call CreateItem('{row['rawcode']}', {x:.1f}, {y:.1f})")
    focused = [row for rawcode in FOCUSED_ITEM_RAWCODES
               for row in roster if row["rawcode"] == rawcode]
    if len(focused) != len(FOCUSED_ITEM_RAWCODES):
        missing = sorted(set(FOCUSED_ITEM_RAWCODES) - {row["rawcode"] for row in focused})
        raise SystemExit(f"retail ItemData.slk is missing focused item rawcodes: {', '.join(missing)}")
    # Keep convenient copies in a compact two-row tray next to the Paladin.
    # The wide roster above remains intact for the all-items map.
    jass.append("    // Focused regression items, two rows beside the Player 1 Hero.")
    focused_offsets = [(-192.0, -128.0), (-64.0, -128.0), (64.0, -128.0), (192.0, -128.0),
                       (-192.0, 128.0), (-64.0, 128.0), (64.0, 128.0), (192.0, 128.0)]
    for row, (dx, dy) in zip(focused, focused_offsets):
        jass.append(
            f"    call CreateItem('{row['rawcode']}', "
            f"GetStartLocationX(GetPlayerStartLocation(Player(0))) + 128.0 + ({dx:.1f}), "
            f"GetStartLocationY(GetPlayerStartLocation(Player(0))) + ({dy:.1f}))")
    jass += [
        "    call CreateUnit(Player(0), 'Hpal', GetStartLocationX(GetPlayerStartLocation(Player(0))) + 128.0, GetStartLocationY(GetPlayerStartLocation(Player(0))), 270.0)",
        "    call SetCameraPosition(GetStartLocationX(GetPlayerStartLocation(Player(0))), GetStartLocationY(GetPlayerStartLocation(Player(0))))",
        "endfunction", "",
    ]
    script = run([str(mpqtool), "-mpq", str(source), "cat", "war3map.j"])
    if "function ItemSandboxSpawn" in script:
        raise SystemExit("source map unexpectedly contains item sandbox injection")
    match = re.search(r"(?m)^function main takes nothing returns nothing$", script)
    if not match:
        raise SystemExit("source map has no main function")
    script = script[:match.start()] + "\n".join(jass) + script[match.start():]
    main_end = re.search(r"(?m)^    call RunInitializationTriggers\(  \)$", script)
    if not main_end:
        raise SystemExit("source map main function has no initialization-trigger call")
    end = main_end.end()
    script = script[:end] + "\n    call ItemSandboxSpawn()" + script[end:]

    work_temp = tempfile.TemporaryDirectory(prefix="wc3-item-sandbox-", dir=output_dir)
    work = Path(work_temp.name)
    (work / "war3map.j").write_text(script, encoding="utf-8")
    wts = run([str(mpqtool), "-mpq", str(source), "cat", "war3map.wts"])

    def replace_wts(text, number, value):
        pattern = rf"(?ms)^STRING {number}\s*\{{.*?^\}}"
        replacement = f"STRING {number}\n{{\n{value}\n}}"
        if re.search(pattern, text):
            return re.sub(pattern, replacement, text, count=1)
        return text.rstrip() + f"\n\n{replacement}\n"

    wts = replace_wts(wts, 3, "Item Pickup Sandbox")
    wts = replace_wts(wts, 5, f"{len(roster)} retail items from ItemData.slk, including power-ups and runes. Owned by Player 1.")
    (work / "war3map.wts").write_text(wts, encoding="utf-8")

    output.write_bytes(source.read_bytes())
    run([args.smpq, "-a", "-f", str(output), "war3map.j", "war3map.wts"], cwd=work)
    roster_path = output_dir / "Item Pickup Sandbox roster.csv"
    with roster_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["rawcode", "name", "class", "powerup", "ability_aliases", "placement", "x", "y"])
        writer.writeheader()
        for row, (x, y) in zip(roster, positions):
            writer.writerow({**row, "placement": "map-wide", "x": f"{x:.1f}", "y": f"{y:.1f}"})
        for row, (dx, dy) in zip(focused, focused_offsets):
            start_x_offset = 128.0 + dx
            writer.writerow({**row, "placement": "near-hero copy",
                             "x": f"Player1StartX{start_x_offset:+.1f}",
                             "y": f"Player1StartY{dy:+.1f}"})
    print(f"Wrote {output} with {len(roster)} retail item types.")
    print(f"Roster: {roster_path}")


if __name__ == "__main__":
    main()
