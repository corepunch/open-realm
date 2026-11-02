#!/usr/bin/env python3
"""Verify repeated public ground-to-air pursuit and separate path-region ownership."""
import hashlib
import json
import re
from pathlib import Path


def owner_rows(rows):
    scene = -1
    follower, target = [], []
    for row in rows:
        if row.get('event') == 'marker':
            scene = int(re.search(r' s=(-?\d+)', row['value'])[1])
        if scene != 7 or row.get('event') != 'gtick' or row['count'] != 1:
            continue
        member = row['members'][0]
        if member.get('m') is None or member['grp'] != row['id']:
            continue
        if not row['flags'] & 0x1000:
            target.append([row['c'], *member['pos'], *member['vel']])
            continue
        path, own = row['path'], member['path']
        follower.append([row['c'], row['cd'], row['unseen'], *path['dest'], *path['times'],
                         path['cnt'][1], path['idx'][1], *member['pos'], *member['vel'], member['range'],
                         *own['dest'], *own['times'], *own['cnt'], *own['idx'], *own['retry'], row['flags'] & 1])
    if len(follower) != 616 or sum(not r[-1] for r in follower) != 202 or len(target) != 621:
        raise ValueError('incomplete ground-to-air owner lifetime')
    return follower, target


def render(follower, target):
    def table(name, rows, width):
        return [f'static uint32_t const {name}[][{width}]={{',
                *['    {' + ','.join(f'0x{x & 0xffffffff:08x}u' for x in r) + '},' for r in rows], '};']
    return '\n'.join([
        '/* Raw read-only TARGET-02.1 scene7; repeated public ground-following-air stream. */',
        *table('follow187_ground_air', follower, 25), '', *table('follow187_target', target, 5), ''])


def region_rows(rows):
    groups = {r['path'] for r in rows if r.get('event') == 'group-path-regions'}
    regions = [r for r in rows if r.get('event') in
               ('group-path-regions', 'coarse-path-regions', 'set-self-region')]
    counts = {event: sum(r['event'] == event for r in regions) for event in
              ('group-path-regions', 'coarse-path-regions', 'set-self-region')}
    if list(counts.values()) != [1239, 73, 2]:
        raise ValueError('incomplete region boundaries')
    coarse = [r for r in regions if r['event'] == 'coarse-path-regions']
    if sum(r['path'] in groups for r in coarse) != 40:
        raise ValueError('coarse owner classification differs')
    for row in regions:
        if row['event'] == 'group-path-regions' and row['self'] != '0x0':
            raise ValueError('group path acquired a member self region')
        if row['event'] == 'coarse-path-regions' and ((row['self'] == '0x0') != (row['path'] in groups)):
            raise ValueError('coarse self region differs from path ownership')
        if row['event'] == 'set-self-region' and (row['region'] == '0x0' or row['caller'] != 0x170ad9):
            raise ValueError('self region has an unexpected producer')
    # Allocation addresses vary between processes. Preserve identity relations,
    # encounter order and all non-address values rather than comparing addresses.
    identities = {}
    def logical(value):
        if isinstance(value, str) and value.startswith('0x'):
            if value == '0x0':
                return 0
            return identities.setdefault(value, len(identities) + 1)
        return value
    return [{k: logical(v) for k, v in r.items() if k != 'ms'} for r in regions]


def capture(path, pin, binary_hash, prefix, marker_count):
    raw = path.read_bytes()
    if len(raw) != pin['bytes'] or hashlib.sha256(raw).hexdigest() != pin['sha256']:
        raise ValueError('capture pin differs: ' + str(path))
    rows = [json.loads(line) for line in raw.splitlines()]
    meta = {k: v for k, v in rows[0].items() if k != 'pid'}
    if meta != pin['metadata'] or not meta.get('owned') or meta['sha256'] != binary_hash:
        raise ValueError('capture provenance differs')
    if any(r.get('type') == 'error' or r.get('event') in ('error', 'trace-failed') for r in rows):
        raise ValueError('failed observer')
    ends = [r for r in rows if r.get('event') == 'preload-file']
    if len(ends) != 1 or ends[0].get('complete') is not True:
        raise ValueError('incomplete public probe')
    preload = path.with_name(path.stem + '-preload.txt').read_bytes()
    if hashlib.sha256(preload).hexdigest() != ends[0]['sha256']:
        raise ValueError('public preload pin differs')
    markers = re.findall(r'call Preload\( "(' + re.escape(prefix) + r'[^"\r\n]*)" \)',
                         preload.decode(errors='replace'))
    if len(markers) != marker_count or len(markers) != ends[0]['markers']:
        raise ValueError('public timeline incomplete')
    if meta['mode'] == 'observe':
        finish = [r for r in rows if r.get('event') == 'trace-end']
        if len(finish) != 1 or finish[0].get('installed') is not True:
            raise ValueError('observer not completed')
        if [r['value'] for r in rows if r.get('event') == 'marker'] != markers:
            raise ValueError('observer marker stream differs')
        if any(k.endswith('-dropped') and v for k, v in finish[0]['counts'].items()):
            raise ValueError('observer dropped events')
    elif meta['mode'] != 'control' or any(r.get('event') not in
            ('metadata', 'loading-key', 'loading-key-skipped', 'controller-end', 'preload-file') for r in rows):
        raise ValueError('control was instrumented')
    return rows, markers, meta['mode']


def verify(expected, archive, header):
    observations, public = [], []
    for name, pin in expected['captures'].items():
        rows, markers, mode = capture(archive / name, pin, expected['binary_sha256'], 'T021 ', 2085)
        public.append(markers)
        if mode == 'observe':
            observations.append(owner_rows(rows))
    if len(observations) != 2 or observations[0] != observations[1] or any(p != public[0] for p in public):
        raise ValueError('retail pursuit repeats/control differ')
    raw = header.read_bytes()
    if hashlib.sha256(raw).hexdigest() != expected['header_sha256'] or raw.decode() != render(*observations[0]):
        raise ValueError('engine fixture differs from retail')
    ownership, public = [], []
    for name, pin in expected['region_captures'].items():
        rows, markers, mode = capture(archive / name, pin, expected['binary_sha256'], 'F187 ', 211)
        public.append(markers)
        if mode == 'observe':
            ownership.append(region_rows(rows))
    if len(ownership) != 2 or ownership[0] != ownership[1] or any(p != public[0] for p in public):
        raise ValueError('region repeats/control differ')
    return dict(status='live-ground-air-follow', passed=True, owner_rows=616, approach_rows=202,
                persistent_rows=414, target_rows=621, public_markers=2085, region_markers=211,
                group_boundaries=1239, group_requests=40, member_requests=33, controls=2)


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--expected', type=Path, required=True)
    ap.add_argument('--archive', type=Path, required=True)
    ap.add_argument('--header', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    try:
        report = verify(json.loads(args.expected.read_text()), args.archive, args.header)
    except (ValueError, KeyError, OSError) as error:
        report = dict(status='failed', passed=False, error=str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    raise SystemExit(0 if report['passed'] else 1)
