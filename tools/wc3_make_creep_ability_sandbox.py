#!/usr/bin/env python3
"""Generate a local Warcraft III creep ability sandbox map from retail data."""

import argparse
import csv
import re
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


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
        if mx: x = int(mx.group(1))
        if my: y = int(my.group(1))
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
                        help="path to the repository mpqtool executable")
    parser.add_argument("--smpq", default="smpq", help="path to the SMPQ command-line tool")
    args = parser.parse_args()

    data_dir = args.data_dir.resolve()
    source = args.source_map if args.source_map.is_absolute() else data_dir / args.source_map
    source = source.resolve()
    output_dir = args.output_dir or data_dir / "Maps/FrozenThrone/Scenario"
    output_dir = output_dir.resolve()
    mpqtool = args.mpqtool.resolve()
    output = output_dir / "Creep Ability Sandbox.w3x"
    if not (data_dir / "War3x.mpq").is_file():
        raise SystemExit(f"retail TFT archive not found: {data_dir / 'War3x.mpq'}")
    if not source.is_file():
        raise SystemExit(f"source map not found: {source}")
    if not mpqtool.is_file():
        raise SystemExit(f"mpqtool not found: {mpqtool}; build it with make build/bin/mpqtool")
    output_dir.mkdir(parents=True, exist_ok=True)

    unit_data = slk("Units/UnitData.slk", data_dir, mpqtool)
    unit_ui = slk("Units/UnitUI.slk", data_dir, mpqtool)
    unit_abilities = slk("Units/UnitAbilities.slk", data_dir, mpqtool)
    temporary_work = tempfile.TemporaryDirectory(prefix="wc3-creep-sandbox-", dir=output_dir)
    work = Path(temporary_work.name)
    roster = []
    for rawcode, data in unit_data.items():
        ui = unit_ui.get(rawcode, {})
        # Retail's UnitUI hostilePal flag identifies these as neutral-hostile
        # roster entries; UnitBalance race alone also includes neutral heroes,
        # campaign-only humans/orcs, demons and summoned forms.
        if not rawcode.startswith("n") or ui.get("hostilePal") != "1" or data.get("valid") != "1":
            continue
        abilities = unit_abilities.get(rawcode, {}).get("abilList", "")
        if not abilities or abilities in ("_", "-"):
            continue
        if len(rawcode) != 4:
            continue
        roster.append({
            "rawcode": rawcode,
            "name": ui.get("name", data.get("comment(s)", rawcode)),
            "unit_class": ui.get("unitClass", ""),
            "ability_aliases": abilities,
        })
    roster.sort(key=lambda row: (row["unit_class"].casefold(), row["name"].casefold(), row["rawcode"]))
    if not roster:
        raise SystemExit("retail creep roster is empty")

    # Divide the roster into small groups spread across Lost Temple. Select group
    # centers from low-blocking pathing regions so land units can move, then leave
    # 320 world units between adjacent creep positions within each 3x4 group.
    group_cols, group_rows = 3, 4
    group_size = group_cols * group_rows
    spacing = 320
    group_count = (len(roster) + group_size - 1) // group_size
    pathing = run([str(mpqtool), "-mpq", str(source), "cat", "war3map.wpm"], text=False)
    if len(pathing) < 16 or pathing[:4] != b"MP3W":
        raise SystemExit("source map has no valid WPM pathing map")
    path_width, path_height = struct.unpack_from("<II", pathing, 8)
    path_cells = pathing[16:16 + path_width * path_height]
    if len(path_cells) != path_width * path_height:
        raise SystemExit("source map pathing map is truncated")
    terrain_blob = run([str(mpqtool), "-mpq", str(source), "cat", "war3map.w3e"], text=False)
    terrain_offset = 8 + 1 + 4
    terrain_tiles = struct.unpack_from("<I", terrain_blob, terrain_offset)[0]
    terrain_offset += 4 + terrain_tiles * 4
    doodad_tiles = struct.unpack_from("<I", terrain_blob, terrain_offset)[0]
    terrain_offset += 4 + doodad_tiles * 4
    terrain_width, terrain_height, terrain_x, terrain_y = struct.unpack_from("<IIff", terrain_blob, terrain_offset)
    terrain_offset += 16
    def terrain_is_dry(px, py):
        x = round(px / 4.0)
        y = round(py / 4.0)
        for vy in range(max(0, y - 6), min(terrain_height, y + 7)):
            for vx in range(max(0, x - 6), min(terrain_width, x + 7)):
                vertex = terrain_offset + (vy * terrain_width + vx) * 7
                if terrain_blob[vertex + 4] & 0x40:  # W3E vertex water flag
                    return False
        return True
    window_w, window_h = 36, 48
    integral = [[0] * (path_width + 1) for _ in range(path_height + 1)]
    for py in range(path_height):
        row_blocked = 0
        for px in range(path_width):
            row_blocked += bool(path_cells[py * path_width + px] & 0x02)  # unwalkable
            integral[py + 1][px + 1] = integral[py][px + 1] + row_blocked

    def blocked_count(left, top):
        right, bottom = left + window_w, top + window_h
        return (integral[bottom][right] - integral[top][right]
                - integral[bottom][left] + integral[top][left])

    candidates = []
    for top in range(16, path_height - window_h - 16, 2):
        for left in range(16, path_width - window_w - 16, 2):
            blocked = blocked_count(left, top)
            if blocked <= window_w * window_h * 0.04 and terrain_is_dry(left + window_w // 2, top + window_h // 2):
                candidates.append((left + window_w // 2, top + window_h // 2, blocked))
    if len(candidates) < group_count:
        raise SystemExit("not enough low-blocking land regions for creep groups")

    # Seed around a regular map-wide grid, then fill gaps with farthest-point
    # selection. This makes placements deterministic and avoids neighboring piles.
    selected = []
    for row in range(4):
        for col in range(4):
            target_x = round((col + 0.5) * path_width / 4)
            target_y = round((row + 0.5) * path_height / 4)
            candidate = min(candidates, key=lambda p: (p[0] - target_x) ** 2 + (p[1] - target_y) ** 2 + p[2] * 4)
            if all((candidate[0] - p[0]) ** 2 + (candidate[1] - p[1]) ** 2 >= 48 ** 2 for p in selected):
                selected.append(candidate)
    while len(selected) < group_count:
        available = [p for p in candidates if all((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 >= 48 ** 2 for q in selected)]
        if not available:
            raise SystemExit("could not separate all creep groups")
        selected.append(max(available, key=lambda p: (min((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 for q in selected), -p[2])))

    # W3E/WPM origins are the map southwest corner; their cells are 32 units.
    group_centers = []
    for px, py, blocked in selected[:group_count]:
        x = terrain_x + px * 32.0
        y = terrain_y + py * 32.0
        group_centers.append((x, y))

    jass = [
        "function CreepSandboxRemoveDestructable takes nothing returns nothing",
        "    call RemoveDestructable(GetEnumDestructable())",
        "endfunction",
        "",
        "function CreepSandboxRemoveUnit takes nothing returns nothing",
        "    call RemoveUnit(GetEnumUnit())",
        "endfunction",
        "",
        "function CreepSandboxSpawn takes nothing returns nothing",
        "    local group units = CreateGroup()",
        "    local rect spawnRect",
        "    local unit current",
    ]
    for index, row in enumerate(roster):
        group = index // group_size
        within = index % group_size
        col, r = within % group_cols, within // group_cols
        center_x, center_y = group_centers[group]
        x = center_x + (col - (group_cols - 1) / 2) * spacing
        y = center_y + (r - (group_rows - 1) / 2) * spacing
        jass.append(f"    call CreateUnit(Player(0), '{row['rawcode']}', {x:.1f}, {y:.1f}, 270.0)")
    for center_x, center_y in group_centers:
        min_x, max_x = center_x - 560, center_x + 560
        min_y, max_y = center_y - 720, center_y + 720
        cleanup = [
            f"    set spawnRect = Rect({min_x:.1f}, {min_y:.1f}, {max_x:.1f}, {max_y:.1f})",
            "    call EnumDestructablesInRect(spawnRect, null, function CreepSandboxRemoveDestructable)",
            "    call GroupEnumUnitsInRect(units, spawnRect, null)",
            "    loop",
            "        set current = FirstOfGroup(units)",
            "        exitwhen current == null",
            "        call GroupRemoveUnit(units, current)",
            "        call RemoveUnit(current)",
            "    endloop",
            "    call RemoveRect(spawnRect)",
        ]
        # Cleanup happens before spawn below; collect for insertion after locals.
        jass[ jass.index("    local unit current") + 1 : jass.index("    local unit current") + 1 ] = cleanup
    jass += [
        "    call DestroyGroup(units)",
        "    call SetCameraPosition(0.0, 0.0)",
        "endfunction",
        "",
    ]
    script = run([str(mpqtool), "-mpq", str(source), "cat", "war3map.j"])
    if "function CreepSandboxSpawn" in script:
        raise SystemExit("source map unexpectedly contains sandbox injection")
    match = re.search(r"(?m)^function main takes nothing returns nothing$", script)
    if not match:
        raise SystemExit("source map has no main function")
    script = script[:match.start()] + "\n".join(jass) + script[match.start():]
    main_end = re.search(r"(?m)^    call RunInitializationTriggers\(  \)$", script)
    if not main_end:
        raise SystemExit("source map main function has no initialization-trigger call")
    end = main_end.end()
    script = script[:end] + "\n    call CreepSandboxSpawn()" + script[end:]
    script_path = work / "war3map.j"
    script_path.write_text(script, encoding="utf-8")

    wts = run([str(mpqtool), "-mpq", str(source), "cat", "war3map.wts"])
    def replace_wts(text, number, value):
        pattern = rf"(?ms)^STRING {number}\s*\{{.*?^\}}"
        replacement = f"STRING {number}\n{{\n{value}\n}}"
        if re.search(pattern, text):
            return re.sub(pattern, replacement, text, count=1)
        return text.rstrip() + f"\n\n{replacement}\n"
    wts = replace_wts(wts, 3, "Creep Ability Sandbox")
    wts = replace_wts(wts, 5, f"{len(roster)} retail neutral-hostile unit types with authored abilities, owned by Player 1.")
    wts_path = work / "war3map.wts"
    wts_path.write_text(wts, encoding="utf-8")

    # Copy only after source extraction and script generation have succeeded.
    output.write_bytes(source.read_bytes())
    run([args.smpq, "-a", "-f", str(output), "war3map.j", "war3map.wts"], cwd=work)

    with (output_dir / "Creep Ability Sandbox roster.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["rawcode", "name", "unit_class", "ability_aliases"])
        writer.writeheader()
        writer.writerows(roster)

    print(f"Wrote {output} with {len(roster)} valid retail neutral-hostile unit types.")
    print(f"Roster: {output_dir / 'Creep Ability Sandbox roster.csv'}")
    print(f"Placed {group_count} groups of up to {group_size}, {group_cols}x{group_rows} per group, spacing {spacing}.")
    print("Group centers:", ", ".join(f"({x:.0f},{y:.0f})" for x, y in group_centers))
    temporary_work.cleanup()


if __name__ == "__main__":
    main()
