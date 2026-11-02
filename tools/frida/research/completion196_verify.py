#!/usr/bin/env python3
"""Validate blocked spell completion against repeated retail and an unhooked control."""
import argparse
import hashlib
import gzip
import json
from pathlib import Path
from follow187_engine_fixture import capture

ROOT = Path(__file__).resolve().parents[3]


def timeline(rows):
    result = []
    for row in rows:
        if row['event'] in ('metadata', 'installed', 'loading-key', 'loading-key-skipped',
                            'trace-end', 'controller-end', 'preload-file'):
            continue
        row = dict(row)
        if 'member' in row:
            row['member'] = list(row['member'])
            row['member'][5] = bool(row['member'][5])
        if row['event'] == 'cant-path':
            del row['ability']  # Process address; retain the event's exact position.
        result.append(row)
    blocked = [r for r in result if r['event'] == 'blocked']
    if len(blocked) != 1 or blocked[0]['c'] != 1129 or blocked[0]['partial'] != 0:
        raise ValueError('blocked completion differs')
    if any(r['event'] == 'arrival' for r in result):
        raise ValueError('unreachable spell reported arrival')
    events = [r for r in result if r['event'] == 'unit-completion-event']
    if len(events) != 1 or events[0]['c'] != 1129 or events[0]['code'] != 0x40190066:
        raise ValueError('wrong Unit completion event')
    if sum(r['event'] == 'cant-path' for r in result) != 1:
        raise ValueError('missing Move cant-path dispatch')
    if sum(r['event'] == 'owner-begin' for r in result) != 600:
        raise ValueError('incomplete owner lifetime')
    return result


def verify(expected, archive):
    observed, public = [], []
    for name, pin in expected['captures'].items():
        rows, markers, mode = capture(archive / name, pin, expected['binary_sha256'], 'C196 ', 186)
        public.append(markers)
        if mode == 'observe':
            observed.append(timeline(rows))
    if len(observed) != 2 or observed[0] != observed[1] or observed[0] != expected['timeline']:
        raise ValueError('retail completion streams differ')
    if len(public) != 3 or any(p != public[0] for p in public):
        raise ValueError('observer-free control differs')
    for source, digest in expected['sources'].items():
        if hashlib.sha256((ROOT / source).read_bytes()).hexdigest() != digest:
            raise ValueError('source differs: ' + source)
    if hashlib.sha256((archive / 'RS-Completion196.w3m').read_bytes()).hexdigest() != expected['map_sha256']:
        raise ValueError('map differs')
    return dict(passed=True, status='live-blocked-spell-completion', observations=2, controls=1,
                public_markers=186, owner_intervals=600, blocked_events=1, arrival_events=0,
                completion_counter=1129, binary_sha256=expected['binary_sha256'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('expected', 'archive', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    a = p.parse_args()
    if a.output.exists():
        p.error('report must be fresh')
    raw = a.expected.read_bytes()
    if a.expected.suffix == '.gz':
        raw = gzip.decompress(raw)
    report = verify(json.loads(raw), a.archive)
    a.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
