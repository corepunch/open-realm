#!/usr/bin/env python3
"""Verify the public Mechanical Critter producer and its latent separation category."""
import argparse
import copy
import hashlib
import json
import re
from pathlib import Path

EVENTS = ('marker', 'choose-begin', 'candidate', 'range-begin', 'range-end', 'choose-end',
          'refresh', 'configure', 'apply-begin', 'apply-end', 'inverse-begin', 'inverse-end')


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def stages(rows):
    result = copy.deepcopy([r for r in rows if r['event'] in EVENTS])
    # 1710e0 reads AL for these three setters. Native upper stack bytes are
    # uninitialized; retain them in the pinned raw captures, never compare them.
    for row in result:
        if row['event'] == 'configure':
            row['args'][1:] = [v & 255 for v in row['args'][1:]]
    begins = [r for r in result if r['event'] == 'choose-begin']
    if len(begins) != 2 or any(r['level'] != -1 or r['tileset'] != 88 or r['args'] != [1, 0, 1] for r in begins):
        raise ValueError('critter filter changed')
    candidates = [r['code'] for r in result if r['event'] == 'candidate']
    order = [int.from_bytes(s.encode(), 'big') for s in ('nfro', 'nech', 'necr', 'nrac', 'ndog', 'nshe')]
    if candidates != order * 2:
        raise ValueError('UnitUI registration order differs')
    ranges = [r for r in result if r['event'] == 'range-begin']
    if len(ranges) != 2 or any((r['index'], r['span'], r['caller']) != (33, 6, '672f14') for r in ranges):
        raise ValueError('critter RNG ownership differs')
    if [r['value'] for r in result if r['event'] == 'range-end'] != [5, 2]:
        raise ValueError('critter draw sequence differs')
    words = [r.get('sep') for r in result if r['event'] == 'configure']
    enabled = words == [0x20300000, 0x2f300000, 0x22300000]
    if not enabled and words != [None, None, None]:
        raise ValueError('category refresh words differ')
    for before, after, flags in [('apply-begin', 'apply-end', [0, 1]), ('inverse-begin', 'inverse-end', [1, 0])]:
        pair = [r['unit'] for r in result if r['event'] in (before, after)]
        if len(pair) != 2 or [r['flag'] for r in pair] != flags or pair[0].get('sep') != pair[1].get('sep'):
            raise ValueError('apply/inverse refreshed separation or changed flag order')
    return result


def verify(expected, archive):
    for scene in expected['scenes']:
        observed, public = [], []
        for name, pin in scene['captures'].items():
            raw = (archive / name).read_bytes()
            if len(raw) != pin['bytes'] or digest(raw) != pin['sha256']:
                raise ValueError('capture pin differs')
            rows = [json.loads(line) for line in raw.splitlines()]
            meta = {k: v for k, v in rows[0].items() if k != 'pid'}
            if meta != pin['metadata'] or meta['sha256'] != expected['binary_sha256'] or not meta['owned']:
                raise ValueError('capture provenance differs')
            if any(r.get('type') == 'error' or r['event'] in ('trace-failed', 'observer-error') for r in rows):
                raise ValueError('failed capture')
            preload = (archive / (Path(name).stem + '-preload.txt')).read_bytes()
            footer = [r for r in rows if r['event'] == 'preload-file']
            if len(footer) != 1 or not footer[0]['complete'] or digest(preload) != footer[0]['sha256']:
                raise ValueError('preload pin differs')
            markers = re.findall(r'call Preload\( "(M201 [^"\r\n]+)" \)', preload.decode())
            if len(markers) != 57 or footer[0]['markers'] != 57:
                raise ValueError('incomplete public continuation')
            public.append(markers)
            if meta['mode'] == 'observe':
                if [r['value'] for r in rows if r['event'] == 'marker'][2:] != markers:
                    raise ValueError('observer differs from public Preload')
                finish = [r for r in rows if r['event'] == 'trace-end']
                if len(finish) != 1 or finish[0]['installed'] is not True:
                    raise ValueError('observer not completed')
                observed.append(stages(rows))
            elif meta['mode'] != 'control' or any(r['event'] not in
                    ('metadata', 'loading-key', 'control-start-file', 'preload-file') for r in rows):
                raise ValueError('control contains instrumentation')
        if len(observed) != 2 or observed[0] != observed[1] or observed[0] != scene['stages']:
            raise ValueError('repeated retail stages differ')
        if len(public) != 3 or any(p != public[0] for p in public) or public[0] != scene['public_markers']:
            raise ValueError('observer-free continuation differs')
        if digest((archive / scene['map']).read_bytes()) != scene['map_sha256']:
            raise ValueError('map pin differs')
    pin = expected['word_capture']
    raw = (archive / pin['name']).read_bytes()
    if len(raw) != pin['bytes'] or digest(raw) != pin['sha256']:
        raise ValueError('public position capture pin differs')
    rows = [json.loads(line) for line in raw.splitlines()]
    if {k: v for k, v in rows[0].items() if k != 'pid'} != pin['metadata']:
        raise ValueError('public position provenance differs')
    if stages(rows) != expected['scenes'][1]['stages']:
        raise ValueError('position hooks changed producer continuation')
    queries = [r for r in rows if r['event'] in ('public-x', 'public-y')]
    valid = [r for r in queries if r['handle']]
    if queries != pin['queries'] or len(queries) != 112 or len(valid) != 82:
        raise ValueError('public position queries differ')
    if len({r['handle'] for r in valid}) != 1 or any(r['word'] !=
            (0x4431f000 if r['event'] == 'public-x' else 0x44000000) for r in valid):
        raise ValueError('public position return words differ')
    footer = [r for r in rows if r['event'] == 'preload-file']
    finish = [r for r in rows if r['event'] == 'trace-end']
    preload = (archive / (Path(pin['name']).stem + '-preload.txt')).read_bytes()
    if len(footer) != 1 or not footer[0]['complete'] or digest(preload) != footer[0]['sha256'] or \
            len(finish) != 1 or not finish[0]['installed']:
        raise ValueError('public position capture incomplete')
    markers = re.findall(r'call Preload\( "(M201 [^"\r\n]+)" \)', preload.decode())
    if markers != expected['scenes'][1]['public_markers']:
        raise ValueError('position hooks changed observer-free public continuation')
    return dict(passed=True, status='live-mechanical-critter-category', scenes=2,
                observations=5, controls=2, public_markers=114, producer_rows=178,
                position_queries=112, critter_position_words=82,
                binary_sha256=expected['binary_sha256'])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('expected', 'archive', 'output'):
        ap.add_argument('--' + name, type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        ap.error('report must be fresh')
    report = verify(json.loads(args.expected.read_text()), args.archive)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
