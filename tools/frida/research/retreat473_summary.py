#!/usr/bin/env python3
"""Human-readable summary of a retreat473 capture (policy calls, actor/member positions)."""
import json, struct, sys


def f(w):
    return struct.unpack('<f', struct.pack('<I', w))[0]


def main():
    rows = [json.loads(line) for line in open(sys.argv[1])]
    every = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    pos = {}
    for r in rows:
        e = r.get('event')
        if e == 'public-position':
            pos[r['u']] = (f(r['xy'][0]), f(r['xy'][1]))
        elif e == 'marker':
            v = r['value']
            if 'label=unit' in v:
                continue
            caps = r.get('captains') or []
            label = v.split('label=')[1]
            if label == 'sample' and r['tick'] % every:
                continue
            for c in caps:
                if c.get('state') is None:
                    continue
                a = c.get('actor') or {}
                ap = [round(f(x), 4) for x in a.get('position', [0, 0])]
                print(f"t{r['tick']} c{r['c']} {label[:20]} st{c['state']} fl{c['flags']:x} n{c['counts']} req({f(c['request'][0]):.0f},{f(c['request'][1]):.0f}) r{f(c['requestRange']):.0f} actor{ap} vel{[round(f(x),3) for x in a.get('velocity',[0,0])]}")
            print('   members', {k: (round(x, 1), round(y, 1)) for k, (x, y) in sorted(pos.items())})
        elif e == 'captain-call':
            if r['name'] in ('periodic-update',):
                continue
            b, a = r['before'], r['after']
            extra = {k: r[k] for k in ('x', 'y', 'range', 'retain') if k in r}
            if 'x' in extra:
                extra['x'] = round(f(extra['x']), 2); extra['y'] = round(f(extra['y']), 2)
            if 'range' in extra:
                extra['range'] = round(f(extra['range']), 2)
            ap = [round(f(x), 4) for x in (r.get('actor') or {}).get('position', [0, 0])]
            print(f"  CALL t{r['tick']} c{r['c']} {r['name']} caller{r['caller']:x} res{r['result']} fl {b['flags']:x}->{a['flags']:x} st {b['state']}->{a['state']} n{a['counts']} {extra} actor{ap}")
        elif e in ('reissue', 'actor-place', 'engagement-test', 'roster', 'retreating-native'):
            d = {k: v for k, v in r.items() if k not in ('seq', 'captain', 'event')}
            if e == 'roster':
                d['captain'] = {'flags': hex(r['captain']['flags']), 'counts': r['captain']['counts']}
            print('  ', e, d)


if __name__ == '__main__':
    main()
