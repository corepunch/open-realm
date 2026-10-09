#!/usr/bin/env python3
"""Admit complete temporary-Captain enrollment repeats and a public control."""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
EVENTS = {'marker', 'append-ai-order', 'attach', 'detach', 'temporary', 'policy'}
COUNTS = {'append-ai-order': 10, 'attach': 3, 'temporary': 7}


def normalize(rows):
    # Creation order identifies the four actors; absolute addresses are process-local.
    units = [r['unit'] for r in rows if r.get('event') == 'append-ai-order' and r['tick'] == 0]
    if len(units) != 4 or len(set(units)) != 4:
        raise ValueError('missing startup actor identities')
    captains = set()
    towns = set()
    result = []
    for row in rows:
        if row.get('event') not in EVENTS:
            continue
        row = dict(row)
        if 'unit' in row:
            row['unit'] = units.index(row['unit'])
        if 'captain' in row:
            captains.add(row.pop('captain'))
        if 'town' in row:
            towns.add(row.pop('town'))
            for stage in ('before', 'after'):
                row[stage] = dict(row[stage])
                row[stage]['unit'] = units.index(row[stage]['unit'])
        result.append(row)
    if len(captains) != 1 or len(towns) != 1:
        raise ValueError('different enrollment owners')
    return result


def read_capture(path, mode, frozen):
    rows = [json.loads(line) for line in path.read_bytes().splitlines()]
    if not rows or rows[0].get('event') != 'metadata' or sum(r.get('event') == 'metadata' for r in rows) != 1:
        raise ValueError('missing/duplicated metadata')
    meta = rows[0]
    if meta.get('mode') != mode or meta.get('task') != 'payoff157' or meta.get('sha256') != HASH or meta.get('owned') is not True:
        raise ValueError('wrong capture identity')
    if meta.get('source_sha256') != frozen['capture_sources']:
        raise ValueError('changed capture source/map')
    if any(r.get('type') == 'error' or r.get('event') in ('trace-failed', 'error') for r in rows):
        raise ValueError('capture error')
    preloads = [r for r in rows if r.get('event') == 'preload-file']
    if len(preloads) != 1 or preloads[0].get('complete') is not True:
        raise ValueError('incomplete public producer')
    data = path.with_name(path.stem + '-preload.txt').read_bytes()
    if hashlib.sha256(data).hexdigest() != preloads[0]['sha256']:
        raise ValueError('changed public file')
    markers = re.findall(r'call Preload\( "(RSG [^"\r\n]*)" \)', data.decode())
    if markers != frozen['public_markers'] or len(markers) != 90 or markers[-1] != 'RSG tick=18 label=complete':
        raise ValueError('public timeline differs')
    ends = [r for r in rows if r.get('event') == 'trace-end']
    if mode == 'observe':
        if len(ends) != 1 or ends[0].get('installed') is not True or ends[0].get('counts') != COUNTS:
            raise ValueError('incomplete observer')
        if {e: sum(r.get('event') == e for r in rows) for e in COUNTS} != COUNTS:
            raise ValueError('incomplete observer events')
        if [r['value'] for r in rows if r.get('event') == 'marker'] != markers:
            raise ValueError('observer/public mismatch')
        if normalize(rows) != frozen['normalized']:
            raise ValueError('enrollment state/order differs')
    elif ends or any(r.get('event') in EVENTS for r in rows):
        raise ValueError('control contains observer')
    if hashlib.sha256(path.read_bytes()).hexdigest() != frozen['captures'][path.name]:
        raise ValueError('capture pin differs')


def verify(captures, expected):
    frozen = json.loads(expected.read_text())
    if frozen['binary_sha256'] != HASH:
        raise ValueError('wrong binary')
    for name, digest in frozen['sources'].items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest:
            raise ValueError('changed source: ' + name)
    for name in frozen['captures']:
        read_capture(captures / name, 'control' if name.startswith('control') else 'observe', frozen)
    return dict(passed=True, repeats=2, public_markers=90, enrollments=7, attachments=3,
                exclusions=['physical trajectory', 'disabled policy', 'automatic enrollment engine integration'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--captures', type=Path, required=True)
    p.add_argument('--expected', type=Path, required=True)
    p.add_argument('--report', type=Path, required=True)
    a = p.parse_args()
    if a.report.exists():
        p.error('report must be fresh')
    try:
        result = verify(a.captures, a.expected)
    except (ValueError, KeyError, OSError) as error:
        result = dict(passed=False, error=str(error))
    a.report.parent.mkdir(parents=True, exist_ok=True)
    a.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
