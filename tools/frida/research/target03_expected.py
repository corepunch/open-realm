#!/usr/bin/env python3
"""TARGET-03.2: freeze per-policy loss/reacquisition outcomes from target03_analyze reports (research tool, new file).

Inputs: two or more `target03_analyze.py --report` files of observed repeats (+ their `--timeline-report` files) and an
optional preload comparison (`target021_compare_preload.py --out`). Every compared field must be identical across the
repeats (absolute owner counters included); otherwise the tool exits non-zero and writes nothing.

Per scene the expected file records: public order/visibility transitions (0.1 s probe samples), target tasks, TargetLost
producers, CAbilityMove_OnTargetLost handler entries, CAbilityMove_ValidateTarget results with target +20/+5c words,
the CUnit_IsWidgetVisibleToOwner branch traces, hidden-visit episodes of the follower group (unseen counter, refresh
countdown, cached destination before/after, first resample after reacquisition) and the derived outcome.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

COMPARED = ('public_transitions', 'tasks', 'groups', 'target_lost', 'handler', 'validate', 'visibility_queries',
            'group_hidden', 'counts', 'markers')


def episodes(timeline):
    """Contiguous hidden visits per group (blocked group-callback query), with reacquisition and first resample."""
    out = []
    by_gi = {}
    for row in timeline:
        by_gi.setdefault(row['gi'], []).append(row)
    for gi, rows in sorted(by_gi.items()):
        cur = None
        for i, r in enumerate(rows):
            hidden = any(d.get('ev') == 'vis-query' and d.get('role') == 'group-callback' and d.get('result') == 0
                         for d in r['decisions'])
            if hidden and cur is None:
                prev = rows[i - 1] if i else None
                cur = dict(gi=gi, group_identity=r['group'].split('@')[-1], first=r['c'], cd_first=r['cd'], unseen_first=r['unseen'],
                           dest_before=(prev or r)['gpath']['dest'] if (prev or r).get('gpath') else None, visits=0)
            if hidden:
                cur['visits'] += 1
                cur['last'] = r['c']
                cur['cd_last'] = r['cd']
                cur['unseen_last_entry'] = r['unseen']
                cur['dest_hidden'] = r['gpath']['dest'] if r.get('gpath') else None
                cur['member_dest_hidden'] = r['self']['dest'] if r.get('self') else None
            visible = any(d.get('ev') == 'vis-query' and d.get('role') == 'group-callback' and d.get('result') == 1
                          for d in r['decisions'])
            if not hidden and not visible and cur is not None:
                cur['reacquired'] = None
                cur['ended'] = dict(c=r['c'], unseen_entry=r['unseen'], cd_entry=r['cd'], member_visit=r.get('self') is not None,
                                    group_flags_after=(r.get('after') or {}).get('flags'))
                out.append(cur)
                cur = None
                continue
            if not hidden and cur is not None:
                cur['reacquired'] = dict(c=r['c'], unseen_entry=r['unseen'], cd_entry=r['cd'],
                                         dest=r['gpath']['dest'] if r.get('gpath') else None)
                resample = None
                base = cur['dest_hidden']
                for r2 in rows[i:]:
                    if r2.get('gpath') and r2['gpath']['dest'] != base:
                        resample = dict(c=r2['c'], dest=r2['gpath']['dest'], visits_after_reacquire=r2['c'] - r['c'])
                        break
                cur['first_destination_change'] = resample
                out.append(cur)
                cur = None
        if cur is not None:
            cur['reacquired'] = None
            cur['group_end'] = rows[-1]['c']
            out.append(cur)
    return out


def outcome(scene):
    tr = scene['public_transitions']
    ordered = [t for t in tr if t['local'] >= 5 and t['follower_order'] not in (None, '0')]
    if not ordered:
        return dict(kind='order-rejected', order=None)
    first = ordered[0]
    before_stop = [t for t in tr if t['local'] < 190]
    last = before_stop[-1]
    ends = [t for t in tr if t['local'] > first['local'] and t['follower_order'] == '0' and t['local'] < 190]
    kind = 'retained' if last['follower_order'] == first['follower_order'] and not ends else 'cancelled'
    res = dict(kind=kind, order=first['follower_order'], order_local=first['local'])
    if ends:
        res['order_zero_local'] = ends[0]['local']
        reissued = [t for t in tr if t['local'] > ends[0]['local'] and t['follower_order'] not in (None, '0') and t['local'] < 190]
        if reissued:
            res['reissued_local'] = reissued[0]['local']
            res['kind'] = 'cancelled-then-reissued-retained' if last['follower_order'] != '0' else 'cancelled'
    deciding = [v for v in scene['validate'] if v['result'] != '0x0']
    if deciding:
        res['deciding_validation'] = deciding[0]
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reports', type=Path, nargs='+', required=True)
    ap.add_argument('--timelines', type=Path, nargs='+', required=True)
    ap.add_argument('--names', nargs='+', required=True)
    ap.add_argument('--catalog', type=Path, required=True, help='JSON {scene: {name, producer, policy}}')
    ap.add_argument('--preload-compare', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    reps = [json.loads(p.read_text()) for p in a.reports]
    tls = [json.loads(p.read_text()) for p in a.timelines]
    catalog = json.loads(a.catalog.read_text())
    ref = reps[0]
    diffs = []
    for n, r in zip(a.names[1:], reps[1:]):
        for x, y in zip(ref['scenes'], r['scenes']):
            for k in COMPARED:
                if x[k] != y[k]:
                    diffs.append((n, x['scene'], k))
    eps = [[episodes(s['timeline']) for s in t['scenes']] for t in tls]
    for n, e in zip(a.names[1:], eps[1:]):
        if e != eps[0]:
            diffs.append((n, 'episodes', None))
    if diffs:
        print('repeat mismatch:', diffs[:20], file=sys.stderr)
        return 1
    scenes = []
    for s, ep in zip(ref['scenes'], eps[0]):
        cat = catalog.get(str(s['scene']), {})
        scenes.append(dict(scene=s['scene'], **cat, window=s['window'], outcome=outcome(s),
                           public_transitions=s['public_transitions'], markers=s['markers'], tasks=s['tasks'],
                           target_lost=s['target_lost'], handler=s['handler'], validate=s['validate'],
                           visibility_queries=s['visibility_queries'], group_hidden=s['group_hidden'],
                           hidden_episodes=ep, groups=s['groups']))
    doc = dict(task='TARGET-03.2', binary_sha256='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236',
               captures=[dict(name=n, report=str(p), capture=r['capture'], capture_sha256=r['capture_sha256'])
                         for n, p, r in zip(a.names, a.reports, reps)],
               repeats_identical=True, compared_fields=list(COMPARED) + ['hidden_episodes'],
               preload_compare=json.loads(a.preload_compare.read_text()) if a.preload_compare else None,
               notes='counters are path-owner +538 visits; destinations are fine-cell floats; validate results: 0 keep, '
                     '0xdd invisible/fogged/dead/null, 0xaa hidden or removed, 0xa9 loaded cargo',
               scenes=scenes)
    a.output.write_text(json.dumps(doc, indent=1))
    print(a.output, hashlib.sha256(a.output.read_bytes()).hexdigest())
    for s in scenes:
        print(s['scene'], s.get('name'), s['outcome'], [(e['first'], e['last'], e['visits'], (e['reacquired'] or {}).get('c'),
                                                      (e.get('first_destination_change') or {}).get('c')) for e in s['hidden_episodes']])
    return 0


if __name__ == '__main__':
    sys.exit(main())
