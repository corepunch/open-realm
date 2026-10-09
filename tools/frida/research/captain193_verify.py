#!/usr/bin/env python3
"""Verify preserved Captain-target ranges across two public point requests and Rhri.

This certifies the captured request/range contract, not full engine trajectories.
The observed repeats and independent public control must all complete.
"""
import argparse
import hashlib
import json
from pathlib import Path
from follow187_engine_fixture import capture

ROOT = Path(__file__).resolve().parents[3]


def unit(row):
    mover = row['mover']
    if not mover or 'error' in mover or 'error' in row:
        raise ValueError('missing live mover')
    attack = row['attackState']
    return dict(rawcode=row['rawcode'], head=row['head'], flags=row['flags5c'],
                mover={k: v for k, v in mover.items() if k != 'mover'},
                attack={k: v for k, v in attack.items() if k != 'attack'} if attack else None)


def timeline(rows):
    calls, points, snapshots = [], [], []
    for row in rows:
        event = row['event']
        if event == 'authored-range':
            calls.append(dict(rawcode=row['rawcode'], range=row['range'], trail=row['trail'],
                              flags=row['flags5c'], captain_flags=row['captainFlags']))
        if event == 'captain-point':
            points.append({k: row[k] for k in ('x', 'y', 'range')})
        if event == 'unit-snapshot' and (row['reason'] == 'captain-point-after' or
                'tick=11 ' in row['reason'] or ' label=complete' in row['reason']):
            snapshots.append(dict(reason=row['reason'], rows=sorted(map(unit, row['rows']), key=lambda u: u['rawcode'])))
    return validate_timeline(dict(calls=calls, points=points, snapshots=snapshots))


def validate_timeline(state):
    calls,points,snapshots=(state[k] for k in ('calls','points','snapshots'))
    if len(calls) != 15 or len({r['rawcode'] for r in calls}) != 15:
        raise ValueError('private ranges were refreshed or a recruit is missing')
    if len(points) != 4:
        raise ValueError('incomplete Captain point sequence')
    point_states = [r for r in snapshots if r['reason'] == 'captain-point-after']
    if len(point_states) != 4:
        raise ValueError('missing completed Captain request state')
    rifle = int.from_bytes(b'hRA9', 'big')
    before = next(r for r in point_states[1]['rows'] if r['rawcode'] == rifle)
    after = next(r for r in point_states[2]['rows'] if r['rawcode'] == rifle)
    if (before['attack']['ranges'][0], after['attack']['ranges'][0]) != (0x43c80000, 0x44160000):
        raise ValueError('Long Rifles did not change the actual slot range')
    if before['head'] != after['head'] or any(r['mover']['storedRange'] != 0x412b0000 for r in (before, after)):
        raise ValueError('Captain update replaced the private range snapshot')
    for old in point_states[1]['rows']:
        new = next(r for r in point_states[2]['rows'] if r['rawcode'] == old['rawcode'])
        if new['head'] != old['head'] or new['mover']['storedRange'] != old['mover']['storedRange']:
            raise ValueError('Captain update replaced a retained member request')
    return state


def verify(expected, archive):
    streams, public = [], []
    for name, pin in expected['captures'].items():
        rows, markers, mode = capture(archive / name, pin, expected['binary_sha256'], 'RSG ', 770)
        public.append(markers)
        if mode == 'observe': streams.append(timeline(rows))
    if len(streams) != 2 or streams[0] != streams[1]:
        raise ValueError('ordered retail range streams differ')
    if len(public) != 3 or any(p != public[0] for p in public):
        raise ValueError('observer perturbed the public scene')
    if streams[0] != expected['timeline']:
        raise ValueError('frozen Captain range stream differs')
    for name, digest in expected['sources'].items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest:
            raise ValueError('producer/observer source differs: ' + name)
    if hashlib.sha256((archive / 'RS-Captain193c.w3m').read_bytes()).hexdigest() != expected['map_sha256']:
        raise ValueError('retail map differs')
    return dict(passed=True, status='live-captain-retained-range', observations=2, controls=1,
                public_markers=770, private_admissions=15, point_requests=4,
                retained_member_heads=15, retained_rifle_range=0x412b0000,
                upgraded_weapon_range=0x44160000)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for arg in ('expected', 'archive', 'output'): p.add_argument('--' + arg, type=Path, required=True)
    a = p.parse_args()
    if a.output.exists(): p.error('report must be fresh')
    report = verify(json.loads(a.expected.read_text()), a.archive)
    a.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__': main()
