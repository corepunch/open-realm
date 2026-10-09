#!/usr/bin/env python3
"""Authenticate repeated CaptainGoHome captures and the bounded engine continuation."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_captain_approach_trace import births
from verify_wc3_captain_cancel_trace import digest
from verify_wc3_captain_shared_trace import shared_state
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary

END_WORD = 0x41be6666
START_WORD = 0x41900000


def continuation(rows, end_word=END_WORD, counter_end=1817):
    movers = births(rows)
    actors = [r for r in rows if r.get('event') == 'movement-mask-publication' and r.get('rawcode') == 0]
    if len(actors) != 1:
        raise ValueError('virtual captain publication is absent or aliases')
    actor = actors[0]
    profile = [actor[k] for k in ('rawcode', 'category', 'queryMask', 'objectCategory', 'pathMask')]
    if profile != [0, 2, 2, 0x01000002, 0x02000002] or actor['mover'] in movers:
        raise ValueError('virtual captain cannot use an ordinary unit profile')
    motion = []
    ranges = []
    for r in rows:
        if r.get('event') == 'velocity-commit' and r['after'][0] <= end_word:
            if r['mover'] in movers:
                motion.append([movers.index(r['mover']), *[r['after'][i] for i in (0, 2, 3, 4, 5, 7)]])
            elif r['mover'] == actor['mover'] and r['after'][0] >= START_WORD:
                motion.append([13, *[r['after'][i] for i in (0, 2, 3, 4, 5, 7)]])
        if r.get('event') == 'arrival-evaluation' and r['mover'] == actor['mover']:
            if r['footprint'] != 0:
                raise ValueError('virtual captain has a physical unit radius')
            if not ranges or ranges[-1] != r['storedRange']:
                ranges.append(r['storedRange'])
    shared = shared_state(rows)
    footprints = [[r[0], r[2], r[3], r[4]] for r in shared['footprints'] if r[0] <= counter_end]
    markers = [r['value'] for r in rows if r.get('event') == 'captain-marker']
    return dict(motion=motion, footprints=footprints, virtual_profile=profile,
                virtual_ranges=ranges, identities=shared['identities'], markers=markers)


def verify_contract(f):
    if f['complete_scene_journeys'] is not False or f['whole_retail_pathfinder'] is not False or \
            f['private_retry_continuation_remains_open'] is not True:
        raise ValueError('moving captain scope exceeds verified continuation')
    if f['engine_end_msec'] != 23800 or f['engine_commits'] != 6251 or \
            f['virtual_commits'] != 193 or f['shared_footprints'] != 398 or len(f['cases']) != 2:
        raise ValueError('moving captain verified extent differs')
    if f['virtual_profile'] != [0, 2, 2, 0x01000002, 0x02000002] or \
            f['virtual_ranges'] != [0x417a0000, 0x40c80000, 0x417a0000, 0x40c80000] or \
            f['retained_request_range'] != 500 or f['actor_final_range'] != 200:
        raise ValueError('virtual profile or retained/current range policies differ')
    if f['identities'] != [[1471, 1941], [1471, 2237]] or f['markers'][-4:] != [
            'PATHCAPTAIN thirteen before south home', 'PATHCAPTAIN thirteen after south home',
            'PATHCAPTAIN thirteen before go home', 'PATHCAPTAIN thirteen after go home']:
        raise ValueError('public moving captain request or shared reuse differs')
    for case in f['cases']:
        if case['velocity_commits'] >= case['metadata']['samples'] or case['metadata']['owned'] is not True:
            raise ValueError('capture is capped or not owned')


def verify_capture_extent(rows, case):
    meta = [r for r in rows if r.get('event') == 'metadata']
    end = [r for r in rows if r.get('event') == 'trace-end']
    if len(meta) != 1 or {k: v for k, v in meta[0].items() if k not in ('event', 'pid')} != case['metadata'] or \
            len(end) != 1 or not end[0].get('installed') or \
            any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
        raise ValueError('capture provenance or completion differs')
    for event, expected in [('marker', 304), ('clock-owner-end', 1000), ('velocity-commit', case['velocity_commits'])]:
        actual = sum(r.get('event') == event for r in rows)
        if actual != expected or (event in end[0]['counts'] and end[0]['counts'][event] != actual):
            raise ValueError('capture observer is capped or incomplete: ' + event)


def render_header(c):
    return ('/* Original all13 Stop, occupied south home and CaptainGoHome: first23.8 seconds.\n'
            ' * Physical births0..12; moving virtual captain13. Private retry continuation remains open. */\n'
            'static uint32_t const captain_go_home_motion[][7]={\n' +
            ''.join('    {' + ','.join(str(v) + 'u' for v in r) + '},\n' for r in c['motion']) +
            '};\n\n/* Shared live/cached footprints through the same23.8-second prefix. */\n'
            'static uint32_t const captain_go_home_footprints[][4]={\n' +
            ''.join('    {' + ','.join(str(v) + 'u' for v in r) + '},\n' for r in c['footprints']) + '};\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces', nargs='+', type=Path)
    p.add_argument('--fixture', required=True, type=Path)
    p.add_argument('--engine-library', required=True, type=Path)
    p.add_argument('--headers-root', required=True, type=Path)
    p.add_argument('--report', required=True, type=Path)
    a = p.parse_args()
    f = json.loads(a.fixture.read_text()); verify_contract(f)
    engine = ctypes.CDLL(str(a.engine_library.resolve())); configure(engine)
    results = []
    for path, case in zip(a.traces, f['cases'], strict=True):
        if hashlib.sha256(path.read_bytes()).hexdigest() != case['trace_sha256'] or path.stat().st_size != case['bytes']:
            raise ValueError('capture hash or byte extent differs')
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        verify_capture_extent(rows, case)
        c = continuation(rows)
        if len(c['motion']) != f['engine_commits'] or sum(r[0] == 13 for r in c['motion']) != f['virtual_commits'] or \
                len(c['footprints']) != f['shared_footprints'] or \
                any(c[k] != f[k] for k in ('virtual_profile', 'virtual_ranges', 'identities', 'markers')) or \
                digest(c['motion']) != f['motion_sha256'] or digest(c['footprints']) != f['footprints_sha256']:
            raise ValueError('repeated initial travel or range handoff differs')
        header = a.headers_root / 'retail_captain_go_home.h'
        if hashlib.sha256(header.read_bytes()).hexdigest() != f['engine_header_sha256'] or header.read_text() != render_header(c):
            raise ValueError('literal engine motion/footprint reference differs')
        result = verify_motion(rows, engine, None); result.update(verify_primary(rows, engine, f)); results.append(result)
    report = dict(passed=True, cases=len(results), results=results, scope=f['scope'],
                  complete_scene_journeys=False, whole_retail_pathfinder=False,
                  private_retry_continuation_remains_open=True, engine_commits=f['engine_commits'],
                  virtual_commits=f['virtual_commits'], shared_footprints=f['shared_footprints'])
    a.report.write_text(json.dumps(report, indent=2) + '\n'); print(json.dumps(report))


if __name__ == '__main__':
    main()
