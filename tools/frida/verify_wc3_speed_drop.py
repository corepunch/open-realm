#!/usr/bin/env python3
"""Verify the actual public moving speed drop and its next velocity commit."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_speed_inputs import bits


def digest(rows):
    selected = [{k: v for k, v in r.items() if k not in ('ms', 'unit', 'handle', 'identity', 'mover')}
                for r in rows if r.get('event') in ('speed-marker', 'speed-native', 'speed-publication', 'speed-cap-change')]
    return hashlib.sha256(json.dumps(selected, separators=(',', ':')).encode()).hexdigest()


def verify(rows, engine, fixture):
    metadata = [r for r in rows if r.get('event') == 'metadata']
    ends = [r for r in rows if r.get('event') == 'trace-end']
    if any(r.get('type') == 'error' for r in rows): raise ValueError('speed-drop observer error')
    if len(metadata) != 1 or metadata[0].get('sha256') != fixture['binary_sha256'] or metadata[0].get('source_sha256') != fixture['source_sha256']:
        raise ValueError('speed-drop source/target differs')
    if not all(metadata[0].get(k) for k in ('owned', 'motionEvents', 'velocityEvents', 'taskEvents')):
        raise ValueError('speed-drop observer options missing')
    if len(ends) != 1 or not ends[0].get('installed'): raise ValueError('speed-drop completion missing')
    if digest(rows) != fixture['speed_sha256']: raise ValueError('speed-drop producer sequence differs')
    for kind, expected in (('speed-native', 6), ('speed-publication', 2), ('speed-cap-change', 2)):
        if sum(r.get('event') == kind for r in rows) != expected or ends[0].get('counts', {}).get(kind) != expected:
            raise ValueError('speed-drop missing/truncated ' + kind)
    markers = [re.fullmatch(r'PATHTRACE tick=(\d+) label=(\w+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)', r.get('value', ''))
               for r in rows if r.get('event') == 'marker']
    if any(m is None for m in markers): raise ValueError('speed-drop malformed marker')
    labels = [m[2] for m in markers]
    if any(labels.count(k) != 1 for k in ('start_speed_drop', 'order_accepted', 'complete')) or 'order_rejected' in labels:
        raise ValueError('speed-drop admission/completion missing')
    if [int(m[1]) for m in markers if m[2] == 'sample'] != list(range(1, 301)):
        raise ValueError('speed-drop samples incomplete')
    engine.pathing_speed_cap.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
    pubs = [r for r in rows if r.get('event') == 'speed-publication']
    identity = (pubs[0]['mover'], pubs[0]['identity'])
    actors = set()
    for r in (r for r in rows if r.get('event') == 'speed-native'):
        if r.get('rawcode') != fixture['rawcode'] or r.get('bounds') != fixture['bounds']:
            raise ValueError('speed-drop actor profile/bounds differ')
        actors.add((r.get('unit'), r.get('handle')))
        expected = 400 if r['case'] == 'foot_travel_high' else 150
        if r['name'] == 'GetUnitMoveSpeed' and r['output'] != bits(expected): raise ValueError('speed-drop effective getter differs')
        if r['name'] == 'GetUnitDefaultMoveSpeed' and r['output'] != bits(270): raise ValueError('speed-drop default changed')
    if len(actors) != 1 or not next(iter(actors))[1]: raise ValueError('speed-drop resolved actor changed')
    for index, r in enumerate(r for r in rows if r.get('event') == 'speed-cap-change'):
        pub = pubs[index]
        if (pub['mover'], pub['identity']) != identity or pub['rawcode'] != fixture['rawcode'] or r['mover'] != pub['mover'] or r['case'] != pub['case']:
            raise ValueError('speed-drop mover epoch changed')
        if pub['limit'] != r['value'] or pub['increment'] != r['value']: raise ValueError('speed-drop bridge cap differs')
        inputs = [*r['before'], *r['clock'], r['value'], r['fineFlagsBefore']]
        if len(inputs) != 13 or any(type(w) is not int or not 0 <= w <= 0xffffffff for w in inputs):
            raise ValueError('speed-drop raw state invalid')
        out = (ctypes.c_uint32 * 10)()
        engine.pathing_speed_cap((ctypes.c_uint32 * 13)(*inputs), out)
        if list(out) != [*r['after'], r['fineFlagsAfter'], index]: raise ValueError('speed-drop C transition differs')
        if index == 1 and (r['before'][:2] != r['clock'][:2] or r['after'][2:4] != r['before'][2:4] or r['after'][7] != r['before'][7]):
            raise ValueError('actual zero-elapsed low-cap pose/facing contract differs')
        pos = rows.index(r)
        commit = next(c for c in rows[pos + 1:] if c.get('event') == 'velocity-commit')
        if commit['mover'] != r['mover'] or commit['before'][4:8] != r['after'][4:8]:
            raise ValueError('speed-drop state did not reach the next commit')
    result = verify_motion(rows, engine, None)
    result.update(binary_sha256=fixture['binary_sha256'], public_speed_calls=6, speed_cap_changes=2,
                  immediate_zero_elapsed_clamps=1, speed_sha256=digest(rows),
                  scope='Actual public high/low setters and zero-elapsed vector clamp; engine nonzero elapsed owner-clock production remains excluded')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--compare', type=Path)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--engine-library', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    engine = ctypes.CDLL(str(args.engine_library.resolve())); configure(engine)
    fixture = json.loads(args.fixture.read_text())
    def capture(path): return verify([json.loads(s) for s in path.read_text().splitlines()], engine, fixture)
    result = capture(args.trace)
    if args.compare:
        repeat = capture(args.compare)
        for key in ('speed_sha256', 'decision_sha256', 'velocity_sha256'):
            if result[key] != repeat[key]: raise ValueError('speed-drop repeat ' + key + ' differs')
        result['repeated'] = True
    args.report.write_text(json.dumps(result, indent=2) + '\n'); print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
