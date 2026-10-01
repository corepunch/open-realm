#!/usr/bin/env python3
"""Verify three public oblique lifetimes and their group destination handoffs."""
import argparse
import ctypes
import copy
import hashlib
import json
import struct
from pathlib import Path
from verify_wc3_arrival_trace import configure, words
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary
from verify_wc3_spawn_phase_trace import verify_births


CASES = ('oblique_base', 'oblique_small', 'oblique_large')


def journeys(rows):
    result = {case: [] for case in CASES}
    current = None
    labels = []
    for row in rows:
        if row.get('event') == 'position-marker' and row.get('value', '').startswith('PATHPOSE case='):
            current = row['value'].split('=', 1)[1]
            labels.append(current)
        if current in result:
            result[current].append(row)
    if labels != list(CASES):
        raise ValueError('oblique public lifetime order differs')
    return result


def digest(rows):
    result = []
    for case, life in journeys(rows).items():
        result.append([case, [{k: v for k, v in row.items() if k not in ('ms', 'mover', 'fineObject')}
            for row in life if row.get('event') in ('arrival-evaluation', 'velocity-commit')]])
    return hashlib.sha256(json.dumps(result, separators=(',', ':')).encode()).hexdigest()


def verify_lifetimes(rows, engine, fixture):
    result = {}
    for case, life in journeys(rows).items():
        expected = fixture['cases'][case]
        select = lambda event: [r for r in life if r.get('event') == event]
        positions, inputs, ranges = select('position-commit'), select('arrival-input'), select('arrival-range')
        arrivals, commits, decisions = select('arrival-evaluation'), select('velocity-commit'), select('motion-decision')
        if len(positions) != 2 or len(inputs) != 1 or len(ranges) != 1 or len(commits) != expected['commits'] or len(arrivals) != len(commits):
            raise ValueError('oblique public producer/commit count differs')
        mover = positions[-1].get('mover')
        if (inputs[0].get('kind') != 'point' or inputs[0].get('rawcode') != fixture['rawcode'] or inputs[0].get('worldRange') != 0
                or ranges[0].get('mover') != mover or ranges[0].get('value') != fixture['range'] or ranges[0].get('after') != fixture['range']):
            raise ValueError('oblique public point/range admission differs')
        if positions[0].get('input')[:2] != expected['public_source'] or positions[-1].get('after')[2:4] != expected['initial_fine']:
            raise ValueError('oblique public CreateUnit geometry differs')
        destinations = [r['destination'] for r in life if r.get('event') == 'path-destination' and r.get('caller') == '0x16fce4']
        if destinations != expected['group_destinations']:
            raise ValueError('oblique group destination handoff differs')
        sequence = [r['event'] for r in life if r.get('event') in ('arrival-evaluation', 'motion-decision', 'velocity-commit')]
        wanted = []
        for index in range(len(commits)):
            wanted.append('arrival-evaluation')
            if index not in expected['stops']:
                wanted.append('motion-decision')
            wanted.append('velocity-commit')
        if sequence != wanted or len(decisions) != len(commits) - len(expected['stops']):
            raise ValueError('oblique intermediate/terminal stop sequence differs')
        previous = positions[-1]['after'][:]
        previous[6] = fixture['speed']  # Public SetUnitMoveSpeed follows CreateUnit.
        stage = 0
        for index, (arrival, commit) in enumerate(zip(arrivals, commits)):
            before, after = words(commit.get('before'), 8), words(commit.get('after'), 8)
            source, target = words(arrival.get('source'), 2), words(arrival.get('destination'), 2)
            if commit.get('mover') != mover or arrival.get('mover') != mover or before != previous:
                raise ValueError('oblique member lifetime/state chain differs')
            target_words = [struct.unpack('<I', struct.pack('<f', n))[0] for n in expected['group_destinations'][stage]]
            inp = words([*source, *target, arrival.get('heading'), arrival.get('threshold'), arrival.get('flags')], 7)
            if (target != target_words or source != after[2:4] or arrival.get('storedPosition') != before[2:4]
                    or inp[4] != before[7] or inp[5] != fixture['range'] or arrival.get('storedRange') != fixture['range'] or inp[6] != 0
                    or before[6] != fixture['speed']):
                raise ValueError('oblique predicted arrival destination/profile differs')
            output = (ctypes.c_uint32 * 4)()
            engine.pathing_arrival((ctypes.c_uint32 * 7)(*inp), output)
            if list(output)[1:] != [arrival.get('angle'), arrival.get('inRange'), arrival.get('result')]:
                raise ValueError('oblique C arrival differs')
            if arrival['result'] != int(index in expected['stops']):
                raise ValueError('oblique natural arrival handoff differs')
            if arrival['result']:
                if (not any(w & 0x7fffffff for w in before[4:6]) or any(w & 0x7fffffff for w in after[4:6])
                        or before[2:4] == after[2:4] or after[2:4] == target):
                    raise ValueError('oblique old-velocity natural stop differs')
                stage += 1
            previous = after
        if stage != len(destinations):
            raise ValueError('oblique final group stage incomplete')
        result[case] = {'commits': len(commits), 'natural_stops': expected['stops']}
    return result


def verify(rows, engine, fixture):
    metadata = [r for r in rows if r.get('event') == 'metadata']
    ending = [r for r in rows if r.get('event') == 'trace-end']
    if (len(metadata) != 1 or metadata[0].get('sha256') != fixture['binary_sha256']
            or metadata[0].get('source_sha256') != fixture['source_sha256']
            or not all(metadata[0].get(k) for k in ('owned', 'motionEvents', 'velocityEvents', 'clockEvents', 'profileEvents'))):
        raise ValueError('oblique provenance/options differ')
    if len(ending) != 1 or not ending[0].get('installed') or any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
        raise ValueError('oblique observer failed/incomplete')
    for event in ('position-commit', 'arrival-input', 'arrival-range', 'arrival-evaluation', 'velocity-commit', 'motion-decision',
                  'clock-source-begin', 'clock-source-end', 'clock-advance-begin', 'clock-advance-end', 'clock-owner-begin', 'clock-owner-end'):
        if sum(r.get('event') == event for r in rows) != ending[0].get('counts', {}).get(event):
            raise ValueError('oblique observer count differs: ' + event)
    result = verify_motion(rows, engine, None)
    result.update(verify_primary(rows, engine, fixture))
    result.update(verify_births(rows, fixture))
    result['public_births'] = 3
    result['cases'] = verify_lifetimes(rows, engine, fixture)
    motion_words = [[r['after'][i] for i in (0, 2, 3, 4, 5, 7)] for r in rows if r.get('event') == 'velocity-commit']
    motion_digest = hashlib.sha256(json.dumps(motion_words, separators=(',', ':')).encode()).hexdigest()
    if motion_digest != fixture['engine_motion_sha256']:
        raise ValueError('oblique engine fixture commit words differ')
    result['engine_motion_sha256'] = motion_digest
    result['journey_sha256'] = digest(rows)
    if result['journey_sha256'] != fixture['journey_sha256']:
        raise ValueError('oblique journey raw words differ')
    result.update(natural_stops=5, intermediate_stops=2,
        scope='Three ordinary public CreateUnit/Move lifetimes from zero primary clock, exact689 commits and two singleton intermediate destination handoffs. General crowds, other movement profiles and deadlines remain open.')
    return result


def verify_geometry(rows, fixture):
    meta = [r for r in rows if r.get('event') == 'metadata']
    wanted = fixture['terrain']['geometry_capture']
    if len(meta) != 1 or meta[0].get('source_sha256') != wanted['source_sha256'] or meta[0].get('sha256') != fixture['binary_sha256']:
        raise ValueError('oblique geometry provenance differs')
    snapshots = [r for r in rows if r.get('event') == 'oblique-geometry']
    if len(snapshots) != 1:
        raise ValueError('oblique geometry snapshot missing/duplicated')
    actual = copy.deepcopy({k: v for k, v in snapshots[0].items() if k != 'ms'})
    expected = dict(fixture['terrain']['geometry'])
    expected.pop('encoding')
    def pack(runs, count, allowed):
        if not isinstance(runs, list) or any(not isinstance(r, list) or len(r) != 2 or type(r[0]) is not int or not 0 < r[0] <= 65535 or r[1] not in allowed for r in runs):
            raise ValueError('oblique geometry run malformed')
        if sum(r[0] for r in runs) != count:
            raise ValueError('oblique geometry run extent differs')
        return b''.join(struct.pack('<HB', *r) for r in runs).hex()
    actual['terrainRuns'] = pack(actual['terrainRuns'], actual['width'] * actual['height'], (0, 2))
    actual['objectRuns'] = pack(actual['objectRuns'], actual['width'] * actual['height'], (0, 2))
    for hierarchy in actual['hierarchy']:
        hierarchy['runs'] = pack(hierarchy['runs'], hierarchy['width'] * hierarchy['height'], (0, 1, 2, 3))
    if actual != expected:
        raise ValueError('oblique scene geometry differs')
    return dict(geometry_verified=True, geometry_width=actual['width'], geometry_height=actual['height'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path); parser.add_argument('--repeat', type=Path)
    parser.add_argument('--geometry', type=Path)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--engine-library', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True); args = parser.parse_args()
    fixture = json.loads(args.fixture.read_text())
    engine = ctypes.CDLL(str(args.engine_library.resolve())); configure(engine)
    read = lambda p: [json.loads(s) for s in p.read_text().splitlines()]
    result = verify(read(args.trace), engine, fixture)
    if args.repeat:
        other = verify(read(args.repeat), engine, fixture)
        for key in ('primary_sha256', 'cases', 'birth_primary_advances', 'birth_phases', 'decision_sha256', 'velocity_sha256', 'journey_sha256'):
            if result[key] != other[key]: raise ValueError('oblique repeat differs: ' + key)
        result['repeated'] = True
    if args.geometry:
        rows = read(args.geometry)
        result.update(verify_geometry(rows, fixture))
        geometry_fixture = dict(fixture, source_sha256=fixture['terrain']['geometry_capture']['source_sha256'])
        other = verify(rows, engine, geometry_fixture)
        if any(result[key] != other[key] for key in ('primary_sha256', 'journey_sha256', 'cases')):
            raise ValueError('geometry observer changed original motion')
    args.report.write_text(json.dumps(result, indent=2) + '\n'); print(json.dumps(result, indent=2))


if __name__ == '__main__': main()
