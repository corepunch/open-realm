#!/usr/bin/env python3
"""Walk the player Hero on every retail campaign map, then save/load/compare.

Each map gets an isolated writable home and UDP port. The engine cvar
`wc3_hero_saveload_audit` issues a move order, writes a save, reloads it, and
prints HERO_SAVELOAD lines. This is a local diagnostic, not a CI test; maps
without a live Hero are reported as no_hero rather than save/load failures.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import fnmatch
import importlib.util
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
STATUS_RE = re.compile(r"^HERO_SAVELOAD status=(\S+)(?:\s+(.*))?$")
SNAPSHOT_RE = re.compile(r"^HERO_SAVELOAD snapshot=(before|after|dump)\s+(.*)$")
SPEC = importlib.util.spec_from_file_location("wc3_map_audit", ROOT / "tools/wc3_map_audit.py")
MAP_AUDIT = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MAP_AUDIT)

FAMILY_MEANINGS = {
    "pass": "Hero origin, abilities, and inventory matched after save/load.",
    "no_hero": "No live Hero was found before the wait budget; often an interlude.",
    "fail_order": "unit_issueorder rejected every nearby walk candidate.",
    "fail_move": "The Hero accepted a walk order but origin did not change.",
    "fail_save": "WriteGame failed on the live map.",
    "fail_load": "ReadGame rejected the file written by this process.",
    "fail_origin": "Restored origin did not match the pre-save snapshot.",
    "fail_abilities": "Restored Hero skills or runtime abilities did not match.",
    "fail_inventory": "Restored inventory class or charges did not match.",
    "fail_identity": "Restored unit number or class_id did not match.",
    "fail_missing": "The Hero edict was gone after load.",
    "incomplete": "The process reached its frame limit without a HERO_SAVELOAD status.",
    "SIGSEGV": "Process exited on signal 11.",
    "PROCESS_EXIT": "Process returned a nonzero exit code other than signal 11.",
}


def parse_hero_saveload(output: str) -> dict[str, Any]:
    """Pull the last status line and before/after snapshots from an engine log."""
    status, detail, before, after = None, "", "", ""
    for line in output.splitlines():
        match = STATUS_RE.match(line.strip())
        if match:
            status, detail = match.group(1), match.group(2) or ""
            continue
        match = SNAPSHOT_RE.match(line.strip())
        if not match:
            continue
        if match.group(1) == "before":
            before = match.group(2)
        elif match.group(1) == "after":
            after = match.group(2)
    return {"status": status, "detail": detail, "before": before, "after": after}


def classify(output: str, exit_code: int | None, timed_out: bool) -> dict[str, Any]:
    """Turn process outcome plus HERO_SAVELOAD lines into one map result."""
    parsed = parse_hero_saveload(output)
    if timed_out:
        parsed["status"] = parsed["status"] or "timeout"
        parsed["families"] = ["timeout"]
        return parsed
    if exit_code not in (0, None) and parsed["status"] not in {None, "pass", "no_hero"}:
        parsed["families"] = [parsed["status"]]
        return parsed
    if exit_code == -11:
        parsed["status"] = parsed["status"] or "crashed"
        parsed["families"] = ["SIGSEGV"]
        return parsed
    if exit_code not in (0, None):
        parsed["status"] = parsed["status"] or "crashed"
        parsed["families"] = ["PROCESS_EXIT"]
        return parsed
    if not parsed["status"]:
        parsed["status"] = "incomplete"
        parsed["families"] = ["incomplete"]
        return parsed
    parsed["families"] = [parsed["status"]]
    return parsed


def run_map(item: dict[str, str], index: int, args: argparse.Namespace, log_path: Path) -> dict[str, Any]:
    """Run one dedicated map with the hero save/load audit armed."""
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="wc3-hero-saveload-home-") as home:
        env = os.environ.copy()
        env["XDG_DATA_HOME"] = home
        command = [
            str(args.binary), "-data", str(args.data),
            *(["-tft"] if item["edition"] == "TFT" else []),
            "+dedicated", "1", "+set", "game_port", str(args.port_base + index),
            "+set", "com_fast_forward", "1", "+set", "vid_hidden", "1",
            "+set", "skip_cutscene", "1", "+set", "wc3_hero_saveload_audit", "1",
            "+map", item["path"], "+com_frame_limit", str(args.frames),
        ]
        try:
            proc = subprocess.run(
                command, cwd=ROOT, env=env, check=False, capture_output=True,
                text=True, errors="replace", timeout=args.timeout)
            output = proc.stdout + proc.stderr
            exit_code, timed_out = proc.returncode, False
        except subprocess.TimeoutExpired as error:
            stdout = error.stdout.decode(errors="replace") if isinstance(error.stdout, bytes) else error.stdout or ""
            stderr = error.stderr.decode(errors="replace") if isinstance(error.stderr, bytes) else error.stderr or ""
            output, exit_code, timed_out = stdout + stderr, None, True
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(output)
    parsed = classify(output, exit_code, timed_out)
    return {
        **item, **parsed, "exit_code": exit_code, "timed_out": timed_out,
        "wall_seconds": round(time.monotonic() - started, 3), "log": str(log_path),
    }


def render_markdown(report: dict[str, Any]) -> str:
    """Render a GitHub-issue-ready per-map matrix."""
    maps = report["maps"]
    status = collections.Counter(item["status"] for item in maps)
    family_maps: collections.Counter[str] = collections.Counter()
    for item in maps:
        family_maps.update(item["families"])
    failed = sum(1 for item in maps if item["status"] not in {"pass", "no_hero"})
    lines = [
        "## Hero walk / save / load campaign audit", "",
        f"Audited commit `{report['commit']}` with `build/bin/openwarcraft3`.", "",
        f"Each of {len(maps)} retail campaign maps launched headlessly with "
        f"`wc3_hero_saveload_audit=1`, `com_fast_forward=1`, and "
        f"**{report['frames']} frames / {report['simulated_seconds']:.0f} simulated seconds**. "
        f"The {report['jobs']}-worker sweep finished in {report['wall_seconds']:.1f}s.", "",
        f"Result: **{status['pass']} passed, {status['no_hero']} had no live Hero, "
        f"{failed} failed or crashed**.", "",
        "> A pass means the player Hero's origin, Hero skills, runtime abilities, and "
        "inventory matched after `WriteGame`/`ReadGame` on that live map. It does not "
        "go through `SV_Map` reconnect, and `no_hero` is often an interlude rather than "
        "a serializer bug.", "", "### Status reach", "",
        "| Status | Maps | Meaning |", "| --- | ---: | --- |",
    ]
    for family, count in family_maps.most_common():
        lines.append(f"| `{family}` | {count} | {FAMILY_MEANINGS.get(family, family).replace('|', '&#124;')} |")
    for edition in ("RoC", "TFT"):
        lines.extend([
            "", f"### {edition}: per-map results", "",
            "| Map name | Filename | Result | Before | After |",
            "| --- | --- | --- | --- | --- |",
        ])
        for item in maps:
            if item["edition"] != edition:
                continue
            name = item["name"].replace("|", "&#124;")
            before = (item.get("before") or "").replace("|", "&#124;") or "—"
            after = (item.get("after") or "").replace("|", "&#124;") or "—"
            lines.append(f"| {name} | `{item['filename']}` | **{item['status']}** | `{before}` | `{after}` |")
    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=ROOT / "data/Warcraft III")
    parser.add_argument("--mpqtool", type=Path, default=ROOT / "build/bin/mpqtool")
    parser.add_argument("--binary", type=Path, default=ROOT / "build/bin/openwarcraft3")
    parser.add_argument(
        "--frames", type=int, default=400, help="server frames per map (10 frames = 1 simulated second)")
    parser.add_argument("--timeout", type=int, default=120, help="wall-clock seconds per map")
    parser.add_argument("--jobs", type=int, default=4, help="concurrent isolated map processes")
    parser.add_argument("--port-base", type=int, default=29100, help="first unique UDP port")
    parser.add_argument("--map", default="*", help="shell-style filename or archive-path filter")
    parser.add_argument("--limit", type=int, default=0, help="limit maps after filtering")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/wc3-hero-saveload-audit")
    parser.add_argument("--fail-on-error", action="store_true", help="exit nonzero after writing the report")
    return parser.parse_args()


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
        maps = [item for item in MAP_AUDIT.enumerate_maps(args.data, args.mpqtool)
                if fnmatch.fnmatch(item["filename"].lower(), args.map.lower())
                or fnmatch.fnmatch(item["path"].lower(), args.map.lower())]
        if args.limit:
            maps = maps[:args.limit]
        if not maps:
            raise RuntimeError(f"no campaign maps matched {args.map!r}")
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
                        "wall_seconds": 0, "log": "", "families": ["auditor-error"],
                        "auditor_error": str(error),
                    }
                done = sum(result is not None for result in results)
                print(f"[{done:02d}/{len(results)}] {maps[index]['filename']}: {results[index]['status']}", flush=True)
        report_maps = [item for item in results if item is not None]
        for item in report_maps:
            item["name"] = MAP_AUDIT.map_name(item, args.mpqtool)
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
    failed = any(item["status"] not in {"pass", "no_hero"} for item in report_maps)
    return 1 if args.fail_on_error and failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
