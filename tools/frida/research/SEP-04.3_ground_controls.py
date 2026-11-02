#!/usr/bin/env python3
"""SEP-04.3: enabled vs disabled-repulsion ground groups; separate path blocking/retry/Stop from repulsion.

Per unit: JASS order timeline (issue -> order 0), stop distance to each goal, separation visits by kind
(cooldown / moving: c0 != 0 clears and installs cooldown 7 / idle body), accepted separation applications
(tick range, summed displacement), retry-init/result rows attributed through 6fd53a8c (mover being
processed; requires observer >= 7eee4c82), and Mover stop calls (6f171340) by caller.
"""
import argparse, hashlib, json, math, sys
from collections import Counter, defaultdict
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from sep_research_analyze import Capture, fw  # noqa: E402

GOALS = {'hSC1': [(720.0, 1552.0), (1552.0, 1616.0)], 'hSD1': [(720.0, 400.0), (1552.0, 464.0)]}
ORDER_TICKS = (22, 202)  # 20/200 after the creation tick 2


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture', type=Path, required=True)
    ap.add_argument('--map-json', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    cap = Capture(a.capture, a.map_json)
    # mover pointer -> unit (pointers are not reused in this single-phase scene)
    unit_of_mover = {m: i for i, m in cap.mover_of.items()}
    retry = defaultdict(list)
    for r in cap.rows:
        if r['event'] in ('retry-init', 'retry-result'):
            retry[unit_of_mover.get(r.get('current'))].append(r)
    stops = defaultdict(Counter)
    for r in cap.rows:
        if r['event'] == 'mover-stop':
            stops[unit_of_mover.get(r['mover'])][hex(r['caller'])] += 1
    out = {}
    for i, ro in enumerate(cap.roster):
        s = cap.samples[i]
        timeline, prev = [], None
        for t, x, y, o, p in s:
            if o != prev:
                timeline.append([t, round(x, 4), round(y, 4), o]); prev = o
        legs = []
        for k, start in enumerate(ORDER_TICKS):
            end = ORDER_TICKS[k + 1] if k + 1 < len(ORDER_TICKS) else 10 ** 9
            seg = [q for q in s if start <= q[0] < end]
            stopped = next((q for q in seg if q[3] == 0), None)
            last = seg[-1] if seg else None
            g = GOALS[ro['code']][k]
            legs.append(dict(goal=g, stopTick=stopped[0] if stopped else None,
                             stopDistance=round(math.hypot(stopped[1] - g[0], stopped[2] - g[1]), 4) if stopped else None,
                             lastTick=last[0] if last else None, lastDistance=round(math.hypot(last[1] - g[0], last[2] - g[1]), 4) if last else None,
                             driftAfterStopWorld=round(math.hypot(last[1] - stopped[1], last[2] - stopped[2]), 4) if stopped and last else None))
        vs = cap.visits(i)
        kinds = Counter('cooldown' if v['before']['word'] & 0xffff else 'moving' if v['moverBefore']['speed'] not in (0, 0x80000000) else 'body' for v in vs)
        apps = [v for v in vs if v['apply'] and v['apply']['pos'] != v['apply']['posAfter']]
        dsep = [sum(fw(v['apply']['posAfter'][c]) - fw(v['apply']['pos'][c]) for v in apps) * 32 for c in (0, 1)]
        bodies_moving = sum(1 for v in vs if not v['before']['word'] & 0xffff and v['pairs'] and v['moverBefore']['speed'] not in (0, 0x80000000))
        rr = retry.get(i, [])
        out[i] = dict(code=ro['code'], repulse=bool(cap.meta['units'][ro['code']].get('urpo')), timeline=timeline, legs=legs,
                      separation=dict(visits=len(vs), kinds=dict(kinds), applications=len(apps), bodiesWhileMoving=bodies_moving,
                                      applicationTicks=sorted({v['tick'] for v in apps}), displacementWorld=[round(x, 4) for x in dsep],
                                      rejected=sum(1 for v in vs if v['apply'] and v['apply'].get('valid') == 0)),
                      retry=dict(init=[(r['tick'], r['count']) for r in rr if r['event'] == 'retry-init'],
                                 results=Counter(f"{r['before']}->{r['after']}:{r['result']}" for r in rr if r['event'] == 'retry-result'),
                                 draws=sum(1 for r in rr if r['ownerBefore'] != r['ownerAfter'])),
                      stops=dict(stops.get(i, {})))
    unattributed = len(retry.get(None, []))
    res = dict(capture_sha256=hashlib.sha256((a.capture / 'capture.jsonl').read_bytes()).hexdigest(), complete=cap.complete,
               observer_sha256=cap.prov.get('observer_sha256'), retryUnattributed=unattributed, units=out)
    a.out.write_text(json.dumps(res, indent=1, default=dict) + '\n')
    for i, u in out.items():
        print(i, u['code'], 'legs', [(l['stopTick'], l['stopDistance'], l['driftAfterStopWorld']) for l in u['legs']], 'sep', u['separation']['kinds'],
              'apps', u['separation']['applications'], 'movingBodies', u['separation']['bodiesWhileMoving'], 'retry', len(u['retry']['init']), dict(u['retry']['results']), 'draws', u['retry']['draws'], 'stops', u['stops'])
    print('unattributed retry rows', unattributed)


if __name__ == '__main__':
    main()
