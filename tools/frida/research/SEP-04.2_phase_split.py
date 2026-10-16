#!/usr/bin/env python3
"""SEP-04.2: split each crowd unit's displacement into the idle phase (before the tick-122 Move, separation only) and the order
phase (path movement + post-stop separation); compare idle JASS displacement with the sum of accepted separation applications."""
import argparse, json, math, sys
from collections import Counter, defaultdict
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from sep_research_analyze import Capture, fw  # noqa: E402
ORDER_TICK, GOAL = 122, (1008.0, 1040.0)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture', type=Path, required=True); ap.add_argument('--map-json', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    cap = Capture(a.capture, a.map_json)
    unit_of_mover = {m: i for i, m in cap.mover_of.items()}
    retry = defaultdict(Counter)
    for r in cap.rows:
        if r['event'] == 'retry-result':
            retry[unit_of_mover.get(r.get('current'))][f"{r['before']}->{r['after']}:{r['result']}"] += 1
    res = {}
    for i, ro in enumerate(cap.roster):
        s = cap.samples[i]
        s0 = s[0]; s_idle = [q for q in s if q[0] <= ORDER_TICK - 2][-1]; s_end = s[-1]
        apps = [v for v in cap.visits(i) if v['apply'] and v['apply']['pos'] != v['apply']['posAfter']]
        def dsum(sel):
            return [sum((fw(v['apply']['posAfter'][c]) - fw(v['apply']['pos'][c])) * 32 for v in sel) for c in (0, 1)]
        idle_sep = dsum([v for v in apps if v['tick'] < ORDER_TICK]); late_sep = dsum([v for v in apps if v['tick'] >= ORDER_TICK])
        idle_jass = [s_idle[1] - s0[1], s_idle[2] - s0[2]]
        stop = next((q for q in s if q[0] >= ORDER_TICK and q[3] == 0), None)
        res[i] = dict(code=ro['code'], owner=ro['owner'], idleJassWorld=[round(x, 4) for x in idle_jass], idleSeparationWorld=[round(x, 4) for x in idle_sep],
                      idleResidualWorld=round(math.hypot(idle_jass[0] - idle_sep[0], idle_jass[1] - idle_sep[1]), 4),
                      stopTick=stop[0] if stop else None, stopGoalDistance=round(math.hypot(stop[1] - GOAL[0], stop[2] - GOAL[1]), 3) if stop else None,
                      postOrderSeparationWorld=[round(x, 4) for x in late_sep], endGoalDistance=round(math.hypot(s_end[1] - GOAL[0], s_end[2] - GOAL[1]), 3),
                      retryResults=dict(retry.get(i, {})))
    a.out.write_text(json.dumps(dict(units=res, unattributedRetry=dict(retry.get(None, {}))), indent=1) + '\n')
    for i, u in res.items():
        print(i, u['code'], 'p%d' % u['owner'], 'idle jass', u['idleJassWorld'], 'sep', u['idleSeparationWorld'], 'resid', u['idleResidualWorld'], 'stop', u['stopTick'], u['stopGoalDistance'], 'post-sep', u['postOrderSeparationWorld'], 'end', u['endGoalDistance'], u['retryResults'])


if __name__ == '__main__':
    main()
