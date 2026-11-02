#!/usr/bin/env python3
"""Freeze physical flying Follow states from read-only retail owner visits."""
import hashlib
import json
import re
from pathlib import Path


def owner_rows(rows):
    scene = -1
    out = []
    for row in rows:
        if row.get('event') == 'marker':
            scene = int(re.search(r' s=(-?\d+)', row['value'])[1])
        if scene != 5 or row.get('event') != 'gtick' or not row['flags'] & 0x1000:
            continue
        if (row['count'] != 1 or row['members'][0].get('m') is None or
                row['members'][0]['grp'] != row['id']):
            continue
        member = row['members'][0]
        path, own = row['path'], member['path']
        out.append([row['c'], row['cd'], row['unseen'], *path['dest'], *path['times'],
                    path['cnt'][1], path['idx'][1], *member['pos'], *member['vel'], member['range'],
                    *own['dest'], *own['times'], *own['cnt'], *own['idx'], *own['retry'], row['flags'] & 1])
    if len(out) != 617 or sum(not r[-1] for r in out) != 93:
        raise ValueError('incomplete flying approach/persistent lifetime')
    return out


def render(rows):
    return '\n'.join([
        '/* Raw read-only TARGET-02.1 scene5; two complete observed repeats agree. */',
        '/* counter, refresh, unseen, group goal/times/count/index, member pose/velocity/range,',
        ' * member goal/times/counts/indices/delay/retry, persistent. */',
        'static uint32_t const follow186_flying[][25]={',
        *['    {' + ','.join(f'0x{x & 0xffffffff:08x}u' for x in r) + '},' for r in rows],
        '};', ''])


def verify(expected, archive, header):
    public = []
    observations = []
    for name, pin in expected['captures'].items():
        path = archive / name
        raw = path.read_bytes()
        if len(raw) != pin['bytes'] or hashlib.sha256(raw).hexdigest() != pin['sha256']:
            raise ValueError('capture pin differs: ' + name)
        rows = [json.loads(line) for line in raw.splitlines()]
        meta = {k: v for k, v in rows[0].items() if k != 'pid'}
        if meta != pin['metadata'] or not meta.get('owned') or meta['sha256'] != expected['binary_sha256']:
            raise ValueError('capture provenance differs')
        if any(r.get('type') == 'error' or r.get('event') in ('error', 'trace-failed') for r in rows):
            raise ValueError('failed observer')
        ends = [r for r in rows if r.get('event') == 'preload-file']
        if len(ends) != 1 or ends[0].get('complete') is not True:
            raise ValueError('incomplete public probe')
        preload = path.with_name(path.stem + '-preload.txt').read_bytes()
        if hashlib.sha256(preload).hexdigest() != ends[0]['sha256']:
            raise ValueError('public preload pin differs')
        markers = re.findall(r'call Preload\( "(T021 [^"\r\n]*)" \)', preload.decode(errors='replace'))
        if len(markers) != 2085 or len(markers) != ends[0]['markers']:
            raise ValueError('public timeline incomplete')
        public.append(markers)
        if meta['mode'] == 'observe':
            finish = [r for r in rows if r.get('event') == 'trace-end']
            if len(finish) != 1 or finish[0].get('installed') is not True:
                raise ValueError('observer not completed')
            if [r['value'] for r in rows if r.get('event') == 'marker'] != markers:
                raise ValueError('observer marker stream differs')
            observations.append(owner_rows(rows))
        elif meta['mode'] != 'control' or any(r.get('event') not in ('metadata', 'loading-key', 'controller-end', 'preload-file') for r in rows):
            raise ValueError('control was instrumented')
    if len(observations) != 2 or observations[0] != observations[1] or any(p != public[0] for p in public):
        raise ValueError('retail repeats/control differ')
    raw = header.read_bytes()
    if hashlib.sha256(raw).hexdigest() != expected['header_sha256'] or raw.decode() != render(observations[0]):
        raise ValueError('engine fixture differs from retail')
    return dict(status='live-flying-follow', passed=True, owner_rows=617, approach_rows=93,
                persistent_rows=524, public_markers=2085, controls=1)


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
