#!/usr/bin/env python3
"""GROUP-03.4.7.3: replay the decoded 9d08e0 / 9d2670 captain home policy against every frozen live call.

Inputs per call come only from frozen words: captain state/flags/counts at entry, authored home, the virtual
actor position from the nearest preceding marker snapshot (actor stationary or slow; distances are far from the
500 boundary except the 495 case), the public SetGroupsFlee tick from the scene schedule (AI phases 2 and 5), and
SetCampaignAI (c0 counts every member).  The model must reproduce the live result (0/1), the retreat bit after a
1 result (ORed in iff members remain; never cleared here), and the presence of a CaptainAI_PlaceActorAtPoint call (empty roster, not near
home).  Every goal-event GoHome near home must clear bit 2 iff c4 >= bc/2.
"""
import argparse
import json
import math
from pathlib import Path

HOME_RANGE = 500.0


def fine_to_world(p):
    return p[0] * 32.0 - 7168.0, p[1] * 32.0 - 3072.0


def actor_at(scene, tick, counter):
    best = None
    for t in scene['timeline']:
        if (t['tick'], t['counter']) <= (tick, counter):
            cap = t['captains'].get('C0')
            if cap and cap.get('actor_fine'):
                best = cap['actor_fine']
    return best


def near_home(scene, row):
    actor = actor_at(scene, row['tick'], row['counter'])
    if actor is None:
        return None, None
    x, y = fine_to_world(actor)
    hx, hy = row['before']['home']
    return math.hypot(x - hx, y - hy) <= HOME_RANGE, math.hypot(x - hx, y - hy)


def model(state, flags, counts, flee, near, targets_empty=True):
    bc, c0 = counts[1], counts[2]
    if near:
        return 0
    if not targets_empty:
        raise ValueError('target branch not modelled')
    if not flee or state == 1:
        return 1 if bc < 1 else 0
    if not flags & 0x1000:
        return 1
    return 1 if (c0 < 3 and bc < 7) else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--expected', type=Path, required=True)
    ap.add_argument('--output', type=Path)
    a = ap.parse_args()
    data = json.loads(a.expected.read_text())
    checked, gohome, failures, rows, skipped = 0, 0, [], [], []
    for name, scene in data['scenes'].items():
        flee_tick = min([t for t, act in scene['schedule'] if act[0] == 'cmd' and act[1] in (2, 5)], default=None)
        decisions = scene['decisions']
        for i, row in enumerate(decisions):
            if row['kind'] != 'home-reevaluate':
                continue
            b = row['before']
            flee = flee_tick is not None and row['tick'] >= flee_tick
            near, dist = near_home(scene, row)
            if near is None:  # AI-start SetCaptainHome before the first actor snapshot: not replayed
                skipped.append(dict(scene=name, tick=row['tick'], caller=row['caller'], live=row['result']))
                continue
            want = model(b['state'], int(b['flags'], 16), b['counts'], flee, near)
            same = [d for d in decisions if d['counter'] == row['counter']]
            placed = any(d['kind'] == 'actor-place' and decisions.index(d) < i and decisions.index(d) > i - 4 for d in same)
            rec = dict(scene=name, tick=row['tick'], caller=row['caller'], state=b['state'], flags=b['flags'], members=b['counts'][1],
                       c0=b['counts'][2], flee=flee, distance=round(dist, 2), model=want, live=row['result'], actor_placed=placed)
            rows.append(rec)
            ok = want == row['result']
            if want == 1:
                after = int(row['after']['flags'], 16)
                ok &= bool(after & 2) == (bool(int(b['flags'], 16) & 2) or b['counts'][1] > 0)  # 9d09ad only ORs bit 2
                ok &= placed == (b['counts'][1] == 0)
            if not ok:
                failures.append(rec)
            checked += 1
        for i, row in enumerate(decisions):
            if row['kind'] == 'go-home' and row['caller'] == '0x9cff7d':
                b, af = row['before'], row['after']
                near, dist = near_home(scene, row)
                gohome += 1
                if near:
                    clear = b['counts'][3] >= int(b['counts'][1] / 2)
                    if bool(int(af['flags'], 16) & 2) == clear and int(b['flags'], 16) & 2:
                        failures.append(dict(scene=name, tick=row['tick'], goal_event='retreat bit mismatch'))
    result = dict(expected=str(a.expected), reevaluations=checked, goal_gohome=gohome, rows=rows, skipped=skipped, failures=failures,
                  passed=not failures and checked > 0)
    if a.output:
        a.output.write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps(dict(reevaluations=checked, skipped=len(skipped), goal_gohome=gohome, failures=failures, passed=result['passed']), indent=1))
    raise SystemExit(0 if result['passed'] else 1)


if __name__ == '__main__':
    main()
