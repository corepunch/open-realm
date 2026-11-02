#!/usr/bin/env python3
"""ORDER-03.1/03.2 capture analyzer (read-only).

  order03_analyze.py markers <observe.jsonl|preload.txt> <control preload.txt>
      exact RSO3 marker equality (observer-free control)
  order03_analyze.py timeline <observe.jsonl> [--ticks 15,25]
      ordered subscriber-table timeline for the probe's units (names U,N,R,K,L,G)
  order03_analyze.py summary <observe.jsonl>
      normalized JSON (identities by encounter order) used for expected-ORDER-03.x.json
"""
import json
import re
import sys

UNIT_BY_TICK = {'5': 'U', '25': 'N', '32': 'R', '36': 'K', '40': 'L', '50': 'G'}


def markers(path):
    if path.endswith('.jsonl'):
        return [json.loads(l)['value'] for l in open(path) if '"event": "marker"' in l]
    return re.findall(r'call Preload\( "(RSO3 [^"]*)" \)', open(path, encoding='latin1').read())


def load(path):
    return [json.loads(l) for l in open(path)]


class Names:
    """Time-aware names: heap addresses are reused after destruction, so every name has a seq interval."""
    def __init__(self, rows):
        self.iv = {}          # pointer -> [(from_seq, to_seq, name)]
        self.sentinels = set(r['sentinel'] for r in rows if r.get('event') == 'core-enter')
        pending_reg = pending_table = pending_trig = None
        unit_name = {}
        for r in rows:
            e, s = r.get('event'), r.get('seq', -1)
            if e == 'register' and r.get('caller') == '27ab50':
                pending_reg, pending_table, pending_from = r['cb']['p'], r['before']['header']['t'], s
            elif e == 'register' and r.get('caller') in ('27ab5c', '27a5e7', '27a5f3'):
                pending_trig = r['cb']['p'], s
            elif e == 'unit-reg':
                unit = r['unit']
                if unit not in unit_name:
                    unit_name[unit] = UNIT_BY_TICK.get(str(r['tick']), 'unit@%s' % r['tick'])
                    self.add(unit, pending_from, unit_name[unit])
                    self.add(pending_table, pending_from, unit_name[unit])
                self.add(r['reg']['p'], pending_from, r['name'] + '.reg')
                if pending_trig:
                    self.add(pending_trig[0], pending_trig[1], r['name'])
                pending_trig = None
            elif e == 'player-reg':
                self.add(r['player'], s, 'player0')
                self.add(r['after']['table']['t'], s, 'player0')
                self.add(r['reg']['p'], s, r['name'] + '.reg')
                if pending_trig:
                    self.add(pending_trig[0], pending_trig[1], r['name'])
                pending_trig = None
            elif e == 'destructor':
                self.end(r['obj'], s)
                if r.get('table') and r['table'] != '0x0':
                    self.end(r['table'], s)
            elif e == 'clear-subscriptions' and r.get('tableAfter') == '0x0':
                self.end(r['table'], s)

    def add(self, p, s, name):
        lst = self.iv.setdefault(p, [])
        if lst and lst[-1][1] is None:
            lst[-1] = (lst[-1][0], s, lst[-1][2])
        lst.append((s, None, name))

    def end(self, p, s):
        lst = self.iv.get(p)
        if lst and lst[-1][1] is None:
            lst[-1] = (lst[-1][0], s, lst[-1][2])

    def name(self, p, s):
        for a, b, n in reversed(self.iv.get(p, [])):
            if a <= s and (b is None or s <= b):
                return n
        return None

    def obj(self, o, s):
        if o is None:
            return None
        return self.name(o['p'], s) or 'vt:' + str(o.get('vt'))

    def chain(self, c, s):
        if c is None:
            return None
        out = []
        for n in c:
            if 'n' not in n:
                out.append('...')
            elif n.get('sentinel') or n['n'] in self.sentinels:
                out.append('<sentinel>')
            elif n['cb'] is None:
                out.append('<tombstone ev=%s>' % n['ev'])
            else:
                out.append(self.obj(n['cb'], s))
        return out


def jass_chain(names, c, s):
    """Only the JASS-relevant members of a bucket chain (regs, sentinel, tombstones of JASS events)."""
    if c is None:
        return None
    full = names.chain(c, s)
    return [x for x, n in zip(full, c) if 'n' in n and (x.startswith('<') or x.endswith('.reg') or 0x80200 <= int(n['ev'], 16) < 0x80400)]


def timeline(path, ticks=None):
    rows = load(path)
    names = Names(rows)
    out = []
    for r in rows:
        e = r.get('event')
        t = str(r.get('tick')) if 'tick' in r else (re.search(r'tick=(\d+)', r.get('value', '')) or [None, None])[1]
        q = r.get('seq', -1)
        nm = lambda o: names.obj(o, q)
        if ticks and t not in ticks:
            continue
        if e == 'marker':
            w = {names.name(x['t'], q) or x['t']: [x.get('depth'), x.get('count')] for x in r['watched'] if 't' in x}
            out.append('M %s | depth/count %s' % (r['value'][5:], {k: v for k, v in w.items() if k in UNIT_BY_TICK.values()}))
        elif e in ('dispatch-enter', 'dispatch-leave'):
            who = nm(r['agent'])
            if who in tuple(UNIT_BY_TICK.values()) + ('player0',) or who.endswith('.reg'):
                tb = r.get('table') or {}
                out.append('%s %s ev=%s agentRefs=%s depth=%s count=%s buckets=%s chain=%s%s' % (
                    e, who, r['ev'], r['agent']['refs'], tb.get('depth'), tb.get('count'), tb.get('buckets'),
                    jass_chain(names, r.get('chain'), q), ' ret=%s' % r['ret'] if 'ret' in r else ''))
        elif e in ('register', 'unregister'):
            tname = names.name(r['before']['header']['t'], q) or r['before']['header']['t']
            if tname not in tuple(UNIT_BY_TICK.values()) + ('player0',):
                continue
            out.append('%s on %s ev=%s cb=%s cbRefs=%s->%s depth=%s count %s->%s chain %s -> %s caller=%s' % (
                e, tname, r['ev'], nm(r['cb']), (r['cb'] or {}).get('refs'), r.get('cbRefsAfter'),
                r['before']['header']['depth'], r['before']['header']['count'], r['after']['header']['count'],
                names.chain(r['before']['chain'], q), names.chain(r['after']['chain'], q), r.get('caller')))
        elif e in ('reg-handler', 'trigger-handler'):
            who = nm(r.get('reg') or r.get('trigger'))
            out.append('%s %s packetEv=%s%s' % (e, who, r['packetEv'], ' eval=%s exec=%s' % (r['eval'], r['exec']) if 'eval' in r else ''))
        elif e == 'trigger-handler-leave':
            out.append('  trigger-handler-leave %s ret=%s eval=%s exec=%s' % (names.name(r['trigger'], q) or r['trigger'], r['ret'], r['eval'], r['exec']))
        elif e == 'release-request':
            n = nm(r['obj'])
            if not n.startswith('vt:'):
                out.append('release-request %s refs=%s caller=%s' % (n, r['obj']['refs'], r['caller']))
        elif e == 'wrapper-destroy':
            n = nm(r['payload']) if r['payload'] else None
            if n and not n.startswith('vt:'):
                out.append('wrapper-destroy payload=%s refs=%s' % (n, r['payload']['refs']))
        elif e == 'destructor':
            n = names.name(r['obj'], q)
            if n:
                out.append('destructor %s %s refs=%s caller=%s' % (r['cls'], n, r['refs'], r['caller']))
        elif e == 'clear-subscriptions':
            out.append('clear-subscriptions %s depth=%s count=%s tableAfter=%s bt=%s' % (
                nm(r['agent']), r['before']['header']['depth'], r['before']['header']['count'], r['tableAfter'], r['backtrace']))
        elif e == 'grow':
            out.append('grow on %s buckets %s->%s count %s->%s depth=%s caller=%s' % (
                names.name(r['before']['header']['t'], q), r['before']['header']['buckets'], r['after']['header']['buckets'],
                r['before']['header']['count'], r['after']['header']['count'], r['before']['header']['depth'], r['caller']))
    return out


def summary(path):
    """Normalized, address-free event list: names by role, vtables by RVA, internal callbacks by vtable."""
    rows = load(path)
    names = Names(rows)
    out = []
    units = tuple(UNIT_BY_TICK.values())
    for r in rows:
        e = r.get('event')
        q = r.get('seq', -1)
        nm = lambda o: names.obj(o, q)
        if e == 'marker':
            w = {names.name(x['t'], q): [x.get('depth'), x.get('count')] for x in r['watched'] if 't' in x}
            out.append(['marker', r['value'], {k: w[k] for k in units if k in w}])
        elif e in ('dispatch-enter', 'dispatch-leave'):
            who = nm(r['agent'])
            if who in units or who == 'player0' or who.endswith('.reg'):
                tb = r.get('table') or {}
                row = [e, who, r['ev'], r['agent']['refs'], tb.get('depth'), tb.get('count'), tb.get('buckets'),
                       names.chain(r.get('chain'), q)]
                if 'ret' in r:
                    row.append(r['ret'])
                out.append(row)
        elif e in ('register', 'unregister'):
            tname = names.name(r['before']['header']['t'], q)
            if tname in units or tname == 'player0' or (tname or '').endswith('.reg'):
                out.append([e, tname, r['ev'], nm(r['cb']), (r['cb'] or {}).get('refs'), r.get('cbRefsAfter'),
                            r['before']['header']['depth'], r['before']['header']['count'], r['after']['header']['count'],
                            names.chain(r['before']['chain'], q), names.chain(r['after']['chain'], q), r.get('caller')])
        elif e in ('reg-handler', 'trigger-handler'):
            o = r.get('reg') or r.get('trigger')
            out.append([e, nm(o), r['packetEv'], r.get('eval'), r.get('exec')])
        elif e == 'trigger-handler-leave':
            out.append([e, names.name(r['trigger'], q), r['ret'], r['eval'], r['exec']])
        elif e == 'release-request':
            n = nm(r['obj'])
            if not n.startswith('vt:'):
                out.append([e, n, r['obj']['refs'], r['caller']])
        elif e == 'wrapper-destroy' and r['payload']:
            n = nm(r['payload'])
            if not n.startswith('vt:'):
                out.append([e, n, r['payload']['refs']])
        elif e == 'destructor':
            n = names.name(r['obj'], q)
            if n:
                out.append([e, r['cls'], n, r['refs'], r['caller']])
        elif e == 'clear-subscriptions':
            out.append([e, nm(r['agent']), r['before']['header']['depth'], r['before']['header']['count'], r['tableAfter'] == '0x0', r['backtrace'][:4]])
        elif e == 'grow':
            t = names.name(r['before']['header']['t'], q)
            if t and r['before']['header']['buckets'] != r['after']['header']['buckets']:
                out.append([e, t, r['before']['header']['buckets'], r['after']['header']['buckets'], r['before']['header']['count'],
                            r['before']['header']['depth'], r['caller']])
    return out


UNIT_SETS = {'first': {'5': 'U', '25': 'N', '32': 'R', '36': 'K', '40': 'L', '50': 'G'},
             'nested': {'5': 'V', '10': 'W', '15': 'M', '20': 'X', '25': 'Y'},
             'payload': {'5': 'Z', '10': 'Q'}}


def main():
    if '--units' in sys.argv:
        i = sys.argv.index('--units')
        UNIT_BY_TICK.clear()
        UNIT_BY_TICK.update(UNIT_SETS[sys.argv[i + 1]])
        del sys.argv[i:i + 2]
    cmd = sys.argv[1]
    if cmd == 'markers':
        a, b = markers(sys.argv[2]), markers(sys.argv[3])
        print(json.dumps({'a': len(a), 'b': len(b), 'equal': a == b,
                          'first_difference': next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), None)}))
        sys.exit(0 if a == b else 1)
    if cmd == 'timeline':
        ticks = None
        if '--ticks' in sys.argv:
            ticks = set(sys.argv[sys.argv.index('--ticks') + 1].split(','))
        print('\n'.join(timeline(sys.argv[2], ticks)))
    if cmd == 'summary':
        print(json.dumps(summary(sys.argv[2])))
    if cmd == 'repeat':
        a, b = summary(sys.argv[2]), summary(sys.argv[3])
        first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), None)
        print(json.dumps({'a': len(a), 'b': len(b), 'equal': a == b, 'first_difference': first,
                          'diff': [a[first], b[first]] if first is not None else None}))
        sys.exit(0 if a == b else 1)


if __name__ == '__main__':
    main()
