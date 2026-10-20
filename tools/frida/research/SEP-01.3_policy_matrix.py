#!/usr/bin/env python3
"""SEP-01.2/01.3: per-cluster policy matrix from a completed RS-SEP policy capture.

Separates (1) policy words written by 6f1710e0, (2) candidate eligibility decided by 6f16e830 for
each source visit (with the failing predicate) and (3) displacement (JASS samples, accepted
applications through 6f16ffa0). Writes a JSON report; never alters the capture.
"""
import argparse, hashlib, json, math, sys
from collections import Counter, defaultdict
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from sep_research_analyze import Capture, decode_word, filter_reason, fw  # noqa: E402


def summarize(cap):
    by_cluster = defaultdict(list)
    for ro in cap.roster:
        by_cluster[(ro['phase'], ro['cluster'])].append(ro['i'])
    out = []
    for (phase, cl), idxs in sorted(by_cluster.items()):
        units = []
        for i in idxs:
            ro = cap.roster[i]
            cfgs = [dict(tick=c['tick'], caller=hex(c['caller']), enable=c['args'][0], sep=c['sep'] != '0x0',
                         word=c['state']['word'] if c['state'] else None,
                         decoded=decode_word(c['state']['word']) if c['state'] else None) for c in cap.configs.get(i, [])]
            vs = cap.visits(i)
            reasons = Counter()
            for v in vs:
                for f in v.get('filter') or []:
                    if f['mover'] == v['mover']:
                        continue
                    j = f.get('_j')
                    reasons[(j, filter_reason(f, v['mover']))] += 1
            body = [v for v in vs if v['pairs'] or v['query'] is not None]
            moved = [v for v in vs if v['apply'] and v['apply']['pos'] != v['apply']['posAfter']]
            rejected = [v for v in vs if v['apply'] and v['apply'].get('valid') == 0]
            samp = cap.samples.get(i, [])
            disp = None
            if samp:
                (t0, x0, y0, *_), (t1, x1, y1, *_) = samp[0], samp[-1]
                disp = dict(first=[t0, x0, y0], last=[t1, x1, y1], dist=math.hypot(x1 - x0, y1 - y0))
            units.append(dict(i=i, code=ro['code'], owner=ro['owner'], configs=cfgs, visits=len(vs), bodyVisits=len(body),
                              applications=len(moved), rejectedEndpoints=len(rejected), draws=sum(len(v['draws']) for v in vs),
                              eligibility={f'{j}:{r}': n for (j, r), n in sorted(reasons.items(), key=str)},
                              maxVector=max((math.hypot(fw(v['after']['vec'][0]), fw(v['after']['vec'][1])) for v in vs), default=0.0),
                              displacement=disp))
        out.append(dict(phase=phase, cluster=cl, units=units))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture', type=Path, required=True)
    ap.add_argument('--map-json', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    a = ap.parse_args()
    cap = Capture(a.capture, a.map_json)
    rep = dict(capture=str(a.capture), capture_sha256=hashlib.sha256((a.capture / 'capture.jsonl').read_bytes()).hexdigest(),
               preload_sha256=hashlib.sha256((a.capture / 'preload.txt').read_bytes()).hexdigest(), complete=cap.complete,
               fine_offset=cap.offset, clusters=summarize(cap))
    a.report.write_text(json.dumps(rep, indent=1) + '\n')
    for c in rep['clusters']:
        print(f"P{c['phase']}C{c['cluster']}: " + ' | '.join(
            f"{u['i']}:{u['code']}/p{u['owner']} w={[hex(x['word']) if x['word'] is not None else None for x in u['configs']]} v={u['visits']} app={u['applications']} rej={u['rejectedEndpoints']} dr={u['draws']} d={u['displacement']['dist'] if u['displacement'] else None:.1f} el={u['eligibility']}"
            for u in c['units']))


if __name__ == '__main__':
    main()
