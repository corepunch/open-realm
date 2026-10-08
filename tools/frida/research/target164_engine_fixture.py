#!/usr/bin/env python3
"""Export raw, read-only retail target-follow owner states; never engine output."""
import argparse
import hashlib
import json
import re
from pathlib import Path


def owner_rows(rows):
    entries = []
    scene = -1
    for row in rows:
        if row.get('event') == 'marker':
            scene = int(re.search(r' s=(-?\d+)', row['value'])[1])
        if scene != 0 or row.get('event') != 'gtick' or row['flags'] & 1 or not row['flags'] & 0x1000:
            continue
        members = row['members']
        if len(members) != 1 or members[0].get('m') is None:
            continue
        member = members[0]
        path, own = row['path'], member['path']
        values = [row['c'], row['cd'], row['unseen'], *path['dest'], *path['times'],
                  path['cnt'][1], path['idx'][1], *member['pos'], *member['vel'], member['range'],
                  *own['dest'], *own['times'], *own['cnt'], *own['idx'], *own['retry']]
        assert len(values) == 24
        entries.append(values)
    assert len(entries) == 235
    return entries


def render(root):
    raw = (root / 'captures/observe-1.jsonl').read_bytes()
    rows = [json.loads(line) for line in raw.splitlines()]
    entries = owner_rows(rows)
    out = ['/* Raw read-only retail TARGET-02.1 scene 0, complete initial approach. */',
           f'/* Capture SHA256 {hashlib.sha256(raw).hexdigest()} */',
           '/* counter, refresh, unseen, group destination/times/count/index,',
           ' * member pose/velocity/range, path destination/times/counts/indices/delay/retry. */',
           'static uint32_t const target164_approach[][24]={']
    out += ['    {' + ','.join(f'0x{v & 0xffffffff:08x}u' for v in row) + '},' for row in entries]
    last = next(r['members'][0] for r in rows if r.get('event') == 'gtick' and r['c'] == entries[-1][0]+1
                and r['flags'] & 0x1000 and r['flags'] & 1 and r['count'] == 1)
    values = [*last['pos'], *last['vel']]
    return '\n'.join(out + ['};', 'static uint32_t const target164_finish[]={' +
                            ','.join(f'0x{v:08x}u' for v in values) + '};', ''])


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--research', type=Path, required=True)
    ap.add_argument('--header', type=Path, required=True)
    args = ap.parse_args()
    args.header.write_text(render(args.research))
