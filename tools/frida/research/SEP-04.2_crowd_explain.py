#!/usr/bin/env python3
"""SEP-04.2: explain each crowd unit's displacement against the recovered separation model.

Inputs: a completed crowd capture (sep_research_observer.js, --pair-probes --retry-events), the map json and
the verify_SEP-02.2_replay.py report for the same capture. Per unit:
  policy   : packed word, the candidate set that can push it (6f16e830 decisions with failing predicate);
  arithmetic: replayed bodies (original pair/tail on live words; must all match);
  endpoint : accepted/rejected applications of the retained vector (6f16ffa0/6f16ee80) and, for each
             rejection, a reconstruction of what overlaps the endpoint footprint (terrain cells from the map
             json, other movers' last observed occupancy rectangles, the disabled unit from JASS samples);
  motion   : separation displacement (sum of accepted application deltas) vs total JASS displacement; the
             remainder is ordinary path/velocity movement.
Reconstructions are labelled as such; they are not additional retail calls.
"""
import argparse, hashlib, json, math, sys
from collections import Counter, defaultdict
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from sep_research_analyze import Capture, decode_word, filter_reason, fw  # noqa: E402


def footprint(x, y, r):
    fx, fy = math.floor(x), math.floor(y)
    if r < 0.5:
        return [(fx, fy)]
    if r < 1.0:
        return [(fx - 1 + i, fy - 1 + j) for i in range(2) for j in range(2)]
    if r < 1.5:
        return [(fx - 1 + i, fy - 1 + j) for i in range(3) for j in range(3)]
    return [(fx - 2 + i, fy - 2 + j) for i in range(4) for j in range(4)]


def rect_cells(rect):
    miny, minx, maxy, maxx = rect
    return {(x, y) for x in range(minx, maxx) for y in range(miny, maxy)}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture', type=Path, required=True)
    ap.add_argument('--map-json', type=Path, required=True)
    ap.add_argument('--replay', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    a = ap.parse_args()
    cap = Capture(a.capture, a.map_json)
    rep_rows = {(r['i'], r['visit']): r for r in json.loads(a.replay.read_text())['rows']}
    blocked = {tuple(c) for c in cap.meta['blocked_cells']}
    last_occ = {}
    jass_disabled = [i for i, ro in enumerate(cap.roster) if not cap.meta['units'][ro['code']].get('urpo')]
    out = {}
    per = defaultdict(lambda: dict(bodies=0, bodiesMatched=0, accepted=0, rejected=0, sepDelta=[0.0, 0.0], reasons=Counter(),
                                   rejectCauses=Counter(), movingVisits=0, cooldownVisits=0))
    for v in cap.updates:
        i = v.get('_i')
        if i is None:
            continue
        P = per[i]
        last_occ[i] = v['moverBefore']['occ']
        if v['before']['word'] & 0xffff:
            P['cooldownVisits'] += 1
        elif v['moverBefore']['speed'] not in (0, 0x80000000):
            P['movingVisits'] += 1
        else:
            P['bodies'] += 1
            rr = rep_rows.get((i, v['visit']))
            P['bodiesMatched'] += bool(rr and rr['match'])
            for f in v.get('filter') or []:
                if f['mover'] != v['mover']:
                    P['reasons'][(f.get('_j'), filter_reason(f, v['mover']))] += 1
            apl = v['apply']
            if apl and apl.get('valid') == 1 and apl['pos'] != apl['posAfter']:
                P['accepted'] += 1
                P['sepDelta'][0] += fw(apl['posAfter'][0]) - fw(apl['pos'][0])
                P['sepDelta'][1] += fw(apl['posAfter'][1]) - fw(apl['pos'][1])
            elif apl and apl.get('valid') == 0:
                P['rejected'] += 1
                ex, ey = fw(apl['endpoint'][0]), fw(apl['endpoint'][1])
                cells = footprint(ex, ey, fw(v['moverBefore']['radius']))
                causes = []
                if any(c in blocked or not (0 <= c[0] < 64 and 0 <= c[1] < 64) for c in cells):
                    causes.append('terrain')
                for j, occ in last_occ.items():
                    if j != i and occ and rect_cells(occ) & set(cells):
                        causes.append(f'occupancy:{j}')
                # disabled units have no separation rows: reconstruct their 1x1 footprint from the last JASS sample
                for j in jass_disabled:
                    s = [t for t in cap.samples.get(j, []) if t[0] <= max(v['tick'], 0)]
                    if s and (math.floor(s[-1][1] / 32), math.floor(s[-1][2] / 32)) in set(cells):
                        causes.append(f'occupancy:{j}(jass)')
                P['rejectCauses'][' + '.join(causes) or 'unexplained'] += 1
        last_occ[i] = v['moverAfter']['occ']
    for i, ro in enumerate(cap.roster):
        P = per[i]
        s = cap.samples.get(i, [])
        tot = [(s[-1][1] - s[0][1]) / 32, (s[-1][2] - s[0][2]) / 32] if s else None
        cfg = [c for c in cap.configs.get(i, []) if c['state']]
        word = cfg[0]['state']['word'] if cfg else None
        out[i] = dict(code=ro['code'], owner=ro['owner'], units=cap.meta['units'][ro['code']], word=word,
                      decoded=decode_word(word) if word is not None else None,
                      pushers=sorted({j for (j, r), n in P['reasons'].items() if r == 'eligible'}, key=lambda x: (x is None, x)),
                      eligibility={f'{j}:{r}': n for (j, r), n in sorted(P['reasons'].items(), key=str)},
                      bodies=P['bodies'], bodiesMatched=P['bodiesMatched'], movingVisits=P['movingVisits'], cooldownVisits=P['cooldownVisits'],
                      accepted=P['accepted'], rejected=P['rejected'], rejectCauses=dict(P['rejectCauses']),
                      separationDisplacementFine=P['sepDelta'], totalDisplacementFine=tot,
                      pathDisplacementFine=[tot[0] - P['sepDelta'][0], tot[1] - P['sepDelta'][1]] if tot else None,
                      first=s[0] if s else None, last=s[-1] if s else None)
    rep = dict(capture_sha256=hashlib.sha256((a.capture / 'capture.jsonl').read_bytes()).hexdigest(),
               replay_sha256=hashlib.sha256(a.replay.read_bytes()).hexdigest(), complete=cap.complete, units=out,
               note='rejectCauses are reconstructions from observed occupancy rectangles/terrain/JASS samples (inference), not retail calls')
    a.report.write_text(json.dumps(rep, indent=1) + '\n')
    for i, u in out.items():
        print(i, u['code'], 'p%d' % u['owner'], 'pushers', u['pushers'], 'bodies %d/%d' % (u['bodiesMatched'], u['bodies']), 'acc', u['accepted'], 'rej', u['rejected'],
              u['rejectCauses'], 'sep', [round(x, 3) for x in u['separationDisplacementFine']], 'tot', u['totalDisplacementFine'] and [round(x, 3) for x in u['totalDisplacementFine']])


if __name__ == '__main__':
    main()
