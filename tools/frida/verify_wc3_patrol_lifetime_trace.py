#!/usr/bin/env python3
"""Verify original Patrol ownership and ordered endpoint continuation witnesses."""
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
    if len(exported) != 17070 or [(r["parent"], r["child"]) for r in exported] != [
            (i, j) for i in range(1707) for j in range(10)]:
        raise ValueError("incomplete Patrol matrix")
    kinds = ["integer"] * 3 + ["real"] * 3 + ["integer"] * 4
    if any(r["kind"] != kinds[r["child"]] or type(r["word"]) is not int or
           not 0 <= r["word"] <= 0xffffffff for r in exported):
        raise ValueError("Patrol export words/types differ")
    values = [[r["word"] for r in exported[i:i+10]] for i in range(0, len(exported), 10)]
    labels = []
    for r in rows:
        if r.get("event") != "metadata-marker" or " row=" not in r.get("value", ""):
            continue
        m = re.fullmatch(r"PATHMETA patrol_lifetime row=(\d+) tick=(\d+) case=(\d+) label=(\w+)", r["value"])
        if not m or int(m[1]) != len(labels) or values[len(labels)][:2] != [int(m[2]), int(m[3])]:
            raise ValueError("Patrol label/order differs")
        labels.append(m[4])
    if len(labels) != len(values):
        raise ValueError("missing Patrol labels")
    samples = [[v for v, label in zip(values, labels) if v[1] == i and label == "sample"] for i in range(7)]
    if any([v[0] for v in sample] != list(range(1, 241)) for sample in samples):
        raise ValueError("incomplete Patrol subject journey")
    actions = {(v[0], v[1], label): v for v, label in zip(values, labels) if label != "sample"}
    if sum(r.get("event") == "metadata-interception" and r.get("command") == 851990 for r in rows) != 7:
        raise ValueError("missing original issued Patrol command")
    for i in range(7):
        if actions[(1, i, "patrol")][2] != 851991:
            raise ValueError("Patrol active head is not the two-point order")
    for i in (0, 1, 5, 6):
        if any(v[2] != 851991 for v in samples[i]):
            raise ValueError("Patrol parent completed during endpoint/combat progression")
    for i in (1, 2, 4):
        damaged = [v for v in samples[i] if 10 < v[0] < 45 and v[8]]
        if not damaged or any(v[8] != v[6] for v in damaged):
            raise ValueError("Patrol actual combat/source absent")
        if actions[(45, i, "loss_after")][2] != 851991:
            raise ValueError("enemy loss completed Patrol parent")
    if actions[(30, 4, "reject_refused")][2] != 851991:
        raise ValueError("rejected Repair replaced Patrol combat")
    if actions[(70, 4, "stop")][2] or any(v[2] for v in samples[4] if v[0] >= 70):
        raise ValueError("Stop failed to retire Patrol")
    if actions[(81, 2, "replacement")][2] or any(v[2] for v in samples[2] if v[0] >= 81):
        raise ValueError("replacement subject adopted old Patrol")
    nested = next((v for v, label in zip(values, labels) if v[1] == 3 and label == "nested_before"), None)
    if nested is None or nested[8] != nested[6] or nested[2] != 851991:
        raise ValueError("missing actual Patrol damage callback")
    if actions[(nested[0], 3, "nested_after")][2] != 851986 or samples[3][-1][2]:
        raise ValueError("old Patrol overwrote nested Move")

    complete = next(i for i, r in enumerate(rows) if r.get("event") == "metadata-marker" and r.get("value") == "PATHMETA complete")
    prefix = rows[:complete+1]
    creators = {tuple(r["order"]["identity"]): r for r in prefix if r.get("event") == "patrol-create-tasks"}
    continuations = [r for r in prefix if r.get("event") == "patrol-continuation-task"]
    appends = [r for r in prefix if r.get("event") == "patrol-append"]
    if len(creators) != 40 or len(continuations) != 33 or len(appends) != 33:
        raise ValueError("incomplete ordered Patrol continuation")
    normalized = []
    for continuation, append in zip(continuations, appends):
        before, after = continuation["before"], continuation["after"]
        creator = creators.get(tuple(before["publicHead"]))
        if creator is None or continuation["eventCode"] != 0xd0175 or (
                continuation["primary"] != creator["order"]["continuation"] or
                continuation["continuation"] != creator["order"]["primary"]):
            raise ValueError("Patrol continuation did not swap authored endpoints")
        if before != append["before"] or after != append["after"] or append["caller"] != 0x5fffe8 or (
                append["order"]["command"] != 851991 or
                append["order"]["primary"] != continuation["primary"] or
                append["order"]["continuation"] != continuation["continuation"] or
                after["publicHead"] != before["publicHead"] or
                after["publicCount"] != before["publicCount"] + 1 or
                after["publicTail"] != append["order"]["identity"]):
            raise ValueError("Patrol continuation bypassed append ordering")
        normalized.append([before["identity"], continuation["primary"], continuation["continuation"],
                           before["publicHead"], before["publicCount"], after["publicHead"], after["publicCount"]])
    blocked_unit = creators[next(k for k, r in creators.items() if r["order"]["primary"][1] == 0x44c00000)]["before"]["unit"]
    if not any(r.get("event") == "task-cant-path" and r["before"]["unit"] == blocked_unit for r in prefix):
        raise ValueError("missing blocked Patrol recovery")
    return {"records": len(values), "public_words_sha256": digest(values), "labels_sha256": digest(labels),
            "continuations": len(continuations), "continuations_sha256": digest(normalized)}


def verify(raw, fixture, cap):
    if hashlib.sha256(raw).hexdigest() != cap["sha256"] or len(raw) != cap["bytes"]:
        raise ValueError("Patrol capture hash/length differs")
    rows = [json.loads(line) for line in raw.splitlines()]
    metadata = [r for r in rows if r.get("event") == "metadata"]
    ends = [r for r in rows if r.get("event") == "trace-end"]
    if len(metadata) != 1 or len(ends) != 1 or rows[-1] != ends[0] or not ends[0].get("installed") or any(
            r.get("event") == "trace-failed" or r.get("type") == "error" for r in rows):
        raise ValueError("incomplete/error Patrol capture")
    if {k: v for k, v in metadata[0].items() if k not in ("event", "pid")} != cap["metadata"]:
        raise ValueError("Patrol producer provenance differs")
    if metadata[0]["sha256"] != HASH or metadata[0]["owned"] is not True:
        raise ValueError("requires owned mapped retail build")
    if [int(r["value"].split()[1][5:]) for r in rows if r.get("event") == "marker"] != list(range(241)):
        raise ValueError("Patrol simulation extent differs")
    if sum(r.get("event") == "metadata-marker" and r.get("value") == "PATHMETA complete" for r in rows) != 1:
        raise ValueError("missing Patrol completion")
    result = contract(rows)
    if result != cap["result"]:
        raise ValueError("frozen Patrol contract differs")
    if fixture["whole_retail_pathfinder"] is not False or fixture["combat_motion_parity"] is not False:
        raise ValueError("Patrol witness overclaims combat trajectory")
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--fixture", type=Path, required=True)
    p.add_argument("--capture", type=Path, action="append")
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    fixture = json.loads(args.fixture.read_text())
    if fixture.get("version") != 1 or len(fixture["captures"]) != 2:
        p.error("requires two complete Patrol repeats")
    results = []
    for i, cap in enumerate(fixture["captures"]):
        path = args.capture[i] if args.capture else args.fixture.parent / cap["archive"]
        raw = gzip.decompress(path.read_bytes()) if path.suffix == ".gz" else path.read_bytes()
        results.append(verify(raw, fixture, cap))
    if results[0] != results[1]:
        raise ValueError("Patrol public/continuation repeat differs")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"status": "verified", "passed": True, "captures": len(results),
        "records": sum(r["records"] for r in results), "results": results}, indent=2) + "\n")


if __name__ == "__main__":
    main()
