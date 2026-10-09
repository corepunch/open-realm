#!/usr/bin/env python3
"""Freeze ORDER-01.10 / ORDER-01.18 expected results from summarize outputs.

  order0110_expected.py --task ORDER-01.10 --scene v3a=summary-v3a.json ... --output expected-ORDER-01.10.json

Each scene keeps: capture/control hashes and identity flags, per-case public transitions (tick, label, order,
x, y, accepted), damage rows (tick, source current order, amount), and the complete normalized ordered decision
list per case up to the probe completion marker. `--check FILE` recomputes and compares.
"""
import argparse, hashlib, json
from pathlib import Path


def scene(path):
    s = json.loads(Path(path).read_text())
    return {
        'captures': [{k: c[k] for k in ('path', 'sha256', 'map_sha256', 'records', 'markers', 'public_sha256', 'words_sha256', 'decisions_sha256')}
                     for c in s['captures']],
        'controls': s['controls'],
        'public_identical': s['public_identical'],
        'decisions_identical': s['decisions_identical'],
        'words_identical': s['words_identical'],
        'timelines': s['timelines'],
        'decisions': {k: v for k, v in s.get('decisions', {}).items() if k != 'factory'},
        'order_factories': s.get('decisions', {}).get('factory', []),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--task', required=True)
    ap.add_argument('--scene', action='append', required=True, metavar='NAME=SUMMARY')
    ap.add_argument('--output', type=Path)
    ap.add_argument('--check', type=Path)
    args = ap.parse_args()
    out = {'task': args.task, 'binary_sha256': 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236',
           'evidence': 'L (owned live retail, repeats + observer-free control where marked) plus A (assembly)',
           'scenes': {}}
    for item in args.scene:
        name, path = item.split('=', 1)
        out['scenes'][name] = scene(path)
    text = json.dumps(out, indent=1, sort_keys=True) + '\n'
    if args.check:
        same = args.check.read_text() == text
        print('check', 'equal' if same else 'DIFFERENT')
        raise SystemExit(0 if same else 1)
    args.output.write_text(text)
    print(args.output, hashlib.sha256(text.encode()).hexdigest())


if __name__ == '__main__':
    main()
