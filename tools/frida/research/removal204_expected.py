#!/usr/bin/env python3
"""Freeze complete accepted removal captures without replacing prior fixtures."""
import argparse
import gzip
import json
from pathlib import Path
from removal204_verify import digest, markers, stages, validate


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.output.exists():
        p.error('fixture must be new')
    spec = dict(version=1, task='SEP-01.2/removal',
                binary_sha256='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236',
                map=dict(name='RS-Removal204-Enabled.w3m',
                         sha256=digest((a.archive / 'RS-Removal204-Enabled.w3m').read_bytes()),
                         units={'hREM': dict(urpo=2, urpp=17, urpg=17, urpr=17)}), captures=[])
    for name in ('enabled-observe-1.jsonl', 'enabled-observe-3.jsonl', 'enabled-control-1.jsonl'):
        raw = (a.archive / name).read_bytes()
        preload = (a.archive / (Path(name).stem + '-preload.txt')).read_bytes()
        spec['captures'].append(dict(name=name, sha256=digest(raw), bytes=len(raw),
                                     rows=[json.loads(s) for s in raw.splitlines()],
                                     preload=preload.decode(), preload_sha256=digest(preload)))
    spec['stages'] = stages(spec['captures'][0]['rows'])
    spec['public_markers'] = markers(spec['captures'][0]['preload'])
    spec['excluded'] = []
    for name, reason in [('enabled-observe-2.jsonl', 'Remote attach timed out; no installed observer or complete Preload.'),
                         ('observe-1.jsonl', 'Initial stock hfoo scene had no enabled repulsors.'),
                         ('observe-2.jsonl', 'Initial stock hfoo repeat had no enabled repulsors.'),
                         ('control-1.jsonl', 'Observer-free initial stock hfoo scene, excluded from enabled claims.')]:
        raw = (a.archive / name).read_bytes()
        spec['excluded'].append(dict(name=name, sha256=digest(raw), bytes=len(raw), reason=reason))
    spec['exclusions'] = [
        'Pending-removal membership only; no full counted-suppression producer closure.',
        'Retail releases its suppression and briefly recreates separation before final mover destruction. '
        'This internal retirement interval is retained in the complete captures and remains outside this engine integration.',
        'No retail save/load, full RNG stream, motion trajectory or arbitrary removal-callback admission claim.',
        'Engine duplicate removal, type-refresh and save/load are lifecycle regressions; they have no new public retail witness here.',
        'Native pointers and undefined upper stack bytes are normalized only for repeat comparison; all raw fields remain frozen.']
    validate(spec, a.archive)
    a.output.write_bytes(gzip.compress((json.dumps(spec, indent=2) + '\n').encode(), mtime=0))
    print(json.dumps(dict(sha256=digest(a.output.read_bytes()), captures=3, native_stages=len(spec['stages']))))


if __name__ == '__main__':
    main()
