#!/usr/bin/env python3
"""Verify original Captain speed queries, repeat timelines and uninstrumented controls."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from verify_wc3_pathing_numeric import add, bits, multiply, subtract

DLL = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
ADRO = 0x4164726f


def value(word):
    return struct.unpack('<f', struct.pack('<I', word))[0]


def model(row):
    before = row['before']
    if before['flags'] & 2:
        return 0x43fa0000
    speed = min(row['inputs'], key=value, default=0x461c3c00)
    ordinary = any(a['kind'] == ADRO and not a['present'] for a in row['abilities'])
    count, entered, additional = (before['counts'][i] for i in (1, 3, 5))
    # These public point-policy fixtures have no retained combat target.
    if any(pair != [0xffffffff, 0xffffffff] for pair in before['targets'][1:]):
        raise ValueError('uncovered retained target policy')
    near = all(value(subtract(p, h) & 0x7fffffff) < value(0x3a83126f)
               for p, h in zip(before['request'], before['home']))
    if ordinary and additional + entered < count and not near:
        # All captured counts are small exact integer conversions. Numeric
        # helper boundary tests do not establish larger public roster domains.
        factor = add(multiply(bits(count), 0x3c321643), 0x3f3d37a7)
        speed = multiply(speed, factor)
    return speed


def normalize(rows):
    result = []
    for row in rows:
        if row.get('event') == 'policy-constants':
            result.append({k: v for k, v in row.items() if k != 'ms'})
        if row.get('event') == 'captain-speed':
            if model(row) != row['word']:
                raise ValueError('original speed differs from decoded policy')
            state = {k: row['before'][k] for k in
                     ('state', 'player', 'flags', 'counts', 'request', 'requestRange', 'home', 'targets')}
            result.append(dict(event='speed', counter=row['counter'], before=state,
                               inputs=row['inputs'], abilities=row['abilities'], word=row['word']))
    if sum(r['event'] == 'policy-constants' for r in result) != 1:
        raise ValueError('missing runtime constants')
    return result


def check_rows(rows, mode, scene):
    meta = rows[0] if rows else {}
    if (meta.get('event') != 'metadata' or meta.get('task') != 'payoff160' or
        meta.get('owned') is not True or meta.get('mode') != mode or meta.get('sha256') != DLL or
        meta.get('source_sha256') != scene['sources'] or
        sum(r.get('event') == 'metadata' for r in rows) != 1):
        raise ValueError('capture provenance differs')
    if any(r.get('type') == 'error' or r.get('event') in ('error', 'trace-failed') for r in rows):
        raise ValueError('failed capture')
    public = [r for r in rows if r.get('event') == 'preload-file']
    if len(public) != 1 or public[0].get('complete') is not True or public[0].get('markers') != len(scene['public_markers']):
        raise ValueError('incomplete public scene')
    if mode == 'observe':
        ends = [r for r in rows if r.get('event') == 'trace-end']
        if len(ends) != 1 or ends[0].get('installed') is not True:
            raise ValueError('observer incomplete')
        if [r['value'] for r in rows if r.get('event') == 'marker'] != scene['public_markers']:
            raise ValueError('public timeline differs')
        if normalize(rows) != scene['normalized']:
            raise ValueError('speed policy inputs or publications differ')
    elif any(r.get('event') in ('trace-end', 'module', 'marker', 'captain-call', 'snapshot', 'captain-speed', 'policy-constants') for r in rows):
        raise ValueError('control contains instrumentation')


def verify(root, capture, mode, scene):
    path = root / capture
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != scene['captures'][capture]:
        raise ValueError('capture pin differs')
    rows = [json.loads(line) for line in raw.splitlines()]
    check_rows(rows, mode, scene)
    preload = path.with_name(path.stem + '-preload.txt').read_bytes()
    footer = next(r for r in rows if r.get('event') == 'preload-file')
    if hashlib.sha256(preload).hexdigest() != footer['sha256']:
        raise ValueError('preload pin differs')
    markers = re.findall(r'call Preload\( "(RSH [^"\r\n]*)" \)', preload.decode())
    if markers != scene['public_markers']:
        raise ValueError('observer-free public timeline differs')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expected', type=Path, required=True)
    parser.add_argument('--captures', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    frozen = json.loads(args.expected.read_text())
    for source, digest in frozen['repository_sources'].items():
        if hashlib.sha256((ROOT / source).read_bytes()).hexdigest() != digest:
            raise ValueError('repository source pin differs')
    publications = markers = 0
    for scene in frozen['scenes'].values():
        if len(scene['repeats']) != 2 or len(set(scene['repeats'])) != 2:
            raise ValueError('two distinct complete repeats required')
        for repeat in scene['repeats']:
            verify(args.captures, repeat, 'observe', scene)
        verify(args.captures, scene['control'], 'control', scene)
        publications += sum(r['event'] == 'speed' for r in scene['normalized'])
        markers += len(scene['public_markers'])
    report = dict(passed=True, scenes=len(frozen['scenes']), repeats=2,
                  speed_publications=publications, public_markers=markers)
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
