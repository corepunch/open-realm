#!/usr/bin/env python3
"""Freeze expected-SEP-01.2.json / expected-SEP-01.3.json from the policy oracle and policy-matrix reports.

Each live case is reduced to: units (rawcode/owner/authored fields), the packed words written by
6f1710e0 (init and runtime refreshes), the per-direction 6f16e830 decision with failing predicate, and
whether separation moved the unit (accepted 6f16ffa0 applications, JASS displacement).
"""
import argparse, hashlib, json
from pathlib import Path

LABELS = {
    (0, 0): 'baseline same owner/group/rank', (0, 1): 'owner 0 vs owner 1', (0, 2): 'enabled vs repulse=0',
    (0, 3): 'group 0 vs group 1', (0, 4): 'group 1 vs group 17 (alias &15)', (0, 5): 'rank 1 vs rank 0',
    (0, 6): 'rank 2 vs rank 1', (0, 7): 'rank 17 vs rank 1 (alias &15)', (0, 8): 'repulse=2 vs repulse=1',
    (1, 0): 'selector 0 pair at 6 fine', (1, 1): 'selector 1 pair at 6 fine', (1, 2): 'selector 2 pair at 6 fine',
    (1, 3): 'selector 3 pair at 6 fine', (1, 4): 'selector 4 pair at 6 fine', (1, 5): 'selector 5 (zero row) pair',
    (1, 6): 'selector 5 vs selector 0', (1, 7): 'selector 17 vs selector 1 (alias &15)', (1, 8): 'repulse=0 pair',
    (2, 0): 'fly vs fly', (2, 1): 'fly vs foot', (2, 2): 'hover vs foot', (2, 3): 'amph vs foot', (2, 4): 'horse vs foot',
    (2, 5): 'hover vs hover', (2, 6): 'fly owner 0 vs fly owner 2', (2, 7): 'owner 15 vs owner 15', (2, 8): 'owner 15 vs owner 0',
    (3, 0): "owner 15 vs owner 0 + UnitAddAbility('Amec') + SetUnitOwner(1)", (3, 1): 'owner 15 vs owner 0 + SetUnitOwner(1)',
    (3, 2): "owner 15 vs owner 0 + 'Amec' + PauseUnit true/false", (3, 3): 'owner 0 vs owner 1 + SetUnitOwner(0)',
    (3, 4): 'channel (ANcl clone) on second unit, collapse', (3, 5): 'PauseUnit on second unit, collapse, unpause',
    (3, 6): 'amph vs amph', (3, 7): 'horse vs horse', (3, 8): 'radius 1.0 vs radius 0.25 at 0.25 fine'}


def reduce_case(c, units_meta):
    us = c['units']
    out = []
    for u in us:
        other = [x for x in us if x['i'] != u['i']]
        dec = {}
        for k, n in u['eligibility'].items():
            j, reason = k.split(':', 1)
            dec.setdefault(j, {})[reason] = n
        out.append(dict(i=u['i'], code=u['code'], owner=u['owner'], authored=units_meta[u['code']],
                        words=[(x['caller'], None if x['word'] is None else '%08x' % x['word']) for x in u['configs']],
                        decisions={j: d for j, d in dec.items() if j != 'None'}, visits=u['visits'], applications=u['applications'],
                        rejectedEndpoints=u['rejectedEndpoints'], draws=u['draws'],
                        jassDisplacementWorld=round(u['displacement']['dist'], 4) if u['displacement'] else None,
                        separated=bool(u['applications'])))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--oracle', type=Path, required=True)
    ap.add_argument('--matrix', type=Path, required=True)
    ap.add_argument('--repeat-matrix', type=Path, required=True)
    ap.add_argument('--map-json', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    orc = json.loads(a.oracle.read_text())
    m1, m2 = json.loads(a.matrix.read_text()), json.loads(a.repeat_matrix.read_text())
    meta = json.loads(a.map_json.read_text())
    assert m1['clusters'] == m2['clusters'], 'repeat matrix differs'
    cases = [dict(phase=c['phase'], cluster=c['cluster'], label=LABELS[(c['phase'], c['cluster'])], units=reduce_case(c, meta['units']))
             for c in m1['clusters']]
    exp = dict(task='SEP-01.2/SEP-01.3', binary_sha256=orc['binary_sha256'], crt_sha256=orc['crt_sha256'],
               oracle=dict(sha256=hashlib.sha256(a.oracle.read_bytes()).hexdigest(), packed_rule=orc['packed_rule'],
                           packed_word_cases=orc['packed_word_cases'], harness_selfcheck=orc['harness_selfcheck'],
                           inert_rows=orc['inert_rows'], inert_pairs=orc['inert_pairs'], inert_tails=orc['inert_tails']),
               map=dict(name=meta['name'], sha256=meta['sha256']),
               captures=dict(first=m1['capture_sha256'], repeat=m2['capture_sha256'], first_preload=m1['preload_sha256'], repeat_preload=m2['preload_sha256']),
               callers={'0x5c9e1': '6f05c9c0 via 6f693d50 refresh', '0x16eb5e': 'Mover_Destroy 6f16eb20'},
               cases=cases)
    a.out.write_text(json.dumps(exp, indent=1) + '\n')
    print(a.out, hashlib.sha256(a.out.read_bytes()).hexdigest(), len(cases))


if __name__ == '__main__':
    main()
