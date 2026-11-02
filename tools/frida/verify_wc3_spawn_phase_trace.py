#!/usr/bin/env python3
"""Verify public spawn admission inside the observed primary clock producer."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_primary_clock import verify_primary
from verify_wc3_spawn_motion_trace import verify as verify_motion


def verify_births(rows, fixture):
    advances = owners = 0
    state = [0, 0, 0x43960000]
    active = None
    births = []
    for row in rows:
        event = row.get('event')
        if event == 'clock-source-begin' and row.get('source') == 'direct':
            active = row['before'][0][:3]
        elif event == 'clock-source-end' and row.get('source') == 'direct':
            active = None
        elif event == 'clock-advance-end' and row.get('domain') == 20:
            advances += 1; state = row['after'][:3]
        elif event == 'clock-owner-end':
            owners += 1
        elif event == 'position-commit':
            birth = dict(case=row.get('case'), primary_advances=advances,
                         owner_updates=owners, clock=row.get('clock'), phase=advances % 6)
            if active != state or birth['clock'] != state:
                raise ValueError('spawn admission outside primary timer dispatch')
            births.append(birth)
    if births != [birth for birth in fixture['births'] for _ in range(2)]:
        raise ValueError('spawn callback admission phase differs')
    return dict(public_births=8, birth_phases=[r['phase'] for r in fixture['births']],
                birth_primary_advances=[r['primary_advances'] for r in fixture['births']])


def verify(rows, engine, fixture, motion):
    result = verify_motion(rows, engine, motion)
    meta = next(r for r in rows if r.get('event') == 'metadata')
    end = next(r for r in rows if r.get('event') == 'trace-end')
    if not meta.get('clockEvents'):
        raise ValueError('spawn phase clock observer missing')
    for event in ('clock-source-begin', 'clock-source-end', 'clock-advance-begin',
                  'clock-advance-end', 'clock-owner-begin', 'clock-owner-end'):
        if sum(r.get('event') == event for r in rows) != end.get('counts', {}).get(event):
            raise ValueError('spawn phase clock observer count differs')
    result.update(verify_primary(rows, engine, fixture))
    result.update(verify_births(rows, fixture))
    result['scope'] = ('Observed zero-clock primary producer, eight periodic public spawn-to-Move admissions '
                       'and247 scalar commits. General timer deadlines, other profiles and crowds remain open.')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path); parser.add_argument('--repeat', type=Path)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--engine-library', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True); args = parser.parse_args()
    fixture = json.loads(args.fixture.read_text())
    motion_path = args.fixture.parent / fixture['motion_fixture']
    if hashlib.sha256(motion_path.read_bytes()).hexdigest() != fixture['motion_fixture_sha256']:
        raise ValueError('spawn phase motion fixture differs')
    motion = json.loads(motion_path.read_text())
    engine = ctypes.CDLL(str(args.engine_library.resolve())); configure(engine)
    read = lambda p: [json.loads(s) for s in p.read_text().splitlines()]
    result = verify(read(args.trace), engine, fixture, motion)
    if args.repeat:
        other = verify(read(args.repeat), engine, fixture, motion)
        for key in ('primary_sha256', 'birth_primary_advances', 'birth_phases', 'spawn_sha256',
                    'decision_sha256', 'velocity_sha256', 'journey_sha256'):
            if result[key] != other[key]: raise ValueError('spawn phase repeat differs')
        result['repeated'] = True
    args.report.write_text(json.dumps(result, indent=2) + '\n'); print(json.dumps(result, indent=2))


if __name__ == '__main__': main()
