#!/usr/bin/env python3
"""Authenticate partial fine/coarse handoff and repeated captain travel through30s."""
import argparse
import ctypes
import hashlib
import json
import struct
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_captain_approach_trace import births
from verify_wc3_captain_cancel_trace import digest
from verify_wc3_captain_go_home_trace import (continuation, render_header as render_initial,
    verify_capture_extent, verify_contract as verify_initial)
from verify_wc3_captain_retarget_trace import (render_header as render_retry,
    reset_witness, verify_contract as verify_retry)
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary


def one(rows, predicate, name):
    found = [r for r in rows if predicate(r)]
    if len(found) != 1:
        raise ValueError(name + ' witness is absent or aliases')
    return found[0]


def refill_witness(rows):
    mover = births(rows)[4]
    step = one(rows, lambda r: r.get('event') == 'route-step' and
               r.get('mover') == mover and r.get('counter') == 1933, 'one-point advance')
    at = rows.index(step)
    route = one(rows[:at], lambda r: r.get('event') == 'route' and
                r.get('path') == step['path'] and r.get('kind') == 'fine' and
                r.get('count') == 1 and r.get('indices') == [0, 7], 'one-point reconstruction')
    search = one(rows, lambda r: r.get('event') == 'search' and
                 r.get('path') == step['path'] and r.get('request') == route['request'], 'partial search')
    return dict(source=step['source'], destination=step['destination'],
                before=step['before'], after=step['after'], outputs=step['outputs'],
                search=[search[k] for k in ('result', 'pops', 'budget', 'nodes', 'goal', 'footprintClass')],
                points=[[struct.unpack('<I', struct.pack('<f', v))[0] for v in p] for p in route['points']],
                truncated=route['truncated'])


def stop_witness(rows):
    mover = births(rows)[4]
    step = one(rows, lambda r: r.get('event') == 'route-step' and
               r.get('mover') == mover and r.get('counter') == 1996, 'stopped coarse handoff')
    at = rows.index(step)
    decision = next((r for r in reversed(rows[:at]) if r.get('event') == 'motion-decision' and
                     r.get('mover') == mover), None)
    if decision is None:
        raise ValueError('stopped coarse handoff decision is absent')
    return dict(source=step['source'], destination=step['destination'],
                before=step['before'], after=step['after'], outputs=step['outputs'],
                decision=[decision[k] for k in ('speed', 'heading', 'error', 'stop', 'nextSpeed', 'nextHeading')])


REFILL = dict(source=[1126268917, 1111991949], destination=[1125580800, 1114374144],
    before=[4294967295, 7, 0, 0, 26215100, 35651584, 0, 0, 0, 6],
    after=[4294967295, 2, 1933, 0, 26215100, 304087040, 0, 0, 0, 6],
    outputs=[0, 1083625226, 0, 0, 0], search=[-1, 701, 700, 712, [161, 47], 1],
    points=[[1126268917, 1111991949]], truncated=False)
STOP = dict(source=[1126031686, 1113399653], destination=[1125580800, 1114374144],
    before=[0, 2, 1943, 0, 26215100, 304087040, 0, 0, 0, 0],
    after=[4294967295, 0, 1943, 0, 26215100, 304087040, 0, 0, 0, 0],
    outputs=[0, 1076350364, 0, 0, 0],
    decision=[1083572252, 1076350212, 941096960, 1, 0, 1076350364])


def verify_contract(f):
    if f['complete_scene_journeys'] is not False or f['whole_retail_pathfinder'] is not False or \
            f['natural_completion_remains_open'] is not True:
        raise ValueError('refill handoff scope exceeds bounded travel')
    if f['engine_end_msec'] != 30000 or f['engine_commits'] != 7933 or \
            f['virtual_commits'] != 327 or f['shared_footprints'] != 812 or len(f['cases']) != 2:
        raise ValueError('refill continuation extent differs')
    if f['refill_witness'] != REFILL or f['stop_witness'] != STOP:
        raise ValueError('partial reconstruction or stopped coarse handoff differs')


def render_header(c):
    return ('/* Native moving captain continuation27.2..30s; prepend initial and retry references. */\n'
            'static uint32_t const captain_go_home_refill_motion[][7]={\n' +
            ''.join('    {' + ','.join(str(v) + 'u' for v in r) + '},\n' for r in c['motion'][7419:]) +
            '};\n\n/* Additional shared footprints through the captured30s completion marker. */\n'
            'static uint32_t const captain_go_home_refill_footprints[][4]={\n' +
            ''.join('    {' + ','.join(str(v) + 'u' for v in r) + '},\n' for r in c['footprints'][624:]) + '};\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces', nargs='+', type=Path)
    p.add_argument('--fixture', required=True, type=Path)
    p.add_argument('--engine-library', required=True, type=Path)
    p.add_argument('--headers-root', required=True, type=Path)
    p.add_argument('--report', required=True, type=Path)
    a = p.parse_args()
    f = json.loads(a.fixture.read_text()); verify_contract(f)
    retry_path = a.fixture.parent / f['base_fixture']
    if hashlib.sha256(retry_path.read_bytes()).hexdigest() != f['base_sha256']:
        raise ValueError('retarget fixture changed')
    retry = json.loads(retry_path.read_text()); verify_retry(retry)
    initial_path = retry_path.parent / retry['base_fixture']
    if hashlib.sha256(initial_path.read_bytes()).hexdigest() != retry['base_sha256']:
        raise ValueError('initial GoHome fixture changed')
    initial = json.loads(initial_path.read_text()); verify_initial(initial)
    if f['cases'] != retry['cases'] or retry['cases'] != initial['cases']:
        raise ValueError('refill requires the same two uncapped public captures')
    engine = ctypes.CDLL(str(a.engine_library.resolve())); configure(engine)
    results = []
    for path, case in zip(a.traces, f['cases'], strict=True):
        if hashlib.sha256(path.read_bytes()).hexdigest() != case['trace_sha256'] or path.stat().st_size != case['bytes']:
            raise ValueError('refill capture hash/extent differs')
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        verify_capture_extent(rows, case)
        c = continuation(rows, 0x41f00000, 2024)
        prefix = continuation(rows); middle = continuation(rows, 0x41d9999a, 1930)
        if c['motion'][:7419] != middle['motion'] or c['footprints'][:624] != middle['footprints'] or \
                digest(c['motion']) != f['motion_sha256'] or digest(c['footprints']) != f['footprints_sha256'] or \
                len(c['motion']) != 7933 or sum(r[0] == 13 for r in c['motion']) != 327 or \
                len(c['footprints']) != 812 or refill_witness(rows) != REFILL or stop_witness(rows) != STOP or \
                reset_witness(rows) != retry['reset_witness']:
            raise ValueError('refill, stopped handoff or repeated continuation differs')
        for filename, expected, sha in [
                ('retail_captain_go_home.h', render_initial(prefix), initial['engine_header_sha256']),
                ('retail_captain_go_home_retry.h', render_retry(middle), retry['engine_header_sha256']),
                ('retail_captain_go_home_refill.h', render_header(c), f['engine_header_sha256'])]:
            header = a.headers_root / filename
            if hashlib.sha256(header.read_bytes()).hexdigest() != sha or header.read_text() != expected:
                raise ValueError('refill composed literal engine reference differs')
        result = verify_motion(rows, engine, None); result.update(verify_primary(rows, engine, initial)); results.append(result)
    report = dict(passed=True, cases=len(results), results=results, scope=f['scope'],
                  complete_scene_journeys=False, whole_retail_pathfinder=False, natural_completion_remains_open=True,
                  engine_commits=7933, virtual_commits=327, shared_footprints=812)
    a.report.write_text(json.dumps(report, indent=2) + '\n'); print(json.dumps(report))


if __name__ == '__main__':
    main()
