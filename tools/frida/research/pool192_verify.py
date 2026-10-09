#!/usr/bin/env python3
"""Verify complete public point-task lifetimes and exact repeat/control markers."""
import argparse
import hashlib
import json
from pathlib import Path
from follow187_engine_fixture import capture


def timeline(rows):
    stream, states, growth, phase = [], {}, [], None
    for row in rows:
        event = row['event']
        if event == 'marker':
            stream.append(row)
            value = row['value']
            if ' label=move' in value and ' label=move-end' not in value: phase = value
            continue
        if event == 'growth':
            # SMemAlloc can also be called for a diagnostic name while inside
            # the allocator. Certify raw blocks only by grow flag AND exact size.
            if not row['grow'] or row['bytes'] != row['size'] * row['block'] + 4: continue
            if row['flags'] != 0: raise ValueError('raw pool allocation flags differ')
            if row['kind'] == 'task' and (row['size'], row['block']) != (0x50, 64):
                raise ValueError('task pool shape differs')
            if row['kind'] == 'wrapper' and (row['size'], row['block']) != (0xbc, 512):
                raise ValueError('wrapper pool shape differs')
            if row['kind'] not in ('task', 'wrapper'): raise ValueError('unexpected public point pool')
            growth.append((phase, row)); stream.append(row); continue
        if event not in ('construct', 'bind', 'reclaim', 'wrapper-return'): continue
        stream.append(row)
        ident = row['id']
        if row['kind'] != 'task': raise ValueError('public native created another point class')
        if event == 'construct':
            if ident != len(states) + 1 or row['refs'] != 0 or row['identity'] != [0xffffffff] * 2:
                raise ValueError('invalid fresh construction')
            if row['poolLive'] != (ident - 1) % 129 + 1: raise ValueError('public task pool lifetime differs')
            states[ident] = event
        else:
            previous = {'bind': 'construct', 'reclaim': 'bind', 'wrapper-return': 'reclaim'}[event]
            if states.get(ident) != previous: raise ValueError('missing, duplicate or reordered lifetime boundary')
            states[ident] = event
            if event == 'reclaim' and (row['refs'] != 0 or row['identity'] != [0xffffffff] * 2):
                raise ValueError('payload returned before final reference/identity release')
            if event == 'wrapper-return' and (not row['ownedNull'] or row['identity'] != [0xffffffff] * 2):
                raise ValueError('wrapper returned with live ownership')
    if len(states) != 258 or any(s != 'wrapper-return' for s in states.values()):
        raise ValueError('incomplete public point lifetime')
    if sum(row['kind'] == 'task' for _, row in growth) != 2:
        raise ValueError('public task pool did not cross both capacity boundaries')
    if sum(row['kind'] == 'wrapper' for _, row in growth) != 5:
        raise ValueError('public wrapper block growth differs')
    if any(row['kind'] == 'task' and phase and 'tick=30' in phase for phase, row in growth):
        raise ValueError('second task burst allocated instead of reusing')
    ends = [r for r in rows if r['event'] == 'trace-end']
    if len(ends) != 1 or ends[0].get('livePayloads') != 0 or ends[0].get('liveWrappers') != 0:
        raise ValueError('observer retained an incomplete lifetime')
    counts = ends[0].get('counts', {})
    for event, expected in [('construct', 258), ('bind', 258), ('reclaim', 258), ('wrapper-return', 258), ('marker', 270)]:
        if counts.get(event) != expected: raise ValueError('observer event count differs')
    return stream


def verify(expected, archive):
    observations, public = [], []
    for name, pin in expected['captures'].items():
        rows, markers, mode = capture(archive / name, pin, expected['binary_sha256'], 'P192 ', 270)
        public.append(markers)
        if mode == 'observe': observations.append(timeline(rows))
    if len(observations) != 2 or observations[0] != observations[1]:
        raise ValueError('ordered public task lifetimes differ between repeats')
    if len(public) != 3 or any(p != public[0] for p in public):
        raise ValueError('observer perturbed public orders')
    raw = json.dumps(observations[0], separators=(',', ':')).encode()
    if hashlib.sha256(raw).hexdigest() != expected['timeline_sha256']:
        raise ValueError('frozen lifetime stream differs')
    return dict(passed=True, status='live-point-task-pool-lifetimes', observations=2, controls=1,
                public_markers=270, constructions=258, final_payload_releases=258,
                final_wrapper_returns=258, task_growth_blocks=2, wrapper_growth_blocks=5,
                second_burst_task_allocations=0)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for arg in ('expected', 'archive', 'output'): p.add_argument('--' + arg, type=Path, required=True)
    a = p.parse_args(); report = verify(json.loads(a.expected.read_text()), a.archive)
    a.output.write_text(json.dumps(report, indent=2) + '\n'); print(json.dumps(report))


if __name__ == '__main__': main()
