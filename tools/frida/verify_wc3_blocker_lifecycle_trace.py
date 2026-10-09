#!/usr/bin/env python3
"""Authenticate public resource and destructible lifetimes and their next requests."""
import argparse
import ctypes
import hashlib
import json
import re
from pathlib import Path
from verify_wc3_motion_trace import verify as verify_motion


LABELS = ('start_blocker_lifecycle', 'trees_created', 'tree_depleted_overlap',
          'before_tree_remove', 'trees_removed', 'mine_created', 'mine_depleted',
          'before_mine_remove', 'mine_removed', 'gate_created', 'gate_killed',
          'gate_restored', 'gate_removed')
STATE = ('baseline', 'trees', 'trees', 'trees', 'baseline', 'mine', 'baseline',
         'baseline', 'baseline', 'gate', 'baseline', 'gate', 'baseline')
REQUESTS = ('after_trees', 'after_mine_depletion', 'after_mine_removal', 'after_gate_removal')


def geometry(row):
    return {k: row[k] for k in ('width', 'height', 'box', 'masks', 'hierarchy')}


def verify_lifecycle(rows, fixture):
    if fixture['version'] != 1 or \
            fixture['scope'] != 'Public depletion/destruction/final removal; static blue footprints and four hierarchy levels' or \
            fixture['whole_retail_pathfinder'] is not False:
        raise ValueError('lifecycle scope differs')
    ends = [i for i, r in enumerate(rows) if r.get('event') == 'trace-end']
    if ends != [len(rows)-1] or not rows[-1].get('installed') or \
            any(r.get('event') in ('trace-failed', 'error') for r in rows):
        raise ValueError('lifecycle observer failed or did not finish')
    stages = [r for r in rows if r.get('event') == 'blocker-geometry']
    lives = [r['value'] for r in rows if r.get('event') == 'blocker-lifecycle-marker']
    labels = [re.search(r'label=([^ ]+)', r['marker']).group(1) for r in stages]
    if labels != list(LABELS) or lives != [r['marker'] for r in stages] or lives != fixture['life_markers']:
        raise ValueError('depletion, overlap, restoration or final removal differs')
    for row, state in zip(stages, STATE, strict=True):
        if geometry(row) != fixture['geometry'][state]:
            raise ValueError('footprint or hierarchy differs: ' + row['marker'])
    baseline = fixture['geometry']['baseline']
    for state in ('trees', 'mine', 'gate'):
        current = fixture['geometry'][state]
        if sum(bool(v) for v in current['masks']) <= sum(bool(v) for v in baseline['masks']) or \
                current['hierarchy'] == baseline['hierarchy']:
            raise ValueError('authored blocker did not change both grids')
    markers = [(i, r['value']) for i, r in enumerate(rows) if r.get('event') == 'marker']
    if any('_failed' in v or '_rejected' in v for _, v in markers) or \
            sum('label=complete ' in v for _, v in markers) != 1:
        raise ValueError('public lifecycle did not complete')
    for name in ('lumber', 'gold', *REQUESTS):
        found = [i for i, value in markers if 'label=' + name + '_accepted ' in value]
        if len(found) != 1:
            raise ValueError('public order missing or duplicated: ' + name)
        if name in REQUESTS:
            start = found[0]
            end = next((i for i in range(start+1, len(rows))
                        if rows[i].get('event') == 'blocker-lifecycle-marker'), len(rows))
            if not any(r.get('event') == 'search' and r.get('kind') == 'fine' for r in rows[start:end]):
                raise ValueError('no fresh fine request after grid publication: ' + name)
    methods = [r for r in rows if r.get('event') == 'widget-method']
    for kind in ('create', 'destroy'):
        if sum(r['method'] == kind for r in methods) != rows[-1]['counts'].get('widget-' + kind, 0):
            raise ValueError('widget lifetime truncated: ' + kind)
    retired = sum(r['method'] == 'destroy' and r['beforeCollection'] != '0x0' and
                  r['afterCollection'] == '0x0' for r in methods)
    if retired != fixture['retired_collections']:
        raise ValueError('complete native collection retirement differs')
    return dict(snapshots=len(stages), fresh_requests=len(REQUESTS), retired_collections=retired)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces', nargs=2, type=Path)
    p.add_argument('--fixture', required=True, type=Path)
    p.add_argument('--engine-library', required=True, type=Path)
    p.add_argument('--report', required=True, type=Path)
    a = p.parse_args()
    fixture = json.loads(a.fixture.read_text())
    engine = ctypes.CDLL(str(a.engine_library.resolve()))
    results = []
    for path, expected in zip(a.traces, fixture['captures'], strict=True):
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected['sha256'] or \
                path.stat().st_size != expected['bytes']:
            raise ValueError('capture hash or extent differs')
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        metadata = [{k: v for k, v in r.items() if k not in ('event', 'pid')}
                    for r in rows if r.get('event') == 'metadata']
        if metadata != [expected['metadata']]:
            raise ValueError('native binary, source, map or observer provenance differs')
        result = verify_lifecycle(rows, fixture)
        result.update(verify_motion(rows, engine, None))
        if result['decision_sha256'] != fixture['decision_sha256'] or \
                result['velocity_sha256'] != fixture['velocity_sha256']:
            raise ValueError('repeated native numerical decisions or commits differ')
        results.append(result)
    report = dict(passed=True, cases=2, snapshots=sum(r['snapshots'] for r in results),
                  fresh_requests=sum(r['fresh_requests'] for r in results),
                  exact_velocity_commits=sum(r['exact_velocity_commits'] for r in results),
                  results=results, whole_retail_pathfinder=False, scope=fixture['scope'])
    a.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
