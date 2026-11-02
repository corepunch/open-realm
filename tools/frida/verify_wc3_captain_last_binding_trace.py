#!/usr/bin/env python3
"""Verify complete all-Stop captain journeys and repeated AI initialization gate."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_captain_cancel_trace import digest, state
from verify_wc3_captain_approach_trace import physical_motion
from verify_wc3_captain_shared_trace import shared_state
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary


def cancellation(rows):
    pubs = [r for r in rows if r.get('event') == 'captain-shared-publish']
    identity = shared_state(rows)['identities']
    if len(identity) != 1:
        raise ValueError('last-binding journey requires one shared generation')
    bound = [r for r in pubs if r.get('identityBefore') == identity[0]]
    markers = [r['value'] for r in rows if r.get('event') == 'captain-marker']
    natives = [[r['event'], r['name']] for r in rows if r.get('event') in
               ('captain-native-begin', 'captain-native-end')]
    return dict(publication=[[r['counter'], r['clock'], r['before'], r['after'], r['identity']]
                             for r in bound], markers=markers, natives=natives)


def verify_contract(f):
    if f['complete_scene_journeys'] is not True or f['whole_retail_pathfinder'] is not False or \
            f['remove_retarget_reuse_remain_open'] is not True:
        raise ValueError('last-binding cancellation scope differs')
    if f['engine_commits'] != 3549 or f['state']['shared_footprints'] != 12 or len(f['cases']) != 2:
        raise ValueError('last-binding complete repeated journey extent differs')
    p = f['cancellation']['publication']
    if len(p) != 8 or [r[0] for r in p] != list(range(1325, 1333)) or \
            any(r[2][0] != 2 or r[3][0] != 2 for r in p[:-1]) or \
            p[-1] != [1332, [0x4113d4ca, 0, 0x43960000, 4096],
                       [0, 0x40960000, 0x7f7fffff, 0], [0, 0x7f7fffff, 0x7f7fffff, 0], [-1, -1]]:
        raise ValueError('final shared binding retirement/publication order differs')
    markers = f['cancellation']['markers']
    required = ['PATHCAPTAIN home begin', 'PATHCAPTAIN home before recruit',
                'PATHCAPTAIN home accepted', 'PATHCAPTAIN thirteen mixed roster thirteen',
                'PATHCAPTAIN thirteen all before stop', 'PATHCAPTAIN thirteen all after stop',
                'PATHCAPTAIN thirteen all before restart', 'PATHCAPTAIN thirteen all after restart']
    if markers != required:
        raise ValueError('second AI call must not replay main')
    natives = f['cancellation']['natives']
    if natives[-6:] != [[event, name] for event, name in
            [('captain-native-begin', 'start-wrapper'), ('captain-native-begin', 'load'),
             ('captain-native-begin', 'compile'), ('captain-native-end', 'compile'),
             ('captain-native-end', 'load'), ('captain-native-end', 'start-wrapper')]]:
        raise ValueError('second public AI call lacks source-loading witness')


def render_header(rows):
    return ('/* Complete native mixed13 journey: public Stop all13 at9.2s; repeated StartCampaignAI at17s. */\n'
            'static uint32_t const captain_thirteen_cancel_all_motion[][7]={\n' +
            ''.join('    {' + ','.join(str(v) + 'u' for v in r) + '},\n' for r in physical_motion(rows)) +
            '};\n\n/* Shared live maximum and cached route footprint before the final public Stop. */\n'
            'static uint32_t const captain_thirteen_cancel_all_footprints[][4]={\n' +
            ''.join('    {' + ','.join(str(v) + 'u' for v in [r[0], r[2], r[3], r[4]]) + '},\n'
                    for r in shared_state(rows)['footprints']) + '};\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces', nargs='+', type=Path)
    p.add_argument('--fixture', required=True, type=Path)
    p.add_argument('--engine-library', required=True, type=Path)
    p.add_argument('--headers-root', required=True, type=Path)
    p.add_argument('--report', required=True, type=Path)
    a = p.parse_args()
    f = json.loads(a.fixture.read_text())
    verify_contract(f)
    engine = ctypes.CDLL(str(a.engine_library.resolve()))
    configure(engine)
    results = []
    for path, case in zip(a.traces, f['cases'], strict=True):
        if hashlib.sha256(path.read_bytes()).hexdigest() != case['trace_sha256'] or path.stat().st_size != case['bytes']:
            raise ValueError('last-binding capture hash/extent differs')
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        meta = [r for r in rows if r.get('event') == 'metadata']
        end = [r for r in rows if r.get('event') == 'trace-end']
        if len(meta) != 1 or {k: v for k, v in meta[0].items() if k not in ('event', 'pid')} != case['metadata'] or \
                len(end) != 1 or not end[0].get('installed') or any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
            raise ValueError('last-binding capture provenance/completeness differs')
        for event, count in f['event_counts'].items():
            if sum(r.get('event') == event for r in rows) != count or \
                    (event in end[0]['counts'] and end[0]['counts'][event] != count):
                raise ValueError('last-binding observer extent differs: ' + event)
        if state(rows) != f['state'] or cancellation(rows) != f['cancellation']:
            raise ValueError('last-binding complete motion/lifecycle/initialization differs')
        header = a.headers_root / 'retail_captain_cancel_all.h'
        if hashlib.sha256(header.read_bytes()).hexdigest() != f['engine_header_sha256'] or header.read_text() != render_header(rows):
            raise ValueError('last-binding literal engine header differs')
        result = verify_motion(rows, engine, None)
        result.update(verify_primary(rows, engine, f))
        results.append(result)
    report = dict(passed=True, cases=len(results), results=results, complete_scene_journeys=True,
                  whole_retail_pathfinder=False, engine_commits=3549, shared_footprints=12,
                  remove_retarget_reuse_remain_open=True, scope=f['scope'])
    a.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
