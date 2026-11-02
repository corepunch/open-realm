#!/usr/bin/env python3
"""Authenticate file-backed overlapping widget creation and both native grids."""
import argparse
import hashlib
import json
from pathlib import Path

LABELS = ('start_blocker_lifecycle', 'tree_first', 'tree_then_gate',
          'tree_first_removed', 'first_pair_removed', 'gate_first',
          'gate_then_tree', 'gate_first_removed', 'second_pair_removed')
STATES = ('baseline', 'tree', 'both', 'gate', 'baseline', 'gate', 'both', 'tree', 'baseline')


def verify_overlap(rows, fixture):
    if fixture['version'] != 1 or fixture['whole_retail_pathfinder'] is not False or \
            fixture['states'] != list(STATES):
        raise ValueError('creation-order scope differs')
    if [i for i, r in enumerate(rows) if r.get('event') == 'trace-end'] != [len(rows)-1] or \
            not rows[-1].get('installed') or any(r.get('event') in ('trace-failed', 'error') for r in rows):
        raise ValueError('observer failed or did not finish')
    stages = [r for r in rows if r.get('event') == 'blocker-geometry']
    lives = [r['value'] for r in rows if r.get('event') == 'blocker-lifecycle-marker']
    if lives != fixture['life_markers'] or lives != [r['marker'] for r in stages] or \
            any('label='+label+' ' not in life for label, life in zip(LABELS, lives, strict=True)):
        raise ValueError('public pose or creation/removal order differs')
    for row, state in zip(stages, STATES, strict=True):
        geometry = {k: row[k] for k in ('width', 'height', 'box', 'masks', 'hierarchy')}
        if geometry != fixture['geometry'][state]:
            raise ValueError('fine footprint or hierarchy differs: ' + state)
    markers = [r['value'] for r in rows if r.get('event') == 'marker']
    if sum('label=complete ' in m for m in markers) != 1 or any('_failed' in m for m in markers):
        raise ValueError('public probe did not complete')
    retired = sum(r.get('event') == 'widget-method' and r['method'] == 'destroy' and
                  r['beforeCollection'] != '0x0' and r['afterCollection'] == '0x0' for r in rows)
    if retired != 6 or rows[-1]['counts'].get('widget-destroy') != 8:
        raise ValueError('native collection retirement differs')
    return dict(snapshots=len(stages), fine_cells=len(stages)*1024,
                hierarchy_classes=len(stages)*1360, retired_collections=retired)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces', nargs=2, type=Path)
    p.add_argument('--fixture', required=True, type=Path)
    p.add_argument('--report', required=True, type=Path)
    args = p.parse_args()
    fixture = json.loads(args.fixture.read_text())
    results = []
    for path, expected in zip(args.traces, fixture['captures'], strict=True):
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected['sha256'] or path.stat().st_size != expected['bytes']:
            raise ValueError('capture hash or extent differs')
        rows = [json.loads(s) for s in path.read_text().splitlines()]
        metadata = [{k:v for k,v in r.items() if k not in ('event','pid')}
                    for r in rows if r.get('event') == 'metadata']
        if metadata != [expected['metadata']]:
            raise ValueError('native binary, source, map or observer provenance differs')
        results.append(verify_overlap(rows, fixture))
    report = dict(passed=True, cases=2, snapshots=sum(r['snapshots'] for r in results),
                  fine_cells=sum(r['fine_cells'] for r in results),
                  hierarchy_classes=sum(r['hierarchy_classes'] for r in results),
                  whole_retail_pathfinder=False, scope=fixture['scope'], results=results)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
