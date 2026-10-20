#!/usr/bin/env python3
"""Verify bounded public outside-axis setter captures and observer-free control.

Only setter reachability is certified. Prior movement samples can vary with
observer timing; this does not certify movement from an outside source.
"""
import argparse
import gzip
import hashlib
import json
import re
import struct
from pathlib import Path

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
TARGETS = [(-112.0, 328.0), (2248.0, 328.0), (328.0, -16.0)]


def read(path):
    data = Path(path).read_bytes()
    return gzip.decompress(data) if str(path).endswith('.gz') else data


def validate_run(path, mode):
    path = Path(path)
    rows = [json.loads(line) for line in read(path).splitlines()]
    metadata = [r for r in rows if r['event'] == 'metadata']
    assert len(metadata) == 1
    metadata = metadata[0]
    assert metadata['mode'] == mode and metadata['sha256'] == SHA and metadata['owned']
    assert not any(r['event'] in ('error', 'trace-failed') for r in rows)
    stem = path.name.removesuffix('.gz').removesuffix('.jsonl')
    preload = path.with_name(stem + '-preload.txt' + ('.gz' if path.suffix == '.gz' else ''))
    raw = read(preload)
    generated = [r for r in rows if r['event'] == 'preload-file']
    assert len(generated) == 1 and generated[0]['complete']
    assert hashlib.sha256(raw).hexdigest() == generated[0]['sha256']
    markers = re.findall(r'call Preload\( "(ROUTE012 [^"\r\n]*)" \)', raw.decode())
    assert len(markers) == generated[0]['markers'] == 137
    endpoints = []
    for marker in markers:
        match = re.fullmatch(r'ROUTE012 tick=(\d+) label=([^ ]+) x=([^ ]+) y=([^ ]+) order=(\d+)', marker)
        assert match, marker
        tick, label, x, y, order = match.groups()
        if label not in ('after-outside', 'outside-held', 'complete'):
            continue
        tick = int(tick)
        phase = min(2, tick // 40)
        point = (float(x), float(y))
        assert point == TARGETS[phase] and int(order) == 0
        fine_words = [struct.unpack('<I', struct.pack('<f', v / 32))[0] for v in point]
        endpoints.append(dict(tick=tick, label=label, world=list(point), fine_words=fine_words))
    assert [e['tick'] for e in endpoints] == [12, 13, 52, 53, 92, 93, 120]
    if mode == 'observe':
        observed = [r['value'] for r in rows if r['event'] == 'marker']
        assert observed == markers
        ends = [r for r in rows if r['event'] == 'trace-end']
        assert len(ends) == 1 and ends[0]['installed']
    else:
        assert not any(r['event'] in ('module', 'marker', 'advance', 'fine-setup', 'coarse-setup', 'trace-end') for r in rows)
    return dict(mode=mode, markers=len(markers), complete=True, endpoints=endpoints)


def summarize(first, repeat, control):
    runs = [validate_run(path, mode) for path, mode in [(first, 'observe'), (repeat, 'observe'), (control, 'control')]]
    assert all(r['endpoints'] == runs[0]['endpoints'] for r in runs)
    return dict(version=1, binary_sha256=SHA, public_outside_sources=3, completing_runs=3,
                observer_free_controls=1, runs=runs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ('first', 'repeat', 'control', 'report'):
        parser.add_argument('--' + flag, type=Path, required=True)
    parser.add_argument('--expected', type=Path)
    args = parser.parse_args()
    payload = summarize(args.first, args.repeat, args.control)
    if args.expected:
        assert payload == json.loads(args.expected.read_text())
    report = dict(passed=True, **payload)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('passed', 'public_outside_sources', 'completing_runs', 'observer_free_controls')}))


if __name__ == '__main__':
    main()
