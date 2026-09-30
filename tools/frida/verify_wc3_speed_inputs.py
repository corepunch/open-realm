#!/usr/bin/env python3
"""Verify public speed setters/getters, authored bounds and moving publications."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ghidra'))
from verify_wc3_pathing_numeric import bits
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_arrival_trace import configure


def normalized(rows):
    return [{k: v for k, v in r.items() if k not in ('ms', 'unit', 'handle', 'identity', 'mover')}
            for r in rows if r.get('event') in ('speed-marker', 'speed-native', 'speed-publication')]


def digest(rows):
    return hashlib.sha256(json.dumps(normalized(rows), separators=(',', ':')).encode()).hexdigest()


def verify(rows, engine, fixture):
    metadata = [r for r in rows if r.get('event') == 'metadata']
    ends = [r for r in rows if r.get('event') == 'trace-end']
    if any(r.get('type') == 'error' for r in rows):
        raise ValueError('speed observer error')
    if len(metadata) != 1 or metadata[0].get('sha256') != fixture['binary_sha256'] or metadata[0].get('source_sha256') != fixture['source_sha256']:
        raise ValueError('speed source/target metadata differs')
    if not all(metadata[0].get(k) for k in ('owned', 'motionEvents', 'velocityEvents', 'taskEvents')):
        raise ValueError('speed observer options missing')
    if len(ends) != 1 or not ends[0].get('installed'):
        raise ValueError('speed completion missing')
    markers = [re.fullmatch(r'PATHTRACE tick=(\d+) label=(\w+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)', r.get('value', ''))
               for r in rows if r.get('event') == 'marker']
    if any(m is None for m in markers):
        raise ValueError('malformed speed scenario marker')
    labels = [m[2] for m in markers]
    if any(labels.count(label) != 1 for label in ('start_speed_inputs', 'order_accepted', 'complete')) or 'order_rejected' in labels:
        raise ValueError('speed scenario admission/completion missing')
    if [int(m[1]) for m in markers if m[2] == 'sample'] != list(range(1, 301)):
        raise ValueError('speed scenario samples incomplete')
    if digest(rows) != fixture['speed_sha256']:
        raise ValueError('public speed producer sequence differs')
    for kind, expected in (('speed-native', 120), ('speed-publication', 26)):
        observed = sum(r.get('event') == kind for r in rows)
        if observed != expected or ends[0].get('counts', {}).get(kind) != expected:
            raise ValueError('missing/truncated ' + kind)
    actors, movers, values = {}, {}, {}
    engine.pathing_speed_limits.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
    engine.pathing_divide.argtypes = [ctypes.c_uint32] * 2
    engine.pathing_divide.restype = ctypes.c_uint32
    for row in rows:
        kind = row.get('event')
        if kind not in ('speed-native', 'speed-publication'):
            continue
        profile = fixture['profiles'][str(row['rawcode'])]
        case = row['case']
        if kind == 'speed-native':
            actor = (row.get('unit'), row.get('handle'))
            if not actor[0] or actor[0] == '0x0' or not actor[1]:
                raise ValueError('speed resolved actor missing')
            previous = actors.setdefault(row['rawcode'], actor)
            if previous != actor or row.get('bounds') != fixture['bounds']:
                raise ValueError('speed actor/bounds changed')
            if row['name'] == 'SetUnitMoveSpeed':
                values[row['rawcode']] = row['input']
            elif row['name'] == 'GetUnitDefaultMoveSpeed':
                if row['output'] != profile['default']:
                    raise ValueError('default speed changed')
            else:
                inputs = [values.get(row['rawcode'], profile['default']), profile['minimum'],
                          profile['maximum'], fixture['bounds'][4], fixture['bounds'][5], profile['disabled']]
                out = (ctypes.c_uint32 * 3)()
                engine.pathing_speed_limits((ctypes.c_uint32 * 6)(*inputs), out)
                if row['output'] != out[0]:
                    raise ValueError('C effective speed differs')
        else:
            if profile['disabled'] or not row.get('mover') or row['mover'] == '0x0':
                raise ValueError('disabled/missing speed publication')
            identity = row.get('identity')
            if not isinstance(identity, list) or len(identity) != 2:
                raise ValueError('speed mover epoch missing')
            if movers.setdefault(row['rawcode'], (row['mover'], identity)) != (row['mover'], identity):
                raise ValueError('speed mover epoch changed')
            if row['globalCap'] != fixture['global_cap'] or row['limit'] != engine.pathing_divide(row['input'], bits(32)) or row['increment'] != row['limit']:
                raise ValueError('speed bridge conversion differs')
    # A live setter reaches the moving owner's cap; the next commit consumes it.
    for case, value in (('foot_travel_high', 400),):
        index = next(i for i, r in enumerate(rows) if r.get('event') == 'speed-publication' and r.get('case') == case)
        publication = rows[index]
        commit = next(r for r in rows[index + 1:] if r.get('event') == 'velocity-commit')
        if publication['input'] != bits(value) or commit['mover'] != publication['mover'] or commit['before'][6] != publication['limit']:
            raise ValueError('travel setter did not reach the committed mover')
    result = verify_motion(rows, engine, None)
    result.update(binary_sha256=fixture['binary_sha256'], public_speed_calls=120,
                  speed_publications=26, travel_speed_changes=1, speed_sha256=digest(rows),
                  scope='Nonhero public speed inputs, authored bounds and captured motion; special caps, hero defaults and engine clock cadence excluded')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--compare', type=Path)
    parser.add_argument('--fixture', required=True, type=Path)
    parser.add_argument('--engine-library', required=True, type=Path)
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    engine = ctypes.CDLL(str(args.engine_library.resolve()))
    configure(engine)
    fixture = json.loads(args.fixture.read_text())
    def capture(path):
        return verify([json.loads(s) for s in path.read_text().splitlines()], engine, fixture)
    result = capture(args.trace)
    if args.compare:
        repeat = capture(args.compare)
        for key in ('speed_sha256', 'decision_sha256', 'velocity_sha256'):
            if result[key] != repeat[key]:
                raise ValueError('repeat ' + key + ' differs')
        result['repeated'] = True
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
