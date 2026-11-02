#!/usr/bin/env python3
"""SEP-02.3/SEP-04.1: path-owner PRNG stream of a triad capture vs replayed overlap pairs.

Checks that every live 6f1b7130 call on the path owner (6fd53a48) is either a startup draw before the first
separation visit or exactly one replayed random-branch pair (same order, same before/after words), and
reports per-cluster draw counts, endpoint outcomes and occupancy changes for the overlap clusters.
"""
import argparse, hashlib, json, sys
from collections import Counter, defaultdict
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from sep_research_analyze import Capture  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture', type=Path, required=True)
    ap.add_argument('--map-json', type=Path, required=True)
    ap.add_argument('--replay', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    cap = Capture(a.capture, a.map_json)
    rows = json.loads(a.replay.read_text())['rows']
    live = [r for r in cap.rows if r['event'] == 'owner-draw']
    startup = [r for r in live if r['inSep'] is None]
    sep_draws = [r for r in live if r['inSep'] is not None]
    replayed = [(r['i'], r['visit'], s) for r in rows if r['kind'] == 'body' for s in r['steps'] if s['randomBranch']]
    pairs_ok = len(sep_draws) == len(replayed) and all(
        d['before'] == s['ownerBefore'] and d['after'] == s['ownerAfter'] for d, (_, _, s) in zip(sep_draws, replayed))
    cluster_of = {ro['i']: (ro['phase'], ro['cluster']) for ro in cap.roster}
    per = defaultdict(lambda: Counter())
    for r in rows:
        k = cluster_of.get(r['i'])
        if r['kind'] != 'body':
            per[k]['cooldownVisits' if r['kind'] == 'cooldown' else 'movingVisits'] += 1
            continue
        per[k]['bodies'] += 1
        per[k]['draws'] += sum(s['randomBranch'] for s in r['steps'])
        ap_ = r['apply']
        per[k]['accepted' if ap_['valid'] == 1 and ap_['moved'] else 'rejected' if ap_['valid'] == 0 else 'noApply'] += 1
        if r['occBefore'] != r['occAfter']:
            per[k]['occupancyChanges'] += 1
        if r['wordAfter'] & 0xffff == 7:
            per[k]['cooldownInstalls'] += 1
    first_sep = next(r for r in cap.rows if r['event'] == 'sep-update')
    res = dict(capture_sha256=hashlib.sha256((a.capture / 'capture.jsonl').read_bytes()).hexdigest(),
               replay_sha256=hashlib.sha256(a.replay.read_bytes()).hexdigest(),
               startup=dict(count=len(startup), callers=sorted({hex(r['caller']) for r in startup}),
                            initial=startup[0]['before'] if startup else None, final=startup[-1]['after'] if startup else None,
                            stream=[[r['before'], r['after']] for r in startup]),
               ownerAtFirstSeparationVisit=first_sep['ownerBefore'],
               separationDraws=len(sep_draws), replayedRandomPairs=len(replayed), streamEqualsReplayedPairs=pairs_ok,
               otherDraws=len(live) - len(startup) - len(sep_draws),
               finalOwner=live[-1]['after'] if live else None,
               perCluster={f'P{k[0]}C{k[1]}': dict(v) for k, v in sorted(per.items(), key=lambda kv: kv[0] or (-1, -1)) if k})
    a.out.write_text(json.dumps(res, indent=1) + '\n')
    print(json.dumps({k: v for k, v in res.items() if k not in ('startup',)}, indent=1)[:3000])


if __name__ == '__main__':
    main()
