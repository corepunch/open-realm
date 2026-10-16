#!/usr/bin/env python3
"""Verify bounded Captain enrollment identity, reachability and public controls."""
import argparse
import hashlib
import json
import re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
EVENTS = {'temporary', 'attach', 'detach', 'policy', 'range', 'reissue', 'reachability', 'idle-member'}
COUNTS = {'temporary': 9, 'attach': 4, 'detach': 3, 'policy': 5, 'range': 1,
          'reissue': 40, 'reachability': 79, 'idle-member': 40}


def normalize(rows):
    units = [r['unit'] for r in rows if r.get('event') == 'append-ai-order' and r['tick'] == 0]
    if len(units) != 4 or len(set(units)) != 4:
        raise ValueError('missing startup units')
    out = []
    captains = set()
    towns = set()
    def owner(value):
        if value in units:
            return units.index(value)
        if value == '0x0':
            return 'null'
        captains.add(value)
        return 'captain'
    def state(value):
        return {k: v for k, v in value.items() if k not in ('unit', 'userHead', 'userTail', 'taskHead')}
    for row in rows:
        event = row.get('event')
        if event == 'marker' and 'label=complete' in row['value']:
            break
        if event not in EVENTS:
            continue
        item = dict(row)
        for key in ('captain', 'unit', 'source', 'target'):
            if key in item:
                item[key] = owner(item[key])
        if 'town' in item:
            towns.add(item.pop('town'))
        if 'before' in item:
            before, after = item['before'], item['after']
            item['unit'] = units.index(before['unit'])
            item['head_changed'] = before['userHead'] != after['userHead']
            item['task_changed'] = before['taskHead'] != after['taskHead']
            item['head_present'] = after['userHead'] != [0xffffffff, 0xffffffff]
            item['task_present'] = after['taskHead'] != [0xffffffff, 0xffffffff]
            item['before'], item['after'] = state(before), state(after)
        for key in ('userHead', 'userTail', 'taskHead'):
            item.pop(key, None)
        out.append(item)
    if len(captains) != 1 or len(towns) != 1:
        raise ValueError('different Captain/Town owners')
    if Counter(x['event'] for x in out) != Counter(COUNTS):
        raise ValueError('missing bounded observer events')
    return out


def read_capture(path, mode, frozen):
    rows = [json.loads(line) for line in path.read_bytes().splitlines()]
    if not rows or rows[0].get('event') != 'metadata' or sum(r.get('event') == 'metadata' for r in rows) != 1:
        raise ValueError('invalid metadata')
    meta = rows[0]
    if (meta.get('task') != 'payoff158' or meta.get('mode') != mode or
        meta.get('owned') is not True or meta.get('sha256') != HASH or
        meta.get('source_sha256') != frozen['capture_sources']):
        raise ValueError('capture provenance differs')
    if any(r.get('type') == 'error' or r.get('event') in ('error', 'trace-failed') for r in rows):
        raise ValueError('capture failed')
    files = [r for r in rows if r.get('event') == 'preload-file']
    if len(files) != 1 or files[0].get('complete') is not True:
        raise ValueError('incomplete public producer')
    raw = path.with_name(path.stem + '-preload.txt').read_bytes()
    if hashlib.sha256(raw).hexdigest() != files[0]['sha256']:
        raise ValueError('public file pin differs')
    markers = re.findall(r'call Preload\( "(RSG [^"\r\n]*)" \)', raw.decode())
    if markers != frozen['public_markers'] or len(markers) != 182 or markers[-1] != 'RSG tick=40 label=complete':
        raise ValueError('public timeline differs')
    ends = [r for r in rows if r.get('event') == 'trace-end']
    if mode == 'observe':
        if len(ends) != 1 or ends[0].get('installed') is not True:
            raise ValueError('observer incomplete')
        if [r['value'] for r in rows if r.get('event') == 'marker'] != markers:
            raise ValueError('observer/public mismatch')
        if normalize(rows) != frozen['normalized']:
            raise ValueError('enrollment/reissue semantics differ')
    elif ends or any(r.get('event') in EVENTS | {'marker', 'append-user', 'append-ai-order', 'snapshot'} for r in rows):
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
    return dict(passed=True, repeats=2, public_markers=182, enrollments=9,
                attachments=4, detachments=3, reissues=40, reachability_queries=79,
                exclusions=['word-exact movement trajectory', 'default Town homes',
                            'combat/retained-target fallback', 'rosters beyond existing verified boundary'])


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
