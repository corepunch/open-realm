#!/usr/bin/env python3
"""Verify Captain policy speed/period evidence and the public observer control."""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DLL = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def normalize(rows):
    updates = [r for r in rows if r.get('event') == 'captain-call' and r.get('name') == 'periodic-update']
    owners = []
    for row in updates:
        owner = row['before']['captain']
        if owner not in owners:
            owners.append(owner)
    if len(owners) != 2 or len(updates) != 128:
        raise ValueError('missing periodic Captain owners/visits')
    out = []
    def state(s):
        return {k: s[k] for k in ('state', 'player', 'flags', 'order', 'counts', 'request', 'requestRange', 'home', 'targets')}
    for row in rows:
        if row.get('event') == 'policy-constants':
            out.append({k: v for k, v in row.items() if k != 'ms'})
        elif row.get('event') == 'captain-speed':
            out.append(dict(event='speed', owner=owners.index(row['captain']), counter=row['counter'], word=row['word']))
        elif row.get('event') == 'captain-call' and row.get('name') == 'periodic-update':
            out.append(dict(event='update', owner=owners.index(row['before']['captain']), counter=row['counter'],
                            caller=row['caller'], before=state(row['before']), after=state(row['after'])))
    if sum(r['event'] == 'policy-constants' for r in out) != 1 or sum(r['event'] == 'speed' for r in out) != 12:
        raise ValueError('missing constants/speed publications')
    return out


def check_rows(rows, mode, frozen):
    meta = rows[0] if rows else {}
    if (meta.get('event') != 'metadata' or sum(r.get('event') == 'metadata' for r in rows) != 1 or
        meta.get('owned') is not True or meta.get('mode') != mode or meta.get('sha256') != DLL or
        meta.get('source_sha256') != frozen[mode + '_sources'] or
        meta.get('task') != ('payoff159' if mode == 'observe' else 'GROUP-03.4.7.3')):
        raise ValueError('capture provenance differs')
    if any(r.get('type') == 'error' or r.get('event') in ('error', 'trace-failed') for r in rows):
        raise ValueError('failed capture')
    public = [r for r in rows if r.get('event') == 'preload-file']
    if len(public) != 1 or not public[0].get('complete') or public[0].get('markers') != 373:
        raise ValueError('incomplete public scene')
    if mode == 'observe':
        ends = [r for r in rows if r.get('event') == 'trace-end']
        if len(ends) != 1 or not ends[0].get('installed'):
            raise ValueError('observer incomplete')
        if [r['value'] for r in rows if r.get('event') == 'marker'] != frozen['public_markers']:
            raise ValueError('observed public timeline differs')
        if normalize(rows) != frozen['normalized']:
            raise ValueError('speed/periodic policy differs')
    elif any(r.get('event') in ('trace-end', 'marker', 'captain-call', 'snapshot', 'captain-speed', 'policy-constants') for r in rows):
        raise ValueError('observer-free control contains instrumentation')


def verify(path, mode, frozen):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != frozen['captures'][path.name]:
        raise ValueError('capture pin differs')
    rows = [json.loads(line) for line in data.splitlines()]
    check_rows(rows, mode, frozen)
    raw = path.with_name(path.stem + '-preload.txt').read_bytes()
    marker = next(r for r in rows if r.get('event') == 'preload-file')
    if hashlib.sha256(raw).hexdigest() != marker['sha256']:
        raise ValueError('public file pin differs')
    # PreloadEnd embeds wall time. Only public Preload records are simulation evidence.
    markers = re.findall(r'call Preload\( "(RSH [^"\r\n]*)" \)', raw.decode())
    if markers != frozen['public_markers']:
        raise ValueError('observer-free public timeline differs')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expected', type=Path, required=True)
    parser.add_argument('--repeat', type=Path, action='append', required=True)
    parser.add_argument('--control', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if len(args.repeat) != 2 or len(set(args.repeat)) != 2:
        parser.error('two distinct observed repeats required')
    frozen = json.loads(args.expected.read_text())
    for source, digest in frozen['repository_sources'].items():
        if hashlib.sha256((ROOT / source).read_bytes()).hexdigest() != digest:
            raise ValueError('repository observer/controller source differs')
    for repeat in args.repeat:
        verify(repeat, 'observe', frozen)
    verify(args.control, 'control', frozen)
    report = dict(passed=True, repeats=2, public_markers=373, speed_publications=12, periodic_updates=128)
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
