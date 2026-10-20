#!/usr/bin/env python3
"""ROUTE-03/05: normalize route03_observer.js captures, compare repeats/controls and freeze expected owner-tick tables.

Normalization replaces process pointers with first-seen role labels (P0, P1, ... for paths, M0.. for movers,
G0.. for groups, X0.. for other objects) and drops the per-message serial.  Every raw numerical word
(float bit patterns, counters, indices, identities, bucket work) is kept unchanged.

  route03_expected.py digest CAPTURE...                  normalized stream digest per capture
  route03_expected.py compare A B                         first difference between two normalized streams
  route03_expected.py control OBSERVE CONTROL-PRELOAD     JASS marker equality (observer-free control)
  route03_expected.py freeze --case NAME=CAPTURE[:LO:HI] ... [--attach NAME=JSON] --output expected.json
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

PTR = re.compile(r'^0x[0-9a-f]+$')
SKIP_EVENTS = {'metadata', 'module', 'loading-key', 'trace-end', 'preload-file', 'owned-process-already-exited'}


class Roles:
    def __init__(self):
        self.map = {'0x0': 'null'}
        self.count = {}

    def label(self, value, kind):
        if value not in self.map:
            n = self.count.get(kind, 0)
            self.count[kind] = n + 1
            self.map[value] = '%s%d' % (kind, n)
        return self.map[value]


def kind_of(key):
    if key in ('self', 'target'):
        return 'S'
    if key == 'path' or key.endswith('path'):
        return 'P'
    if key in ('mover', 'blocker'):
        return 'M'
    if key in ('group',):
        return 'G'
    if key in ('head', 'tail'):
        return 'P'
    return 'X'


def normalize(obj, roles, key=''):
    if isinstance(obj, dict):
        out = {}
        for k, v in obj.items():
            if k in ('n', 'ms'):
                continue
            if k == 'groups' and isinstance(v, dict):
                out[k] = {roles.label(mk, 'M'): normalize(mv, roles, 'groupinfo') for mk, mv in sorted(v.items())}
                continue
            out[k] = normalize(v, roles, k)
        return out
    if isinstance(obj, list):
        return [normalize(v, roles, key) for v in obj]
    if isinstance(obj, str) and PTR.match(obj):
        if obj in ('0xffffffff',):
            return obj
        return roles.label(obj, kind_of(key))
    return obj


def load(path):
    rows = []
    for line in Path(path).open():
        row = json.loads(line)
        if row.get('event') in SKIP_EVENTS or row.get('type') == 'error':
            continue
        rows.append(row)
    return rows


def stream(path):
    roles = Roles()
    return [normalize(r, roles) for r in load(path)], roles


def digest(items):
    h = hashlib.sha256()
    for item in items:
        h.update(json.dumps(item, sort_keys=True, separators=(',', ':')).encode())
        h.update(b'\n')
    return h.hexdigest()


def markers_from_capture(path):
    return [r['value'] for r in load(path) if r.get('event') == 'marker']


def markers_from_preload(path):
    return re.findall(r'call Preload\( "(ROUTE0[35] [^"\r\n]*)" \)', Path(path).read_text(errors='replace'))


def fl(u):
    return struct.unpack('<f', struct.pack('<I', u))[0]


def visits(items, lo, hi):
    """Group normalized events into owner visits of each path (step-enter..step-leave), keeping order."""
    out, cur, counter = [], {}, None
    for r in items:
        e = r.get('event')
        if e == 'owner':
            counter = r['after']
            continue
        if e == 'marker':
            counter = r['counter']
            if lo <= counter <= hi:
                out.append({'marker': r['value'], 'counter': counter})
            continue
        if counter is None or not (lo <= counter <= hi):
            continue
        if e == 'step-enter':
            path = r['path']['path']
            cur[path] = {'counter': r['counter'], 'path': path, 'mover': r['mover'], 'enter': r['path'],
                         'source': r['source'], 'destination': r['destination'], 'query': r['query'],
                         'refresh': r['refresh'], 'decisions': []}
            continue
        if e == 'step-leave':
            path = r['path']['path']
            v = cur.pop(path, None)
            if v is None:
                v = {'counter': r['counter'], 'path': path, 'decisions': [], 'enter': None}
            v.update(leave=r['path'], moverAfter=r['mover'], outputs=r['outputs'],
                     sourceAfter=r['source'], destinationAfter=r['destination'])
            out.append(v)
            continue
        target = r.get('path') if isinstance(r.get('path'), str) else None
        if target in cur:
            cur[target]['decisions'].append(r)
        elif e in ('yield-decision', 'collect', 'yield-set', 'blocker-resolve'):
            # resolver/collector events that belong to the currently open visit (single-threaded owner)
            if cur:
                list(cur.values())[-1]['decisions'].append(r)
            else:
                out.append({'counter': counter, 'outside': r})
        else:
            out.append({'counter': counter, 'outside': r})
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest='cmd', required=True)
    d = sub.add_parser('digest'); d.add_argument('captures', nargs='+')
    c = sub.add_parser('compare'); c.add_argument('a'); c.add_argument('b')
    c.add_argument('--path', help='compare only member-path decision events of this role label (e.g. P0)')
    k = sub.add_parser('control'); k.add_argument('observe'); k.add_argument('preload')
    f = sub.add_parser('freeze'); f.add_argument('--case', action='append', required=True)
    f.add_argument('--output', type=Path, required=True); f.add_argument('--task', required=True)
    f.add_argument('--note', action='append', default=[])
    f.add_argument('--attach', action='append', default=[], metavar='NAME=JSON', help='embed a verifier/control JSON result')
    args = ap.parse_args()
    if args.cmd == 'digest':
        for p in args.captures:
            items, roles = stream(p)
            print(digest(items), len(items), p)
    elif args.cmd == 'compare':
        a, _ = stream(args.a)
        b, _ = stream(args.b)
        if args.path:
            keep = ('step-enter', 'step-leave', 'advance', 'acc-advance', 'acc-check', 'fine-advance', 'fine-get', 'fine-progress',
                    'collect', 'interval', 'fine-request', 'acc-request', 'retry', 'retry-init', 'reset', 'yield-set')
            def own(r):
                p = r.get('path')
                p = p.get('path') if isinstance(p, dict) else p
                return r.get('event') in keep and p == args.path
            a = [r for r in a if own(r)]
            b = [r for r in b if own(r)]
        for i, (x, y) in enumerate(zip(a, b)):
            if x != y:
                print('first difference at normalized event', i)
                print(' A', json.dumps(x)[:1500])
                print(' B', json.dumps(y)[:1500])
                sys.exit(1)
        if len(a) != len(b):
            print('length differs', len(a), len(b)); sys.exit(1)
        print('identical', len(a), digest(a))
    elif args.cmd == 'control':
        a = markers_from_capture(args.observe)
        b = markers_from_preload(args.preload)
        same = a == b
        print(json.dumps(dict(observe=len(a), control=len(b), identical=same,
                              digest_observe=digest(a), digest_control=digest(b))))
        if not same:
            for i, (x, y) in enumerate(zip(a, b)):
                if x != y:
                    print('first marker difference', i, x, '|', y)
                    break
            sys.exit(1)
    else:
        cases = {}
        for spec in args.case:
            name, rest = spec.split('=', 1)
            parts = rest.split(':')
            path = parts[0]
            lo = int(parts[1]) if len(parts) > 1 else 0
            hi = int(parts[2]) if len(parts) > 2 else 1 << 32
            items, roles = stream(path)
            raw = Path(path).read_bytes()
            cases[name] = dict(capture=Path(path).name, capture_sha256=hashlib.sha256(raw).hexdigest(),
                               normalized_digest=digest(items), window=[lo, hi], role_counts=roles.count,
                               visits=visits(items, lo, hi))
        result = dict(task=args.task, binary_sha256='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236',
                      notes=args.note, encoding='raw u32 words; floats are IEEE-754 bit patterns; ffffffff = -1 index/identity',
                      cases=cases, attached={k: json.loads(Path(v).read_text()) for k, v in (a.split('=', 1) for a in args.attach)})
        text = json.dumps(result, indent=1, sort_keys=True) + '\n'
        args.output.write_text(text)
        print(hashlib.sha256(text.encode()).hexdigest(), len(text), args.output)


if __name__ == '__main__':
    main()
