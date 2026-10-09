#!/usr/bin/env python3
"""Verify actual point Move range publication, predicted arrival and final stop."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_motion_trace import verify as verify_motion

GAME_SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def words(value, count):
    if not isinstance(value, list) or len(value) != count or any(type(w) is not int or not 0 <= w <= 0xffffffff for w in value):
        raise ValueError('invalid arrival raw words')
    return value


def verify(rows, engine, fixture):
    if any(r.get('type') == 'error' for r in rows):
        raise ValueError('arrival observer error')
    metadata = [r for r in rows if r.get('event') == 'metadata']
    ending = [r for r in rows if r.get('event') == 'trace-end']
    if len(metadata) != 1 or metadata[0].get('sha256') != GAME_SHA or metadata[0].get('source_sha256') != fixture['source_sha256']:
        raise ValueError('arrival source/target metadata differs')
    if not all(metadata[0].get(k) for k in ('owned', 'motionEvents', 'velocityEvents', 'taskEvents')):
        raise ValueError('arrival observer options missing')
    if len(ending) != 1 or not ending[0].get('installed'):
        raise ValueError('arrival completion missing')
    selected = {}
    for kind in ('arrival-input', 'arrival-range', 'arrival-evaluation', 'velocity-commit'):
        selected[kind] = [r for r in rows if r.get('event') == kind]
        if not selected[kind] or len(selected[kind]) != ending[0].get('counts', {}).get(kind):
            raise ValueError('missing/truncated ' + kind)
    inputs, ranges = selected['arrival-input'], selected['arrival-range']
    if len(inputs) != 1 or inputs[0].get('kind') != 'point' or inputs[0].get('rawcode') != fixture['rawcode'] or inputs[0].get('worldRange') != 0:
        raise ValueError('actual zero-range point command missing')
    words(inputs[0].get('identity'), 2)
    if len(ranges) != 1 or ranges[0].get('value') != fixture['range'] or ranges[0].get('after') != fixture['range']:
        raise ValueError('minimum arrival range publication differs')
    mover = ranges[0].get('mover')
    if not mover or mover == '0x0':
        raise ValueError('arrival mover identity missing')
    evaluations, commits = selected['arrival-evaluation'], selected['velocity-commit']
    if len(evaluations) != len(commits) or len(evaluations) < 2:
        raise ValueError('arrival/commit pairing incomplete')
    # Preserve event order, including the final no-motion-decision stop.
    pairs = [r['event'] for r in rows if r.get('event') in ('arrival-evaluation', 'velocity-commit')]
    if pairs != ['arrival-evaluation', 'velocity-commit'] * len(commits):
        raise ValueError('arrival/commit order differs')
    normalized = []
    for index, (a, c) in enumerate(zip(evaluations, commits)):
        if a.get('mover') != mover or c.get('mover') != mover:
            raise ValueError('arrival mover changed')
        source, target = words(a.get('source'), 2), words(a.get('destination'), 2)
        stored = words(a.get('storedPosition'), 2)
        before, after = words(c.get('before'), 8), words(c.get('after'), 8)
        inp = words([*source, *target, a.get('heading'), a.get('threshold'), a.get('flags')], 7)
        if inp[5] != fixture['range'] or a.get('storedRange') != fixture['range'] or inp[6] != 0:
            raise ValueError('produced point range/force inputs changed')
        if stored != before[2:4] or source != after[2:4]:
            raise ValueError('arrival did not evaluate the committed predicted pose')
        output = (ctypes.c_uint32 * 4)()
        engine.pathing_arrival((ctypes.c_uint32 * 7)(*inp), output)
        if list(output)[1:] != [a.get('angle'), a.get('inRange'), a.get('result')]:
            raise ValueError('C arrival differs from retail')
        if a['result'] != int(index == len(evaluations) - 1):
            raise ValueError('point order lacks one terminal natural arrival')
        normalized.append([*inp, *list(output)])
    final = commits[-1]
    if not any(w & 0x7fffffff for w in final['before'][4:6]) or any(w & 0x7fffffff for w in final['after'][4:6]):
        raise ValueError('final old-velocity/zero-velocity handoff missing')
    if final['before'][2:4] == final['after'][2:4] or final['after'][2:4] == evaluations[-1]['destination']:
        raise ValueError('arrival snapped or failed to advance the previous velocity')
    result = verify_motion(rows, engine, 'open')
    result.update(binary_sha256=GAME_SHA, public_point_inputs=1, range_publications=1,
                  arrival_evaluations=len(evaluations), final_old_velocity_stops=1,
                  arrival_sha256=hashlib.sha256(json.dumps(normalized, separators=(',', ':')).encode()).hexdigest(),
                  scope='Actual zero-range point Move range, all predicate inputs and old-velocity final stop; engine cadence/world-to-grid parity excluded')
    return result


def configure(engine):
    for name in ('motion', 'velocity_commit', 'integrate'):
        getattr(engine, 'pathing_' + name).argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    engine.pathing_arrival.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--fixture', required=True, type=Path)
    parser.add_argument('--engine-library', required=True, type=Path)
    parser.add_argument('--compare', type=Path)
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    engine = ctypes.CDLL(str(args.engine_library.resolve()))
    configure(engine)
    fixture = json.loads(args.fixture.read_text())
    def capture(path):
        return verify([json.loads(line) for line in path.read_text().splitlines()], engine, fixture)
    result = capture(args.trace)
    if args.compare:
        repeat = capture(args.compare)
        for key in ('arrival_sha256', 'velocity_sha256', 'decision_sha256'):
            if result[key] != repeat[key]:
                raise ValueError('repeat ' + key + ' differs')
        result['repeated'] = True
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
