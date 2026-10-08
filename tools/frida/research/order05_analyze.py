#!/usr/bin/env python3
"""ORDER-05 capture analysis (order05_observer.js JSONL / JASS Preload output).

  order05_analyze.py markers  <capture.jsonl|rs-*.txt>       RSO5 marker strings
  order05_analyze.py timeline <capture.jsonl> [--all]        compact request timeline
  order05_analyze.py summary  <capture.jsonl>                normalized rows (JSON)
  order05_analyze.py repeat   <a.jsonl> <b.jsonl>            summary equality
  order05_analyze.py clocks   <capture.jsonl>                clock state per marker / load / rebase rows

Normalization: request blocks, receivers and wrappers are heap pointers; they are renamed by first
appearance (blocks B<n> by pointer for the whole capture, so block reuse stays visible; receivers by
first appearance: range listeners L<n>, other wrappers W<n>). Deadline/period words and serials are kept.
"""
import json
import re
import sys


def markers(path):
    if path.endswith('.jsonl'):
        return [json.loads(l)['value'] for l in open(path) if '"event": "marker"' in l]
    return re.findall(r'call Preload\( "(RSO5 [^"]*)" \)', open(path, encoding='latin1').read())


def load(path):
    return [json.loads(l) for l in open(path)]


class Names:
    def __init__(self):
        self.blocks, self.recv = {}, {}
        self.n = {'L': 0, 'W': 0}

    def block(self, p):
        if p not in self.blocks:
            self.blocks[p] = 'B%d' % len(self.blocks)
        return self.blocks[p]

    def receiver(self, p, vt, label):
        if p not in self.recv:
            k = 'L' if (label or '').startswith('range') or vt == 'a91220' else 'W'
            self.n[k] += 1
            self.recv[p] = '%s%d:%s' % (k, self.n[k], vt)
        return self.recv[p]


def req_row(names, q):
    return [names.block(q['r']), names.receiver(q['recv'], q['recvVt'], q['recvLabel']), q['deadlineW'], q['delayW'], q['flags'],
            q['serial'], q['clock']]


def summary(path, keep=None):
    names = Names()
    out = []
    tick = 0
    for r in load(path):
        e = r.get('event')
        if e == 'marker':
            tick = int(re.search(r'tick=(\d+)', r['value']).group(1))
            out.append(['M', re.sub(r' el=[0-9.]+', '', r['value'][5:])])
            if r['value'].endswith(' complete'):
                break                      # the run continues for a variable time after the probe ends
        elif e == 'queue':
            out.append(['Q', tick, r['by']] + req_row(names, r['req']) + [r['clock']['timeW']])
        elif e == 'execute':
            out.append(['X', tick] + req_row(names, r['req']))
        elif e == 'rearm':
            out.append(['R', tick] + req_row(names, r['req']))
        elif e == 'start-timer':
            out.append(['ST', tick, names.receiver(r['before']['p'], r['before']['vt'], r['before']['label']), r['by'],
                        'old' if r['before']['timer'] != '00000000' else 'none'])
        elif e == 'stop-timer':
            out.append(['SP', tick, names.receiver(r['wrapper']['p'], r['wrapper']['vt'], r['wrapper']['label']), r['by'],
                        names.block(r['req']['r']) if r['req'] else None])
        elif e == 'release':
            out.append(['REL', tick, names.receiver(r['before']['p'], r['before']['vt'], r['before']['label']), r['by'],
                        'new' if r['before']['release'] == '00000000' else 'existing'])
        elif e == 'range-emit-enter':
            out.append(['ENTER', tick, names.receiver(r['listener']['p'], r['listener']['vt'], r['listener']['label'])])
        elif e in ('rebase-enter', 'rebase-leave'):
            out.append([e, tick, r['clock']['id'], r['clock']['timeW'], r['clock']['epoch'], r['heap']['count'],
                        [req_row(names, q) for q in r['heap']['items']]])
        elif e in ('game-load-enter', 'game-load-leave', 'settle-enter', 'settle-leave'):
            out.append([e, tick])
        elif e == 'wrapper-load':
            out.append(['WLOAD', tick, r['wrapper']['vt'], r['timer'] and req_row(names, r['timer']), r['release'] and req_row(names, r['release'])])
        elif e == 'request-save':
            out.append(['SAVE', tick] + req_row(names, r['req']))
        elif e == 'clock-flags':
            out.append(['FLAGS', tick, r['clock']['id'], r['before'], r['clock']['flags'], r['clock']['timeW']])
    if keep:
        out = [o for o in out if o[0] in keep]
    return out


def timeline(path, show_all=False):
    for row in summary(path):
        if not show_all and row[0] == 'X' and row[4].startswith('W') and False:
            continue
        print(json.dumps(row))


def clocks(path):
    for r in load(path):
        e = r.get('event')
        if e == 'marker' and ('beat' in r['value'] or 'init' in r['value'] or 'win=' in r['value'] or 'enter trig' in r['value']):
            c = r['clocks']
            p = c.get('primary') or {}
            q = c.get('presentation') or {}
            print(r['seq'], r['value'][5:80], c.get('owner'), 'P t=%s ep=%s fl=%s ser=%s n=%s' % (p.get('time'), p.get('epoch'), p.get('flags'), p.get('serial'), p.get('count')),
                  'V t=%s ep=%s fl=%s' % (q.get('time'), q.get('epoch'), q.get('flags')))
        elif e in ('game-load-enter', 'game-load-leave', 'settle-enter', 'settle-leave', 'clock-flags', 'rebase-enter', 'rebase-leave',
                   'advance-near-span', 'ui-action', 'wrapper-load', 'request-save'):
            print(r.get('seq'), e, json.dumps({k: v for k, v in r.items() if k not in ('event', 'seq', 'heap')})[:400])


def ordercheck(path):
    """Every logged pop must be the minimum (float deadline, unsigned serial) among logged pending requests of
    its clock, and the clock time seen by the request must equal its deadline word. Requests of unwatched
    receivers are only logged inside marker windows, so they are dropped from the pending set at win=0."""
    import struct
    f = lambda w: struct.unpack('<f', struct.pack('<I', int(w, 16)))[0]
    pending, pops, bad, epoch = {}, 0, [], None
    for r in load(path):
        e = r.get('event')
        if e == 'game-load-enter':
            pending = {}
            continue
        if e == 'game-load-leave':         # loaded requests are logged, their later pops only inside windows
            pending = {k: v for k, v in pending.items() if v[3]}
            continue
        if e == 'rebase-leave':
            for q in r['heap']['items']:
                if q['r'] in pending:
                    pending[q['r']] = (f(q['deadlineW']), q['serial'], q['clock'], pending[q['r']][3])
            continue
        if e == 'marker' and ' win=0' in r['value']:
            pending = {k: v for k, v in pending.items() if v[3]}
            continue
        if e in ('queue', 'rearm'):
            q = r['req']
            pending[q['r']] = (f(q['deadlineW']), q['serial'], q['clock'], q['recvLabel'] is not None)
        elif e == 'execute':
            q = r['req']
            key = (f(q['deadlineW']), q['serial'], q['clock'])
            pops += 1
            if r['clockTime'] is not None and abs(r['clockTime'] - key[0]) > 0:
                bad.append(('clock-time', r['seq'], r['clockTime'], key))
            for k, v in pending.items():
                if k != q['r'] and v[2] == key[2] and (v[0], v[1]) < (key[0], key[1]):
                    bad.append(('not-min', r['seq'], q['r'], key, k, v))
                    break
            pending.pop(q['r'], None)
    return {'pops': pops, 'violations': len(bad), 'first': bad[:5]}


def main():
    cmd = sys.argv[1]
    if cmd == 'markers':
        for m in markers(sys.argv[2]):
            print(m)
    elif cmd == 'timeline':
        timeline(sys.argv[2], '--all' in sys.argv)
    elif cmd == 'summary':
        print(json.dumps(summary(sys.argv[2]), indent=0))
    elif cmd == 'repeat':
        a, b = summary(sys.argv[2]), summary(sys.argv[3])
        print('equal' if a == b else 'DIFFER')
        if a != b:
            for i, (x, y) in enumerate(zip(a, b)):
                if x != y:
                    print(i, x, '\n ', y)
                    break
            print(len(a), len(b))
    elif cmd == 'ordercheck':
        print(json.dumps(ordercheck(sys.argv[2])))
    elif cmd == 'clocks':
        clocks(sys.argv[2])


if __name__ == '__main__':
    main()
