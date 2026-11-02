#!/usr/bin/env python3
"""Certify public target/blocker reinsertion against real fine-cell chains."""
import argparse
import collections
import ctypes
import hashlib
import json
import re
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_blocked_goal_trace import lifecycle
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary
from verify_wc3_selected_queued_trace import canonical, digest, motion_words, owner_order


def overlap_lifecycle(rows):
    result = lifecycle(rows)
    for row in result:
        if 'blockers' in row:
            objects = row['blockers']['objects']
            row['blockers']['objects'] = sorted(
                [{k: v for k, v in obj.items() if k not in ('object', 'payload')}
                 for obj in objects.values()], key=lambda obj: json.dumps(obj, sort_keys=True))
    return result


def cell_chains(rows):
    cells = [r for r in rows if r.get('event') == 'cell-links']
    if len(cells) != 9 or any(r['truncated'] or (r['x'], r['y']) != (31, 32) for r in cells):
        raise ValueError('overlap requires nine complete observed cell chains')
    ordinary = [r for r in cells[0]['records'] if r.get('category') == 0x010000ca]
    if len(ordinary) != 2:
        raise ValueError('overlap initial ordinary actors differ')
    identities = {ordinary[0]['payload']: 'blocker', ordinary[1]['payload']: 'target'}
    result = []
    for cell in cells:
        records = []
        for r in cell['records']:
            if r['kind'] not in (0, 1):
                raise ValueError('unexpected nonordinary cell record in overlap fixture')
            role = identities.get(r['payload'], 'captain')
            if role == 'captain' and r['category'] != 0x01000000:
                raise ValueError('unclassified overlap actor')
            records.append(dict(role=role, kind=r['kind'], rectangle=r['rectangle'],
                                category=r['category'], flags=r['flags']))
        result.append(dict(marker=cell['marker'], records=records))
    return result


def render_header(spec):
    return ('/* Literal retail overlapping target/blocker public journey. */\n'
            'static uint32_t const overlap81_motion[][7]={\n' +
            ''.join('    {' + ','.join(str(v) + 'u' for v in row) + '},\n'
                    for row in spec['motion']) + '};\n')


def verify_contract(spec):
    marks = [re.fullmatch(r'PATHTRACE tick=(\d+) label=(\w+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)', m)
             for m in spec['markers']]
    if any(m is None for m in marks) or len(marks) != 310 or [int(m[1]) for m in marks if m[2] == 'sample'] != list(range(1, 301)):
        raise ValueError('overlap public timeline truncated or altered')
    fine = [r for r in spec['lifecycle'] if r['event'] == 'search' and r['kind'] == 'fine']
    if [(r['result'], r['pops'], r['budget'], r['nodes']) for r in fine] != [(-1, 701, 700, 538), (122, 39, 700, 127), (122, 39, 700, 128)]:
        raise ValueError('overlap blocker-first/target-first/removal searches differ')
    if [r['blockers']['objectHits'] for r in fine] != [27, 1, 0]:
        raise ValueError('overlap actual foreign blockage differs')
    chains = spec['chains']
    if [r['role'] for r in chains[1]['records']] != ['captain', 'blocker', 'target'] or [r['role'] for r in chains[4]['records']] != ['target', 'captain', 'blocker']:
        raise ValueError('overlap did not reverse real ordinary link order')
    if [r['role'] for r in chains[-1]['records']] != ['target', 'captain'] or len(spec['motion']) != 501:
        raise ValueError('overlap removal or final movement extent differs')


def verify(rows, spec, case, engine):
    verify_contract(spec)
    meta = [r for r in rows if r.get('event') == 'metadata']
    end = [r for r in rows if r.get('event') == 'trace-end']
    if len(meta) != 1 or {k: v for k, v in meta[0].items() if k not in ('event', 'pid')} != case['metadata']:
        raise ValueError('overlap capture provenance differs')
    if len(end) != 1 or not end[0].get('installed') or any(r.get('event') == 'trace-failed' or r.get('type') == 'error' for r in rows):
        raise ValueError('overlap native observer failed/incomplete')
    counts = collections.Counter(r.get('event') for r in rows)
    for event, count in spec['event_counts'].items():
        if counts[event] != count or event in end[0]['counts'] and end[0]['counts'][event] != count:
            raise ValueError('overlap observer extent differs: ' + event)
    for event, count in counts.items():
        if event.startswith('clock-') and end[0]['counts'].get(event) != count:
            raise ValueError('overlap clock observer did not close')
    if (digest(canonical(rows)) != spec['phases_sha256'] or motion_words(rows) != spec['motion'] or
        owner_order(rows) != spec['owner_order'] or overlap_lifecycle(rows) != spec['lifecycle'] or
        cell_chains(rows) != spec['chains'] or
        [r['value'] for r in rows if r.get('event') == 'marker'] != spec['markers'] or
        [r['value'] for r in rows if r.get('event') == 'overlap-marker'] != spec['overlap_markers']):
        raise ValueError('overlap literal decisions/chain producer/outcome differ')
    loaded = [r for r in rows if r.get('event') == 'map-load-complete']
    if len(loaded) != 1 or (loaded[0]['width'], loaded[0]['height']) != (64, 64) or any(loaded[0]['cells']) or digest(loaded[0]['hierarchy']) != spec['hierarchy_sha256']:
        raise ValueError('overlap real empty terrain loader differs')
    result = verify_motion(rows, engine, None)
    result.update(verify_primary(rows, engine, spec))
    result.update(passed=True, fine_searches=3, observed_chains=9, motion_sha256=digest(spec['motion']))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces', nargs='+', type=Path)
    p.add_argument('--fixture', required=True, type=Path)
    p.add_argument('--engine-library', required=True, type=Path)
    p.add_argument('--check-engine-header', type=Path)
    p.add_argument('--report', required=True, type=Path)
    a = p.parse_args()
    spec = json.loads(a.fixture.read_text())
    engine = ctypes.CDLL(str(a.engine_library.resolve())); configure(engine)
    engine.pathing_heading_error.argtypes = [ctypes.c_uint32] * 3
    engine.pathing_heading_error.restype = ctypes.c_uint32
    results = []
    for path, case in zip(a.traces, spec['cases'], strict=True):
        result = verify([json.loads(l) for l in path.read_text().splitlines()], spec, case, engine)
        result['trace_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest(); results.append(result)
    if a.check_engine_header and a.check_engine_header.read_text() != render_header(spec):
        raise ValueError('overlap literal engine header differs')
    report = dict(passed=True, cases=len(results), results=results, scope=spec['scope'])
    for key in ('fine_searches', 'observed_chains', 'exact_velocity_commits', 'exact_decisions', 'owner_callbacks'):
        report[key] = sum(r[key] for r in results)
    a.report.write_text(json.dumps(report, indent=2) + '\n'); print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
