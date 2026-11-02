#!/usr/bin/env python3
"""Verify largest-recruit Stop before/after mixed13 shared admission."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_blocked_goal_trace import lifecycle
from verify_wc3_captain_approach_trace import births, physical_motion
from verify_wc3_captain_home_trace import producer
from verify_wc3_captain_pair_trace import admission
from verify_wc3_captain_range_trace import range_timeline
from verify_wc3_captain_shared_trace import shared_state
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary


def digest(value):
    return hashlib.sha256(json.dumps(value, separators=(',', ':')).encode()).hexdigest()


def canonical_lifecycle(rows):
    movers = births(rows)
    captain = [r for r in rows if r.get('event') == 'movement-mask-publication' and
               r['category'] == 2 and r['rawcode'] == 0]
    if len(captain) != 1 or captain[0]['mover'] in movers:
        raise ValueError('captain fine-target owner is missing or aliases a recruit')
    owners = {m: i for i, m in enumerate(movers)}
    owners[captain[0]['mover']] = -1
    fine = {}
    for r in rows:
        if r.get('mover') not in owners:
            continue
        pointer = r.get('fineObject') if r.get('event') == 'velocity-commit' else \
                  r.get('fine') if r.get('event') == 'mover-radius-state' else None
        if not pointer or pointer == '0x0':
            continue
        address = int(pointer, 16)
        owner = owners[r['mover']]
        if address in fine and fine[address] != owner:
            raise ValueError('fine target pointer aliases another canonical mover')
        fine[address] = owner
    result = lifecycle(rows)
    for r in result:
        if r['event'] != 'retry-result':
            continue
        # Original166310 dereferences path+a4 as a fine record (+38 live slot).
        # The following a8 word belongs to blocker identity, not this pointer.
        pointer, blocker_slot = r['target']
        if pointer and pointer not in fine:
            raise ValueError('retry fine target has no recorded canonical mover')
        r['target'] = [fine[pointer] if pointer else None, blocker_slot]
    return result


def state(rows):
    shared = shared_state(rows)
    transitions = []
    for r in shared['footprints']:
        key = [r[1], r[3], r[4]]
        if not transitions or transitions[-1][1:] != key:
            transitions.append([r[0], *key])
    return dict(motion_sha256=digest(physical_motion(rows)), producer_sha256=digest(producer(rows)),
                admission_sha256=digest(admission(rows)), ranges_sha256=digest(range_timeline(rows)),
                lifecycle_sha256=digest(canonical_lifecycle(rows)), shared_sha256=digest(shared),
                shared_identities=shared['identities'], footprint_transitions=transitions,
                shared_footprints=len(shared['footprints']),
                shared_reference_counts=sorted({r[1][0] for r in shared['publication']}))


def verify_contract(f):
    if f['complete_scene_journeys'] is not True or f['whole_retail_pathfinder'] is not False or \
            f['last_reference_cancellation_remains_open'] is not True:
        raise ValueError('captain Stop scope exceeds complete largest-recruit scenes')
    if canonical_lifecycle(f['fine_target_control'])[0]['target'] != [-1, 0xffffffff]:
        raise ValueError('captain retry target control differs')
    expected = {'early': (5458, 353, 1352, 1561), 'late': (5593, 400, 1331, 1603)}
    if [v['name'] for v in f['variants']] != ['early', 'late']:
        raise ValueError('captain Stop requires both admission timings')
    for v in f['variants']:
        commits, footprints, drop1, drop2 = expected[v['name']]
        s = v['state']
        ids = s['shared_identities']
        if v['engine_commits'] != commits or s['shared_footprints'] != footprints or \
                len(ids) != 2 or ids[0][0] != ids[1][0] or ids[0][1] == ids[1][1] or \
                s['shared_reference_counts'] != [1, 2] or len(v['cases']) != 2:
            raise ValueError('captain Stop extent/shared lifetime differs')
        if s['footprint_transitions'] != [[1325, 0, 0x3ffc0000, 0x3ffc0000],
                [drop1, 0, 0x3f780000, 0x3ffc0000], [1558, 1, 0x3ffc0000, 0x3ffc0000],
                [drop2, 1, 0x3f780000, 0x3ffc0000]]:
            raise ValueError('captain Stop live maximum/cached footprint differs')


def render_header(rows, name):
    when = '8.9' if name == 'early' else '9.2'
    comment = 'private Stop' if name == 'early' else 'Stop'
    return (f'/* Complete native mixed13 journey; largest recruit receives public Stop at{when}s. */\n'
            f'static uint32_t const captain_thirteen_cancel_{name}_motion[][7]={{\n' +
            ''.join('    {' + ','.join(str(v) + 'u' for v in r) + '},\n' for r in physical_motion(rows)) +
            '};\n\n' + (f'/* Shared live maximum and cached footprint after largest-member {comment}. */\n' if name == 'early' else
                          '/* Shared live maximum and cached route footprint after largest-member Stop. */\n') +
            f'static uint32_t const captain_thirteen_cancel_{name}_footprints[][4]={{\n' +
            ''.join('    {' + ','.join(str(v) + 'u' for v in [r[0], r[2], r[3], r[4]]) + '},\n'
                    for r in shared_state(rows)['footprints']) + '};\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces', nargs='+', type=Path)
    p.add_argument('--fixture', required=True, type=Path)
    p.add_argument('--engine-library', required=True, type=Path)
    p.add_argument('--headers-root', type=Path)
    p.add_argument('--report', required=True, type=Path)
    a = p.parse_args()
    f = json.loads(a.fixture.read_text())
    verify_contract(f)
    engine = ctypes.CDLL(str(a.engine_library.resolve()))
    configure(engine)
    cases = [(v, c) for v in f['variants'] for c in v['cases']]
    results = []
    for path, (v, case) in zip(a.traces, cases, strict=True):
        if hashlib.sha256(path.read_bytes()).hexdigest() != case['trace_sha256'] or path.stat().st_size != case['bytes']:
            raise ValueError('captain Stop capture hash/extent differs')
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        meta = [r for r in rows if r.get('event') == 'metadata']
        end = [r for r in rows if r.get('event') == 'trace-end']
        if len(meta) != 1 or {k: value for k, value in meta[0].items() if k not in ('event', 'pid')} != case['metadata'] or \
                len(end) != 1 or not end[0].get('installed') or any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
            raise ValueError('captain Stop capture provenance/completeness differs')
        for event, count in v['event_counts'].items():
            if sum(r.get('event') == event for r in rows) != count or \
                    (event in end[0]['counts'] and end[0]['counts'][event] != count):
                raise ValueError('captain Stop observer extent differs: ' + event)
        if state(rows) != v['state']:
            raise ValueError('captain Stop motion/producer/target/shared state differs')
        if a.headers_root:
            header = a.headers_root / f'retail_captain_cancel_{v["name"]}.h'
            if hashlib.sha256(header.read_bytes()).hexdigest() != v['engine_header_sha256'] or header.read_text() != render_header(rows, v['name']):
                raise ValueError('captain Stop literal engine header differs')
        result = verify_motion(rows, engine, None)
        result.update(verify_primary(rows, engine, f))
        result.update(name=v['name'], engine_commits=v['engine_commits'], shared_footprints=v['state']['shared_footprints'])
        results.append(result)
    report = dict(passed=True, cases=len(results), results=results, complete_scene_journeys=True,
                  whole_retail_pathfinder=False, engine_commits=11051, shared_footprints=753,
                  last_reference_cancellation_remains_open=True, scope=f['scope'])
    a.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
