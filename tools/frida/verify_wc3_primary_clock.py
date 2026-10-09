#!/usr/bin/env python3
"""Check the observed primary source/owner order against production scalar clocks."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re
from verify_wc3_motion_trace import verify as verify_motion


def primary(rows):
    result = []
    for row in rows:
        if not row.get('event', '').startswith('clock-') or row.get('source') == 'subdivide' or row.get('domain') == 104:
            continue
        value = {k: v for k, v in row.items() if k not in ('ms', 'serial')}
        for key in ('before', 'after', 'clock'):
            if key in value and value[key] and isinstance(value[key][0], list): value[key] = value[key][:1]
        result.append(value)
    return result


def digest(rows):
    return hashlib.sha256(json.dumps(primary(rows), separators=(',', ':')).encode()).hexdigest()


def verify_primary(rows, engine, fixture):
    records = primary(rows)
    engine.pathing_clock_advance.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
    advances = fixture.get('primary_advances', 6000)
    if type(advances) is not int or advances <= 0:
        raise ValueError('primary observation extent must be a positive integer')
    index = 0
    state = [0, 0, 0x43960000, 4096]
    deadline = [0x3cf5c290, 0, 0x43960000, 0]
    owners = 0
    def take(event):
        nonlocal index
        if index == len(records) or records[index].get('event') != event:
            raise ValueError('primary clock event order differs at ' + str(index))
        row = records[index]; index += 1
        return row
    for tick in range(advances + 1):
        source = take('clock-source-begin')
        if source.get('source') != 'direct' or source.get('input') != 0x3ba3d70a or source.get('maximum') != 0x43958000 or source.get('caller') != '0x36aba8' or source.get('before') != [state]:
            raise ValueError('primary source input/state differs')
        out = (ctypes.c_uint32 * 4)()
        engine.pathing_clock_advance((ctypes.c_uint32 * 5)(*state, 0x3ba3d70a), out)
        expected = list(out)[:3] + [4096]
        # Timer stage precedes request-clock publication. Its scalar deadline
        # accumulates the native period; integer six-tick cadence diverges later.
        due = (expected[1] > deadline[1] or
               (expected[1] == deadline[1] and expected[0] >= deadline[0]))
        if due:
            before = take('clock-owner-begin'); after = take('clock-owner-end')
            counter = fixture['owner_counter'] + owners
            if before.get('clock') != [state] or after.get('clock') != [state] or before.get('counter') != counter or after.get('counter') != counter + 1:
                raise ValueError('primary owner cadence/state differs')
            owners += 1
            timer_out = (ctypes.c_uint32 * 4)()
            engine.pathing_clock_advance((ctypes.c_uint32 * 5)(*deadline, 0x3cf5c290), timer_out)
            deadline = list(timer_out)[:3] + [0]
        if tick < advances:
            before = take('clock-advance-begin'); after = take('clock-advance-end')
            if before.get('domain') != 20 or before.get('input') != 0x3ba3d70a or before.get('caller') != '0x4f7ed' or before.get('before') != state or after.get('domain') != 20 or after.get('after') != expected or type(after.get('output')) is not int or after.get('output') != 1 or out[3]:
                raise ValueError('primary advance/C words differ')
        end = take('clock-source-end')
        if end.get('source') != 'direct' or end.get('after') != [expected]:
            raise ValueError('primary source completion differs')
        state = expected
    if index != len(records) or owners != fixture.get('owner_callbacks', advances // 6):
        raise ValueError('primary clock capture truncated or extended')
    observed = digest(rows)
    if observed != fixture['primary_sha256']: raise ValueError('primary clock sequence differs')
    return dict(primary_advances=advances, owner_callbacks=owners, completion_boundary_advances=1,
                primary_sha256=observed)


def verify(rows, engine, fixture):
    metadata = [r for r in rows if r.get('event') == 'metadata']
    ending = [r for r in rows if r.get('event') == 'trace-end']
    if len(metadata) != 1 or metadata[0].get('sha256') != fixture['binary_sha256'] or metadata[0].get('source_sha256') != fixture['source_sha256'] or not all(metadata[0].get(k) for k in ('owned', 'clockEvents', 'taskEvents', 'motionEvents', 'velocityEvents')):
        raise ValueError('primary clock provenance/options differ')
    if len(ending) != 1 or not ending[0].get('installed') or any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
        raise ValueError('primary clock observer failed/incomplete')
    for event in ('clock-source-begin', 'clock-source-end', 'clock-advance-begin', 'clock-advance-end', 'clock-owner-begin', 'clock-owner-end'):
        if sum(r.get('event') == event for r in rows) != ending[0].get('counts', {}).get(event):
            raise ValueError('primary clock observer count differs')
    marks = [re.fullmatch(r'PATHTRACE tick=(\d+) label=(\w+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)', r.get('value', '')) for r in rows if r.get('event') == 'marker']
    if any(m is None for m in marks) or [int(m[1]) for m in marks if m[2] == 'sample'] != list(range(1, 301)) or any(sum(m[2] == label for m in marks) != 1 for label in ('start_clock_oblique', 'order_accepted', 'complete')):
        raise ValueError('primary clock scenario samples/admission differ')
    result = verify_primary(rows, engine, fixture)
    result.update(verify_motion(rows, engine, None))
    result['scope'] = 'Observed primary source/owner clock and captured scalar commits; presentation host time, full owner routing and whole engine trajectories excluded.'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--repeat', type=Path)
    parser.add_argument('--engine-library', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    engine = ctypes.CDLL(str(args.engine_library.resolve()))
    for name in ('motion', 'velocity_commit', 'integrate'):
        getattr(engine, 'pathing_' + name).argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    fixture = json.loads(args.fixture.read_text())
    read = lambda path: [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    result = verify(read(args.trace), engine, fixture)
    if args.repeat:
        repeated = verify(read(args.repeat), engine, fixture)
        for key in ('primary_sha256', 'decision_sha256', 'velocity_sha256'):
            if result[key] != repeated[key]: raise ValueError('repeated primary clock/motion differs')
        result['repeat_verified'] = True
    result.update(trace_sha256=hashlib.sha256(args.trace.read_bytes()).hexdigest(),
                  engine_library_sha256=hashlib.sha256(args.engine_library.read_bytes()).hexdigest())
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))

if __name__ == '__main__': main()
