#!/usr/bin/env python3
"""Strictly verify complete archived dynamic-blocker/yield streams and exports."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'tools/frida/research'))
from route03_expected import digest, markers_from_preload, stream, visits
from yield163_engine_fixture import render


def validate_rows(rows, spec, binary):
    metadata = [r for r in rows if r.get('event') == 'metadata']
    if len(metadata) != 1 or rows[0] != metadata[0]:
        raise ValueError('one leading metadata record required')
    meta = {k: v for k, v in metadata[0].items() if k not in ('event', 'pid')}
    if (meta != spec['metadata'] or meta.get('sha256') != binary or
            meta.get('owned') is not True or meta.get('mode') != spec['mode'] or
            spec['mode'] not in ('observe', 'control')):
        raise ValueError('capture provenance differs')
    if any(r.get('type') == 'error' or r.get('event') in ('error', 'trace-failed') for r in rows):
        raise ValueError('failed capture')
    footers = [r for r in rows if r.get('event') == 'preload-file']
    if len(footers) != 1 or footers[0].get('complete') is not True:
        raise ValueError('incomplete public scene')
    if spec['mode'] == 'observe':
        ends = [r for r in rows if r.get('event') == 'trace-end']
        if len(ends) != 1 or not all(ends[0].get(k) is True for k in ('installed', 'finished')):
            raise ValueError('incomplete observer')
        if sum(r.get('event') == 'marker' for r in rows) != footers[0]['markers']:
            raise ValueError('public marker count differs')
    elif any(r.get('event') not in ('metadata', 'loading-key', 'preload-file', 'owned-process-already-exited') for r in rows):
        raise ValueError('instrumented control')
    return footers[0]


def verify(frozen, archive):
    public = {}; normalized = {}; steps = 0
    for name, spec in frozen['captures'].items():
        path = archive/name; raw = path.read_bytes()
        if len(raw) != spec['bytes'] or hashlib.sha256(raw).hexdigest() != spec['sha256']:
            raise ValueError('capture pin differs: '+name)
        rows = [json.loads(line) for line in raw.splitlines()]
        footer = validate_rows(rows, spec, frozen['binary_sha256'])
        preload = path.with_name(path.stem+'-preload.txt')
        if hashlib.sha256(preload.read_bytes()).hexdigest() != footer['sha256']:
            raise ValueError('public preload pin differs')
        markers = markers_from_preload(preload)
        if len(markers) != footer['markers'] or digest(markers) != spec['marker_digest']:
            raise ValueError('public timeline differs')
        public[name] = markers
        if spec['mode'] == 'observe':
            if [r['value'] for r in rows if r.get('event') == 'marker'] != markers:
                raise ValueError('observer public timeline differs')
            items, _ = stream(path)
            if digest(items) != spec['normalized_digest']:
                raise ValueError('complete normalized stream differs')
            normalized[name] = items
            steps += sum(r.get('event') == 'step-enter' for r in items)
    for left, right in frozen['controls']:
        if public[left] != public[right]:
            raise ValueError('observer-free control differs')
    for left, right in frozen['repeats']:
        if normalized[left] != normalized[right]:
            raise ValueError('complete repeated stream differs')
    windows = 0
    for name, task in frozen['original_fixtures'].items():
        raw = (ROOT/name).read_bytes()
        original = json.loads(gzip.decompress(raw) if name.endswith('.gz') else raw)
        if original['binary_sha256'] != frozen['binary_sha256']:
            raise ValueError('original fixture binary differs')
        for case in original['cases'].values():
            items = normalized[task+'/captures/'+case['capture']]
            if digest(items) != case['normalized_digest'] or visits(items, *case['window']) != case['visits']:
                raise ValueError('original owner window differs')
            windows += len(case['visits'])
    scenes = engine_steps = 0
    for name, pin in frozen['headers'].items():
        header = (ROOT/name).read_text()
        if hashlib.sha256(header.encode()).hexdigest() != pin:
            raise ValueError('engine header pin differs')
        dynamic = 'dynamic' in name
        if render(archive/('ROUTE-03.1' if dynamic else 'ROUTE-05.2'), dynamic) != header:
            raise ValueError('engine header differs from complete original steps')
        scenes += header.count('static uint32_t const ')
        engine_steps += sum(line.startswith('    {0x') for line in header.splitlines())
    for name, pin in frozen['repository_sources'].items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest() != pin:
            raise ValueError('repository source pin differs')
    return dict(passed=True, captures=len(normalized), controls=len(frozen['controls']),
                repeats=len(frozen['repeats']), original_owner_records=windows,
                observed_member_steps=steps, engine_scenes=scenes, engine_owner_steps=engine_steps)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--expected', type=Path, required=True)
    ap.add_argument('--archive', type=Path, required=True)
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    result = verify(json.loads(args.expected.read_text()), args.archive)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
