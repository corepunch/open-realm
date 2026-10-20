#!/usr/bin/env python3
"""Verify repeated public Cargo Drop policy and native coarse gate requests.

The observer only reads memory. Full semantic hook records repeat; all public
timer markers also equal an observer-free run. Forced states are not used.
This certifies policy production and consumption, not complete engine motion.
"""
import argparse
import gzip
import hashlib
import json
import re
from pathlib import Path

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def observe(path):
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    metadata = [r for r in rows if r['event'] == 'metadata']
    ends = [r for r in rows if r['event'] == 'trace-end']
    preload = [r for r in rows if r['event'] == 'preload-file']
    if len(metadata) != 1 or metadata[0]['sha256'] != SHA or metadata[0]['mode'] != 'observe':
        raise ValueError('invalid observed provenance')
    if len(ends) != 1 or not ends[0]['installed'] or ends[0]['tick'] != 401:
        raise ValueError('incomplete observer')
    if len(preload) != 1 or not preload[0]['complete'] or any(r['event'] in ('trace-failed', 'error') for r in rows):
        raise ValueError('failed capture')
    semantic = [r for r in rows if r['event'] in ('marker', 'group-route', 'coarse-request')]
    requests = [r for r in semantic if r['event'] == 'coarse-request']
    if len(requests) != 4 or [r['warp'] for r in requests] != [1, 0, 1, 1]:
        raise ValueError('target-class warp policy differs')
    if any(r['result'] != 1 for r in requests):
        raise ValueError('coarse request failed')
    for r in semantic:
        if r['event'] == 'group-route' and (r['flags'] & 0xffff) != (0x1811 if r['lane'] == 1 else 0x1801):
            raise ValueError('captured group policy differs')
    if any(bool(r['flags'] & 0x1000000) != bool(r['warp']) for r in requests):
        raise ValueError('gate marker presence differs')
    return semantic


def verify(first, repeat, control, control_preload):
    semantic = observe(first)
    if semantic != observe(repeat):
        raise ValueError('complete semantic repeat differs')
    rows = [json.loads(line) for line in control.read_text().splitlines()]
    meta = [r for r in rows if r['event'] == 'metadata']
    pre = [r for r in rows if r['event'] == 'preload-file']
    if len(meta) != 1 or meta[0]['mode'] != 'control' or meta[0]['sha256'] != SHA:
        raise ValueError('control provenance differs')
    if len(pre) != 1 or not pre[0]['complete'] or any(r['event'] in ('module', 'trace-end', 'trace-failed', 'error') for r in rows):
        raise ValueError('control contains hooks or failed')
    reference = next(json.loads(line) for line in first.read_text().splitlines()
                     if json.loads(line)['event'] == 'metadata')
    for path in (repeat, control):
        actual = next(json.loads(line) for line in path.read_text().splitlines()
                      if json.loads(line)['event'] == 'metadata')
        for key in ('sha256', 'source_sha256', 'map', 'timestamp', 'imageSize', 'prefix'):
            if actual[key] != reference[key]:
                raise ValueError('repeat/control inputs differ: ' + key)
    root = Path(__file__).resolve().parents[3]
    for name in ('group032_capture.py', 'group032_make_map.py', 'form013_warp_observer.js'):
        source = root / 'tools/frida/research' / name
        if hashlib.sha256(source.read_bytes()).hexdigest() != reference['source_sha256'][name]:
            raise ValueError('capture source hash differs: ' + name)
    raw = control_preload.read_bytes()
    if hashlib.sha256(raw).hexdigest() != pre[0]['sha256']:
        raise ValueError('control preload hash differs')
    markers = re.findall(r'call Preload\( "(F13 [^"\r\n]*)" \)', raw.decode('utf-8', 'replace'))
    if markers != [r['value'] for r in semantic if r['event'] == 'marker']:
        raise ValueError('observer-free public timeline differs')
    return dict(binary_sha256=SHA, passed=True, requests=4, repeats=2,
                markers=len(markers), semantic=semantic,
                scope='Public ground Footman target with no added ability, Adro, Amed, Atdp; '
                      'one active Way Gate. Captured target policy, coarse warp argument, '
                      'gate marker presence and complete public marker/control timeline. '
                      'Not complete engine trajectories or stock flying transport traversal.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('first', 'repeat', 'control', 'control-preload', 'expected', 'report'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.first, args.repeat, args.control, args.control_preload)
    if json.loads(gzip.decompress(args.expected.read_bytes())) != result:
        raise ValueError('frozen public policy witness differs')
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print('verified requests=4 repeats=2 markers=' + str(result['markers']))


if __name__ == '__main__':
    main()
