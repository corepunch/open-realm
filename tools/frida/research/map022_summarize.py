#!/usr/bin/env python3
"""MAP-02.2: per-case summary of support refreshes between probe markers."""
import json, sys, re, collections
def r3(v): return round(v, 3) if isinstance(v, float) else v
def summarize(path):
    rows = [json.loads(l) for l in open(path)]
    cases = collections.OrderedDict(); cur = None; key = None
    for r in rows:
        e = r.get('event')
        if e == 'marker' and ' label=cross ' in r['value']:
            key = ('cross',); cases.setdefault(key, dict(type='foot', point='crossing', markers={}, windows={}, steps=[]))
            cases[key]['steps'].append(r['value'].split(' label=cross ')[1]); cur = 'moving'
            continue
        if e == 'marker':
            m = re.search(r'case=(\d+) label=(\w+) type=(\w+) point=(\w+) x=(\S+) y=(\S+) locz=(\S+) fly=(\S+)', r['value'])
            if m:
                key = (int(m[1]), m[3], m[4]); c = cases.setdefault(key, dict(type=m[3], point=m[4], markers={}, windows={}))
                c['markers'][m[2]] = dict(x=m[5], y=m[6], locz=m[7], fly=m[8]); cur = m[2]
            continue
        if e == 'support-refresh' and key is not None and r.get('calls'):
            call = r['calls'][-1]
            sig = dict(z=r3(r['out'][2]), zbits='%08x' % r['outBits'][2], xy=[r3(v) for v in r['out'][:2]],
                       move1fc=r['after']['move1fc'], f280_before='%x' % r['before']['flags280'], f280_after='%x' % r['after']['flags280'],
                       f5c_fly=bool(r['after']['flags5c'] & 0x20000000), ground200=r['after']['ground200'],
                       getter_bridge=r.get('getterBridge'), deep_water=(r.get('deepWater') or {}).get('result'),
                       layer=call['layer'],
                       events=[{k: (r3(v) if not isinstance(v, list) else [r3(x) for x in v]) for k, v in ev.items() if k not in ('x', 'y')} for ev in call['events']])
            win = cases[key]['windows'].setdefault(cur, [])
            s = json.dumps(sig, sort_keys=True)
            if not win or json.dumps(win[-1], sort_keys=True) != s: win.append(sig)
    return cases
if __name__ == '__main__':
    cases = summarize(sys.argv[1])
    out = [dict(case=k[0], **v) for k, v in cases.items()]
    if len(sys.argv) > 3 and sys.argv[3] == '--quiet':
        open(sys.argv[2], 'w').write(json.dumps(out, indent=1) + '\n'); sys.exit(0)
    if len(sys.argv) > 2: open(sys.argv[2], 'w').write(json.dumps(out, indent=1) + '\n')
    for c in out:
        print(c['case'], c['type'], c['point'], {k: (m['locz'], m['fly']) for k, m in c['markers'].items()})
        for st in c.get('steps', []): print('   step', st)
        for w in ('moved', 'placed', 'moving'):
            for s in c['windows'].get(w, []):
                ev = ' | '.join(e['at'] + ':' + ','.join(f'{k}={v}' for k, v in e.items() if k not in ('at', 'flag', 'xy', 'deckOrY', 'point')) for e in s['events'])
                print(f"   {w:6s} z={s['z']} 1fc={s['move1fc']:x} 280 {s['f280_before']}->{s['f280_after']} fly5c={int(s['f5c_fly'])} br={s['getter_bridge']} deep={s['deep_water']} :: {ev}")
