#!/usr/bin/env python3
"""Preserve literal target-pursuit owner snapshots from repeated Frida captures.

The earlier TARGET-02.1 analyzer used six-decimal display coordinates. Never
reconstruct native float words from that display when raw captures are available.
Only owner visits that reach the destination sampler are movement decisions;
pre-prune snapshots of an already retired owner remain in source captures.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
FIELDS = ['counter', 'refresh', 'unseen', 'x', 'y', 'fine_time', 'coarse_time',
          'destination_x', 'destination_y', 'group_destination_x', 'group_destination_y',
          'group_fine_time', 'group_coarse_time', 'group_fine_count', 'group_coarse_count',
          'group_fine_index', 'group_coarse_index', 'fine_count', 'coarse_count',
          'fine_index', 'coarse_index', 'wait', 'retry']


def capture(path):
    source_bytes = path.read_bytes()
    rows = []
    for line in path.read_text().splitlines():
        row = json.loads(line)
        rows.extend(row['rows'] if row.get('event') == 'batch' else [row])
    meta = next(r for r in rows if r.get('event') == 'metadata')
    if meta.get('sha256') != HASH or meta.get('mode') != 'observe':
        raise ValueError('literal observed matching retail DLL required')
    if any(r.get('event') in ('trace-failed', 'error') for r in rows):
        raise ValueError('failed capture')
    scenes = {}
    current = None
    completed = set()
    marker_complete = False
    pending = {}
    for row in rows:
        event = row.get('event')
        if event == 'marker':
            marker_complete |= ' label=complete' in row['value']
            marker = re.search(r' s=(\d+) l=\d+ label=(\S+)', row['value'])
            if marker and marker[2] == 'begin-setup':
                current = int(marker[1])
                scenes[current] = dict(follower=None, rows=[], retired=[])
            elif marker and marker[2] == 'end-cleanup':
                completed.add(int(marker[1]))
                current = None
        if current is None:
            continue
        scene = scenes[current]
        if event == 'begin-target' and scene['follower'] is None:
            scene['follower'] = row['self']
        elif event == 'gtick' and len(row.get('members', [])) == 1:
            if row['members'][0].get('id') == scene['follower']:
                pending[(row['c'], row['g'])] = (scene, row)
        elif event == 'sample':
            selected = pending.pop((row['c'], row['g']), None)
            if selected:
                owner, before = selected
                member = before['members'][0]
                path = member['path']
                group = before['path']
                literal = [before['c'], before['cd'], before['unseen'], *member['pos'],
                           *path['times'], *path['dest'], *group['dest'], *group['times'],
                           *group['cnt'], *group['idx'], *path['cnt'], *path['idx'], *path['retry']]
                owner['rows'].append([v & 0xffffffff for v in literal])
    if not marker_complete or completed != set(range(9)) or set(scenes) != set(range(9)):
        raise ValueError('all nine complete scene windows required')
    if any(not scene['rows'] for scene in scenes.values()):
        raise ValueError('missing literal owner decisions')
    for owner, row in pending.values():
        owner['retired'].append(row['c'])
    return dict(sha256=hashlib.sha256(source_bytes).hexdigest(),
                metadata=meta, scenes=scenes)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('captures', type=Path, nargs='+')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        ap.error('new output required')
    runs = [capture(p) for p in args.captures]
    if len(runs) < 2:
        ap.error('two observed repeats required')
    for run in runs[1:]:
        if ({k: v['rows'] for k, v in run['scenes'].items()} !=
                {k: v['rows'] for k, v in runs[0]['scenes'].items()}):
            raise ValueError('literal owner snapshots differ; investigate instead of rebasing expectations')
    data = dict(schema=1, task='TARGET-02.1', fields=FIELDS, source_runs=runs)
    payload = (json.dumps(data, separators=(',', ':')) + '\n').encode()
    args.output.write_bytes(gzip.compress(payload, mtime=0) if args.output.suffix == '.gz' else payload)
    print(json.dumps({s: len(v['rows']) for s, v in runs[0]['scenes'].items()}))


if __name__ == '__main__':
    main()
