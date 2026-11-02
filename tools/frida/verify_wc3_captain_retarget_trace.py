#!/usr/bin/env python3
"""Verify changed-destination wait reset and moving captain travel through27.2s."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_captain_approach_trace import births
from verify_wc3_captain_cancel_trace import digest
from verify_wc3_captain_go_home_trace import (continuation, render_header as render_base,
    verify_capture_extent, verify_contract as verify_base)
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary


def reset_witness(rows):
    mover = births(rows)[8]
    steps = [r for r in rows if r.get('event') == 'route-step' and
             r['mover'] == mover and r['counter'] == 1821]
    if len(steps) != 1:
        raise ValueError('changed-destination member witness is absent or aliases')
    step = steps[0]
    path = step['path']
    replan = [r for r in rows if r.get('event') == 'replan-check' and
              r['path'] == path and r['counter'] == 1821]
    if len(replan) != 1:
        raise ValueError('member destination readiness witness differs')
    return dict(before_delay=step['before'][8], after_delay=step['after'][8],
                source=step['source'], destination=step['destination'], outputs=step['outputs'],
                changed=replan[0]['changed'], ready=replan[0]['ready'], timestamps=replan[0]['timestamps'])


def verify_contract(f):
    if f['complete_scene_journeys'] is not False or f['whole_retail_pathfinder'] is not False or \
            f['one_point_refill_remains_open'] is not True:
        raise ValueError('retarget reset scope exceeds verified travel')
    if f['engine_end_msec'] != 27200 or f['engine_commits'] != 7419 or \
            f['virtual_commits'] != 306 or f['shared_footprints'] != 624 or len(f['cases']) != 2:
        raise ValueError('retarget reset continuation extent differs')
    if f['reset_witness'] != dict(before_delay=20, after_delay=0, source=[1126192892, 1113215205],
            destination=[1126531208, 1116880010], outputs=[0, 1073729781, 0, 0, 0],
            changed=1, ready=1, timestamps=[0, 0]):
        raise ValueError('accepted changed destination must clear wait before refill')


def render_header(c):
    return ('/* Native moving captain continuation23.8..27.2s; prepend retail_captain_go_home.h. */\n'
            'static uint32_t const captain_go_home_retry_motion[][7]={\n' +
            ''.join('    {' + ','.join(str(v) + 'u' for v in r) + '},\n' for r in c['motion'][6251:]) +
            '};\n\n/* Additional shared footprints after the initial captain travel prefix. */\n'
            'static uint32_t const captain_go_home_retry_footprints[][4]={\n' +
            ''.join('    {' + ','.join(str(v) + 'u' for v in r) + '},\n' for r in c['footprints'][398:]) + '};\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces', nargs='+', type=Path)
    p.add_argument('--fixture', required=True, type=Path)
    p.add_argument('--engine-library', required=True, type=Path)
    p.add_argument('--headers-root', required=True, type=Path)
    p.add_argument('--report', required=True, type=Path)
    a = p.parse_args()
    f = json.loads(a.fixture.read_text()); verify_contract(f)
    base_path = a.fixture.parent / f['base_fixture']
    if hashlib.sha256(base_path.read_bytes()).hexdigest() != f['base_sha256']:
        raise ValueError('initial GoHome fixture changed')
    base = json.loads(base_path.read_text()); verify_base(base)
    if f['cases'] != base['cases']:
        raise ValueError('retarget reset requires the same two uncapped public captures')
    engine = ctypes.CDLL(str(a.engine_library.resolve())); configure(engine)
    results = []
    for path, case in zip(a.traces, f['cases'], strict=True):
        if hashlib.sha256(path.read_bytes()).hexdigest() != case['trace_sha256'] or path.stat().st_size != case['bytes']:
            raise ValueError('retarget capture hash/extent differs')
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        verify_capture_extent(rows, case)
        c = continuation(rows, 0x41d9999a, 1930); prefix = continuation(rows)
        if c['motion'][:6251] != prefix['motion'] or c['footprints'][:398] != prefix['footprints'] or \
                digest(c['motion']) != f['motion_sha256'] or digest(c['footprints']) != f['footprints_sha256'] or \
                len(c['motion']) != 7419 or sum(r[0] == 13 for r in c['motion']) != 306 or \
                len(c['footprints']) != 624 or reset_witness(rows) != f['reset_witness']:
            raise ValueError('retarget wait reset or repeated continuation differs')
        for filename, expected, sha in [('retail_captain_go_home.h', render_base(prefix), base['engine_header_sha256']),
                ('retail_captain_go_home_retry.h', render_header(c), f['engine_header_sha256'])]:
            header = a.headers_root / filename
            if hashlib.sha256(header.read_bytes()).hexdigest() != sha or header.read_text() != expected:
                raise ValueError('retarget composed literal engine reference differs')
        result = verify_motion(rows, engine, None); result.update(verify_primary(rows, engine, base)); results.append(result)
    report = dict(passed=True, cases=len(results), results=results, scope=f['scope'],
                  complete_scene_journeys=False, whole_retail_pathfinder=False, one_point_refill_remains_open=True,
                  engine_commits=7419, virtual_commits=306, shared_footprints=624)
    a.report.write_text(json.dumps(report, indent=2) + '\n'); print(json.dumps(report))


if __name__ == '__main__':
    main()
