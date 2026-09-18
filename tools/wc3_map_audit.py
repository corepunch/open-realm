#!/usr/bin/env python3
"""Run retail Warcraft III campaign maps (or selected loose Maps/*.w3x) for a bounded frame budget.

Each map gets an isolated writable home and UDP port. The resulting JSON,
Markdown matrix, and raw logs are evidence of bounded runtime health only;
they do not prove that a mission is playable or completable.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import fnmatch
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
WTS_RE = re.compile(r"(?ms)^\s*STRING\s+(\d+)\s*\{\s*(.*?)\s*^\}")
BROAD_RE = re.compile(
    r"(?:error|failed|missing|invalid|unimplemented|assert|overflow|exhaust|\bfull\b|stopped|"
    r"unreachable|not found|fatal|signal|segmentation|abort|corrupt|cannot)", re.I)
FAMILY_MEANINGS = {
    "SIGSEGV": "Process exited on signal 11; an optional serial rerun confirms reproducibility.",
    "PROCESS_EXIT": "Process returned a nonzero exit code other than signal 11.",
    "SLK_MISSING": "A required SLK failed to load.",
    "CREEP_SLEEP_ART": "ACsp TargetArt is absent; engine used canonical art.",
    "CREEP_SLEEP_SPAWN": "Creep-sleep overlay creation failed.",
    "MODEL_POOL_FULL": "Server model configstring pool was full.",
    "SOUND_POOL_FULL": "Server sound configstring pool was full.",
    "INVENTORY_DATA": "AInv inv1 resolved to zero for the listed unit rawcodes.",
    "TECH_CAPACITY": "Per-player tech-state capacity was exhausted.",
    "AI_STOP": "Campaign AI script stopped on an unimplemented native.",
    "FIRE_EFFECT_REGISTRATION": "Building-fire model registration failed.",
    "DEFAULT_ZFOG": "DefaultZFog.Style is missing for version 1.",
    "PARSER_ERROR": "Runtime emitted an otherwise unqualified parser error.",
}


def run_mpqtool(mpqtool: Path, archive: Path, command: str, member: str) -> bytes:
    """Read archive data while retaining mpqtool's actionable error."""
    proc = subprocess.run(
        [str(mpqtool), "-mpq", str(archive), command, member], check=False, capture_output=True)
    if proc.returncode:
        detail = proc.stderr.decode(errors="replace").strip() or proc.stdout.decode(errors="replace").strip()
        raise RuntimeError(detail or f"mpqtool exited {proc.returncode}")
    return proc.stdout


def enumerate_maps(data: Path, mpqtool: Path) -> list[dict[str, str]]:
    """Enumerate the 44 RoC and 53 TFT campaign members from retail archives."""
    specs = [
        (data / "War3.mpq", "Maps/Campaign", "RoC"),
        (data / "Frozen Throne/War3x.mpq", "Maps/FrozenThrone/Campaign", "TFT"),
    ]
    maps = []
    for archive, directory, edition in specs:
        if not archive.is_file():
            raise RuntimeError(f"required archive not found: {archive}")
        listing = run_mpqtool(mpqtool, archive, "ls", directory).decode(errors="replace")
        for line in listing.splitlines():
            filename = line.strip().replace("\\", "/").removesuffix("/")
            if filename.lower().endswith((".w3m", ".w3x")):
                maps.append({
                    "archive": str(archive), "edition": edition,
                    "filename": filename, "path": f"{directory}/{filename}",
                })
    return maps


def loose_map_spec(path: Path, data: Path) -> dict[str, str]:
    """Build one audit row for a disk-resident .w3m/.w3x under the data tree."""
    data = data.expanduser().resolve()
    candidate = path.expanduser()
    candidate = candidate.resolve() if candidate.is_absolute() else (Path.cwd() / candidate).resolve()
    if not candidate.is_file():
        raise RuntimeError(f"loose map not found: {path}")
    suffix = candidate.suffix.lower()
    if suffix not in (".w3m", ".w3x"):
        raise RuntimeError(f"loose map must be .w3m or .w3x: {candidate}")
    try:
        rel = candidate.relative_to(data)
    except ValueError as error:
        raise RuntimeError(f"loose map must live under data dir {data}: {candidate}") from error
    return {
        "archive": str(candidate),
        "edition": "TFT" if suffix == ".w3x" else "RoC",
        "filename": candidate.name,
        "path": str(rel).replace("\\", "/"),
        "loose": "1",
    }


def clean_title(text: str) -> str:
    """Remove WC3 color/newline markup from report-facing map titles."""
    text = re.sub(r"\|c[0-9A-Fa-f]{8}|\|r", "", text)
    text = text.replace("\\n", " ").replace("\r", " ").replace("\n", " ")
    return re.sub(r"\s+", " ", text).strip()


def parse_wts(source: str) -> dict[str, str]:
    """Parse trigger strings, including the BOM-prefixed STRING 0 used by campaigns."""
    return {str(int(key)): value for key, value in WTS_RE.findall(source.lstrip("\ufeff"))}


def resolve_trig(text: str, strings: dict[str, str]) -> str:
    """Resolve one TRIGSTR token without inventing a fallback value."""
    match = re.fullmatch(r"TRIGSTR_0*(\d+)", text, re.I)
    return strings.get(str(int(match.group(1))), text) if match else text


def read_cstring(data: bytes, offset: int) -> tuple[str, int]:
    """Read one W3I NUL-terminated string and return the next byte offset."""
    end = data.find(b"\0", offset)
    if end < 0:
        raise ValueError("unterminated W3I string")
    return data[offset:end].decode(errors="replace"), end + 1


def parse_w3i_name(info: bytes, wts: str, fallback: str) -> str:
    """Prefer the human-readable loading title/subtitle over the internal map ID."""
    if len(info) < 12:
        raise ValueError("war3map.w3i is too small")
    version = struct.unpack_from("<I", info)[0]
    offset = 12 + (16 if version >= 28 else 0)
    name, offset = read_cstring(info, offset)
    for _ in range(3):
        _, offset = read_cstring(info, offset)
    offset += 48 + 8 + 4 + 1 + 4
    if version >= 25:
        _, offset = read_cstring(info, offset)
    _, offset = read_cstring(info, offset)
    title, offset = read_cstring(info, offset)
    subtitle, _ = read_cstring(info, offset)
    strings = parse_wts(wts)
    fields = [clean_title(resolve_trig(text, strings)) for text in (title, subtitle)]
    fields = [text for text in fields if text]
    if fields:
        return " — ".join(dict.fromkeys(fields))
    return clean_title(resolve_trig(name, strings)) or fallback


def map_name(item: dict[str, str], mpqtool: Path) -> str:
    """Extract nested W3I/WTS metadata and resolve the report-facing name."""
    if item.get("loose"):
        archive = Path(item["archive"])
        info = run_mpqtool(mpqtool, archive, "cat", "war3map.w3i")
        try:
            wts = run_mpqtool(mpqtool, archive, "cat", "war3map.wts").decode(errors="replace")
        except RuntimeError:
            wts = ""
        return parse_w3i_name(info, wts, Path(item["filename"]).stem)
    payload = run_mpqtool(mpqtool, Path(item["archive"]), "cat", item["path"])
    with tempfile.NamedTemporaryFile(prefix="wc3-map-audit-", suffix=Path(item["filename"]).suffix) as nested:
        nested.write(payload)
        nested.flush()
        info = run_mpqtool(mpqtool, Path(nested.name), "cat", "war3map.w3i")
        try:
            wts = run_mpqtool(mpqtool, Path(nested.name), "cat", "war3map.wts").decode(errors="replace")
        except RuntimeError:
            wts = ""
    return parse_w3i_name(info, wts, Path(item["filename"]).stem)


def compact_diagnostics(output: str, status: str, exit_code: int | None,
                        serial_crash: bool) -> tuple[list[str], set[str]]:
    """Collapse entity/resource floods while preserving counts and native names."""
    exact = collections.Counter(line.strip() for line in output.splitlines() if BROAD_RE.search(line))
    errors: list[str] = []
    families: set[str] = set()

    def take(pattern: str) -> list[tuple[str, int]]:
        matched = []
        regex = re.compile(pattern, re.I)
        for line in list(exact):
            if regex.search(line):
                matched.append((line, exact.pop(line)))
        return matched

    for line, count in take(r"^SLK: failed to load"):
        path = re.search(r"'([^']+)'", line)
        errors.append(f"SLK_MISSING `{path.group(1) if path else line}` ×{count}")
        families.add("SLK_MISSING")
    rows = take(r"CreepSleep: ACsp TargetArt missing")
    if rows:
        errors.append(f"CREEP_SLEEP_ART ×{sum(count for _, count in rows)}")
        families.add("CREEP_SLEEP_ART")
    rows = take(r"CreepSleep: failed to spawn ACsp overlay")
    if rows:
        errors.append(f"CREEP_SLEEP_SPAWN ×{sum(count for _, count in rows)}")
        families.add("CREEP_SLEEP_SPAWN")
    for start, family in ((32, "MODEL_POOL_FULL"), (288, "SOUND_POOL_FULL")):
        rows = take(rf"^SV_FindIndex: pool full start={start}")
        if rows:
            errors.append(f"{family} ×{sum(count for _, count in rows)} ({len(rows)} resources)")
            families.add(family)
    rows = take(r"^G_InventoryCapacity:")
    if rows:
        rawcodes: collections.Counter[str] = collections.Counter()
        for line, count in rows:
            match = re.search(r"G_InventoryCapacity: (\S+)", line)
            rawcodes[match.group(1) if match else "?"] += count
        detail = ", ".join(f"{name}×{count}" for name, count in sorted(rawcodes.items()))
        errors.append(f"INVENTORY_DATA ×{sum(rawcodes.values())} ({detail})")
        families.add("INVENTORY_DATA")
    rows = take(r"^G_FindTechSlot:.*capacity .* exhausted")
    if rows:
        errors.append(f"TECH_CAPACITY ×{sum(count for _, count in rows)}")
        families.add("TECH_CAPACITY")
    for line, count in take(r"^JASS runtime error: unimplemented native:"):
        native = line.rsplit(":", 1)[-1].strip()
        errors.append(f"JASS `{native}` ×{count}")
        families.add(f"JASS:{native}")
    for line, count in take(r"^WC3 AI:.*stopped: unimplemented native:"):
        match = re.search(r"script (\S+) stopped: unimplemented native: (\S+)", line)
        detail = f"`{match.group(1)}` → `{match.group(2)}`" if match else f"`{line}`"
        errors.append(f"AI_STOP {detail} ×{count}")
        families.add("AI_STOP")
    rows = take(r"^onfire_level_changed: failed to register")
    if rows:
        errors.append(f"FIRE_EFFECT_REGISTRATION ×{sum(count for _, count in rows)} ({len(rows)} resources)")
        families.add("FIRE_EFFECT_REGISTRATION")
    for pattern, family in ((r"DefaultZFog\.Style is missing", "DEFAULT_ZFOG"),
                            (r"^Parser Error$", "PARSER_ERROR")):
        rows = take(pattern)
        if rows:
            errors.append(f"{family} ×{sum(count for _, count in rows)}")
            families.add(family)
    for line, count in sorted(exact.items()):
        errors.append(f"`{line}` ×{count}")
        families.add(line)
    if status == "crashed":
        if exit_code == -11:
            suffix = "; reproduced serially" if serial_crash else ""
            errors.insert(0, f"SIGSEGV (exit {exit_code}{suffix})")
            families.add("SIGSEGV")
        else:
            errors.insert(0, f"PROCESS_EXIT (code {exit_code})")
            families.add("PROCESS_EXIT")
    return errors, families


def run_map(item: dict[str, str], index: int, args: argparse.Namespace, log_path: Path) -> dict[str, Any]:
    """Run one bounded server in an isolated home and retain its complete log."""
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="wc3-map-audit-home-") as home:
        env = os.environ.copy()
        env["XDG_DATA_HOME"] = home
        command = [
            str(args.binary), "-data", str(args.data),
            *(["-tft"] if item["edition"] == "TFT" else []),
            "+dedicated", "1", "+set", "game_port", str(args.port_base + index),
            "+set", "com_fast_forward", "1", "+set", "vid_hidden", "1",
            "+set", "skip_cutscene", "1", "+map", item["path"],
            "+com_frame_limit", str(args.frames),
        ]
        try:
            proc = subprocess.run(
                command, cwd=ROOT, env=env, check=False, capture_output=True,
                text=True, errors="replace", timeout=args.timeout)
            output = proc.stdout + proc.stderr
            exit_code = proc.returncode
            status = "completed" if exit_code == 0 else "crashed"
        except subprocess.TimeoutExpired as error:
            stdout = error.stdout.decode(errors="replace") if isinstance(error.stdout, bytes) else error.stdout or ""
            stderr = error.stderr.decode(errors="replace") if isinstance(error.stderr, bytes) else error.stderr or ""
            output, exit_code, status = stdout + stderr, None, "timeout"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(output)
    return {
        **item, "status": status, "exit_code": exit_code,
        "wall_seconds": round(time.monotonic() - started, 3), "log": str(log_path),
    }


def render_markdown(report: dict[str, Any]) -> str:
    """Render the audit as a GitHub-issue-ready per-map matrix."""
    maps = report["maps"]
    status = collections.Counter(item["status"] for item in maps)
    family_maps: collections.Counter[str] = collections.Counter()
    for item in maps:
        family_maps.update(item["families"])
    reproduced = sum(item.get("serial_crash", False) for item in maps if item["status"] == "crashed")
    sim = report["simulated_seconds"]
    sim_unit = "second" if sim == 1 else "seconds"
    lines = [
        "## Actual bounded campaign-map audit", "",
        f"Audited commit `{report['commit']}` with `build/bin/openwarcraft3`.", "",
        f"Each of {len(maps)} retail campaign maps was launched headlessly with `com_fast_forward=1` for "
        f"**{report['frames']} frames / {sim:.0f} simulated {sim_unit}**. Each process used "
        f"an isolated writable home, a unique UDP port, and a {report['timeout_seconds']}s wall timeout. "
        f"The {report['jobs']}-worker sweep finished in {report['wall_seconds']:.1f}s.", "",
        f"Result: **{status['completed']} reached the frame limit, {status['crashed']} crashed, "
        f"{status['timeout']} timed out**. {reproduced} crashes reproduced serially.", "",
        "> Reaching the frame limit is a startup/runtime smoke result. It does not prove objectives, combat, "
        "cinematics, mission completion, or visual correctness.", "", "### Error-family reach", "",
        "| Family | Maps | Meaning |", "| --- | ---: | --- |",
    ]
    for family, count in family_maps.most_common():
        meaning = (f"Unimplemented native `{family.split(':', 1)[1]}` executed."
                   if family.startswith("JASS:") else FAMILY_MEANINGS.get(family, family))
        lines.append(f"| `{family}` | {count} | {meaning.replace('|', '&#124;')} |")
    for edition in ("RoC", "TFT"):
        lines.extend([
            "", f"### {edition}: per-map results", "",
            "| Map name | Filename | Result | Errors observed |",
            "| --- | --- | --- | --- |",
        ])
        for item in maps:
            if item["edition"] != edition:
                continue
            name = item["name"].replace("|", "&#124;")
            errors = "; ".join(item["compact_errors"]).replace("|", "&#124;") or "none"
            if item["status"] == "crashed":
                result = "SIGSEGV" if item["exit_code"] == -11 else f"exit {item['exit_code']}"
            else:
                result = item["status"]
            lines.append(f"| {name} | `{item['filename']}` | **{result}** | {errors} |")
    return "\n".join(lines) + "\n"


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=ROOT / "data/Warcraft III")
    parser.add_argument("--mpqtool", type=Path, default=ROOT / "build/bin/mpqtool")
    parser.add_argument("--binary", type=Path, default=ROOT / "build/bin/openwarcraft3")
    parser.add_argument(
        "--frames", type=int, default=600, help="server frames per map (10 frames = 1 simulated second)")
    parser.add_argument("--timeout", type=int, default=120, help="wall-clock seconds per map")
    parser.add_argument("--jobs", type=int, default=4, help="concurrent isolated map processes")
    parser.add_argument("--port-base", type=int, default=28100, help="first unique UDP port")
    parser.add_argument("--map", default="*", help="shell-style filename or archive-path filter")
    parser.add_argument(
        "--loose-map", action="append", default=[], metavar="PATH",
        help="audit a disk-resident .w3m/.w3x under --data instead of campaign archives")
    parser.add_argument("--limit", type=int, default=0, help="limit maps after filtering")
    parser.add_argument("--rerun-crashes", action="store_true", help="confirm first-pass crashes serially")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/wc3-map-audit")
    parser.add_argument("--fail-on-crash", action="store_true", help="exit nonzero after writing the report")
    return parser.parse_args(argv)


def filter_maps(maps: list[dict[str, str]], pattern: str) -> list[dict[str, str]]:
    """Keep maps whose filename or +map path matches the shell-style filter."""
    needle = pattern.lower()
    return [
        item for item in maps
        if fnmatch.fnmatch(item["filename"].lower(), needle)
        or fnmatch.fnmatch(item["path"].lower(), needle)
    ]


def main() -> int:
    args = parse_args()
    if args.frames < 1 or args.timeout < 1 or args.jobs < 1:
        print("error: frames, timeout, and jobs must be positive", file=sys.stderr)
        return 2
    if args.port_base < 1024 or args.port_base + 1000 >= 65536:
        print("error: port-base must leave room for unique audit ports", file=sys.stderr)
        return 2
    for tool in (args.mpqtool, args.binary):
        if not tool.is_file():
            print(f"error: executable not found: {tool}", file=sys.stderr)
            return 2
    try:
        if args.loose_map:
            maps = [loose_map_spec(Path(path), args.data) for path in args.loose_map]
        else:
            maps = enumerate_maps(args.data, args.mpqtool)
        maps = filter_maps(maps, args.map)
        if args.limit:
            maps = maps[:args.limit]
        if not maps:
            kind = "loose" if args.loose_map else "campaign"
            raise RuntimeError(f"no {kind} maps matched {args.map!r}")
        args.output_dir.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        results: list[dict[str, Any] | None] = [None] * len(maps)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            pending = {}
            for index, item in enumerate(maps):
                log = args.output_dir / "logs" / f"{index:03d}-{Path(item['filename']).stem}.log"
                pending[pool.submit(run_map, item, index, args, log)] = index
            for future in concurrent.futures.as_completed(pending):
                index = pending[future]
                try:
                    results[index] = future.result()
                except Exception as error:
                    results[index] = {
                        **maps[index], "status": "auditor-error", "exit_code": None,
                        "wall_seconds": 0, "log": "", "auditor_error": str(error),
                    }
                done = sum(result is not None for result in results)
                print(f"[{done:02d}/{len(results)}] {maps[index]['filename']}: {results[index]['status']}", flush=True)
        report_maps = [item for item in results if item is not None]
        if args.rerun_crashes:
            for index, item in enumerate(report_maps):
                if item["status"] != "crashed":
                    continue
                log = args.output_dir / "logs" / f"rerun-{Path(item['filename']).stem}.log"
                rerun = run_map(item, index, args, log)
                item["serial_crash"] = rerun["status"] == "crashed" and rerun["exit_code"] == item["exit_code"]
                item["serial_rerun"] = {
                    "status": rerun["status"], "exit_code": rerun["exit_code"],
                    "wall_seconds": rerun["wall_seconds"], "log": rerun["log"],
                }
        for item in report_maps:
            item["name"] = map_name(item, args.mpqtool)
            output = Path(item["log"]).read_text(errors="replace") if item.get("log") else item.get("auditor_error", "")
            errors, families = compact_diagnostics(
                output, item["status"], item["exit_code"], item.get("serial_crash", False))
            item["compact_errors"], item["families"] = errors, sorted(families)
        report = {
            "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "frames": args.frames, "simulated_seconds": args.frames / 10,
            "timeout_seconds": args.timeout, "jobs": args.jobs,
            "wall_seconds": round(time.monotonic() - started, 3), "maps": report_maps,
        }
        markdown = render_markdown(report)
        (args.output_dir / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        (args.output_dir / "report.md").write_text(markdown)
        print(f"wrote {args.output_dir / 'report.json'}")
        print(f"wrote {args.output_dir / 'report.md'}")
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    failed = any(item["status"] != "completed" for item in report_maps)
    return 1 if args.fail_on_crash and failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
