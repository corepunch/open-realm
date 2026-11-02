#!/usr/bin/env python3
"""Verify complete read-only Follow/combat/removal producer journeys."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re

HASH = "d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"


def digest(value):
    return hashlib.sha256(json.dumps(value, separators=(",", ":")).encode()).hexdigest()


def contract(rows):
    exported = [r for r in rows if r.get("event") == "metadata-row"]
    if len(exported) != 14550:
        raise ValueError("incomplete Follow matrix")
    if [(r["parent"], r["child"]) for r in exported] != [(i, j) for i in range(1455) for j in range(10)]:
        raise ValueError("Follow export order differs")
    kinds = ["integer"] * 3 + ["real"] * 3 + ["integer"] * 4
    if any(r["kind"] != kinds[r["child"]] or type(r["word"]) is not int or not 0 <= r["word"] <= 0xffffffff for r in exported):
        raise ValueError("Follow export words/types differ")
    values = [[r["word"] for r in exported[i:i+10]] for i in range(0, len(exported), 10)]
    labels = []
    for r in rows:
        if r.get("event") != "metadata-marker" or " row=" not in r.get("value", ""):
            continue
        m = re.fullmatch(r"PATHMETA follow_lifetime row=(\d+) tick=(\d+) case=(\d+) label=(\w+)", r["value"])
        if not m or int(m[1]) != len(labels) or values[len(labels)][:2] != [int(m[2]), int(m[3])]:
            raise ValueError("Follow label/order differs")
        labels.append(m[4])
    if len(labels) != len(values):
        raise ValueError("missing Follow labels")
    samples = [[v for v, label in zip(values, labels) if v[1] == i and label == "sample"] for i in range(7)]
    if any([v[0] for v in sample] != list(range(1, 201)) for sample in samples):
        raise ValueError("incomplete Follow subject journey")
    actions = {(v[0], v[1], label): v for v, label in zip(values, labels) if label != "sample"}
    for i in range(7):
        order = 851986 if i in (0, 2, 4) else 851971
        if actions[(1, i, "follow")][2] != order:
            raise ValueError("Follow admission head differs")
    for i in range(4):
        head = 0 if i % 2 == 0 else 851971
        if actions[(40, i, "loss_after")][2] != head or actions[(50, i, "replacement_target")][2] != head:
            raise ValueError("combat-parent loss/reuse head differs")
        if actions[(60, i, "enemy_death")][2] != 0:
            raise ValueError("enemy loss did not complete head")
        if actions[(81, i, "replacement_subject")][2] != 0 or any(v[2] for v in samples[i] if v[0] >= 81):
            raise ValueError("replacement subject adopted old ownership")
    for i in (0, 2):
        if any(v[9] for v in samples[i] if v[0] < 40):
            raise ValueError("explicit Move unexpectedly attacked")
    for i in (1, 3, 6):
        damaged = [v for v in samples[i] if 10 < v[0] < 40 and v[9]]
        if not damaged or any(v[9] != v[6] for v in damaged):
            raise ValueError("Smart combat is absent or from another subject")
    if any(v[2] != 851971 for v in samples[6]):
        raise ValueError("healthy Follow parent was completed")
    for i in (4, 5):
        marker = next((v for v, label in zip(values, labels) if v[1] == i and label == "callback_before"), None)
        if marker is None:
            raise ValueError("missing damage callback")
        tick = marker[0]
        expected_source = marker[8] if i == 4 else marker[6]
        if marker[9] != expected_source:
            raise ValueError("damage callback source differs")
        if actions[(tick, i, "callback_removed")][2] != (0 if i == 4 else 851971):
            raise ValueError("callback removal head differs")
        for label in ("nested_after", "callback_after"):
            if actions[(tick, i, label)][2] != 851986:
                raise ValueError("old owner overwrote nested replacement")
        if samples[i][-1][2] != 0:
            raise ValueError("nested replacement never completed")
    sources = [r for r in rows if r.get("event") == "follow-loss-source"]
    owners = [r for r in rows if r.get("event") == "follow-loss-owner"]
    callers = {r["callers"][0] for r in sources}
    if not {0x688373, 0x679c2f}.issubset(callers) or len(owners) != 3:
        raise ValueError("missing original removal/death caller or Follow handler")
    if any(r["retainedAfter"] != [0xffffffff, 0xffffffff] or r["after"]["publicHead"] != [0xffffffff, 0xffffffff] for r in owners):
        raise ValueError("Move loss failed to retire target/head")
    return {"records": len(values), "public_words_sha256": digest(values), "labels_sha256": digest(labels),
            "motion_commits": sum(r.get("event") == "velocity-commit" for r in rows),
            "source_events": len(sources), "owner_events": len(owners)}


def verify(raw, fixture, cap):
    if hashlib.sha256(raw).hexdigest() != cap["sha256"] or len(raw) != cap["bytes"]:
        raise ValueError("Follow capture hash/length differs")
    rows = [json.loads(line) for line in raw.splitlines()]
    metadata = [r for r in rows if r.get("event") == "metadata"]
    ends = [r for r in rows if r.get("event") == "trace-end"]
    if len(metadata) != 1 or len(ends) != 1 or rows[-1] != ends[0] or not ends[0].get("installed") or any(
            r.get("event") == "trace-failed" or r.get("type") == "error" for r in rows):
        raise ValueError("incomplete/error Follow capture")
    if {k: v for k, v in metadata[0].items() if k not in ("event", "pid")} != cap["metadata"]:
        raise ValueError("Follow producer provenance differs")
    if metadata[0]["sha256"] != HASH or metadata[0]["owned"] is not True:
        raise ValueError("requires owned mapped retail build")
    if [int(r["value"].split()[1][5:]) for r in rows if r.get("event") == "marker"] != list(range(201)):
        raise ValueError("Follow simulation extent differs")
    if sum(r.get("event") == "metadata-marker" and r.get("value") == "PATHMETA complete" for r in rows) != 1:
        raise ValueError("missing Follow completion")
    result = contract(rows)
    if result != cap["result"]:
        raise ValueError("frozen Follow contract differs")
    if fixture["whole_retail_pathfinder"] is not False or fixture["combat_motion_parity"] is not False:
        raise ValueError("Follow witness overclaims combat trajectory")
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--fixture", type=Path, required=True)
    p.add_argument("--capture", type=Path, action="append")
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    fixture = json.loads(args.fixture.read_text())
    if fixture.get("version") != 1 or len(fixture["captures"]) != 2:
        p.error("requires two complete Follow repeats")
    results = []
    for i, cap in enumerate(fixture["captures"]):
        path = args.capture[i] if args.capture else args.fixture.parent / cap["archive"]
        raw = gzip.decompress(path.read_bytes()) if path.suffix == ".gz" else path.read_bytes()
        results.append(verify(raw, fixture, cap))
    if any(results[0][key] != results[1][key] for key in ("records", "public_words_sha256", "labels_sha256")):
        raise ValueError("Follow public simulation repeat differs")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"status": "verified", "passed": True, "captures": len(results), "records": sum(r["records"] for r in results), "results": results}, indent=2) + "\n")


if __name__ == "__main__":
    main()
