#!/usr/bin/env python3
"""Freeze/compare normalized separation sequences from verify_SEP-02.2_replay.py reports.

Normalization drops heap pointers and wall time only; unit identity is the JASS creation index.
usage: sep_research_expected.py --replay A.json [--repeat B.json] --units 0 1 2 [--units ...] --out X.json
"""
import argparse, hashlib, json
from pathlib import Path


def norm(row):
    r = dict(i=row['i'], visit=row['visit'], kind=row['kind'], wordBefore=row['wordBefore'], vecBefore=row['vecBefore'],
             vecAfter=row['vecAfter'], wordAfter=row['wordAfter'], posBefore=row['posBefore'], posAfter=row['posAfter'],
             occBefore=row['occBefore'], occAfter=row['occAfter'], match=row.get('match'))
    if row['kind'] == 'body':
        ap = row['apply']
        r['apply'] = dict(vec=ap['vec'], endpoint=ap['endpoint'], valid=ap['valid'], moved=ap['moved'], pos=ap['pos'], posAfter=ap['posAfter'],
                          occ=ap['occ'], occAfter=ap['occAfter'])
        r['neighbors'] = [dict(j=s['j'], source=s['source'], candidate=s['candidate'], randomBranch=s['randomBranch'],
                               ownerBefore=s['ownerBefore'], ownerAfter=s['ownerAfter'], vecBefore=s['vecBefore'],
                               vecAfter=s['vecAfterLive'], distanceWord=s['distanceWord'], match=s['match']) for s in row['steps']]
        r['accumulated'] = row['accumulated']
        r['tail'] = dict(oracle=row['tailOracle'], outcome=row['outcome'], match=row['tailMatch'])
    return r


def sequences(replay, groups):
    rows = json.loads(Path(replay).read_text())['rows']
    return [[norm(r) for r in rows if r['i'] in set(g)] for g in groups]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--replay', type=Path, required=True)
    ap.add_argument('--repeat', type=Path)
    ap.add_argument('--units', type=int, nargs='+', action='append', required=True)
    ap.add_argument('--label', action='append')
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    first = sequences(a.replay, a.units)
    res = dict(source=str(a.replay), source_sha256=hashlib.sha256(a.replay.read_bytes()).hexdigest(), groups=[])
    second = sequences(a.repeat, a.units) if a.repeat else None
    for k, g in enumerate(a.units):
        entry = dict(label=(a.label or [None] * len(a.units))[k], units=g, visits=len(first[k]),
                     allMatch=all(r['match'] for r in first[k]), sequence=first[k])
        if second is not None:
            entry['repeatEqual'] = first[k] == second[k]
            if not entry['repeatEqual']:
                diff = next((n for n, (x, y) in enumerate(zip(first[k], second[k])) if x != y), min(len(first[k]), len(second[k])))
                entry['firstDifference'] = dict(index=diff, first=first[k][diff] if diff < len(first[k]) else None,
                                                repeat=second[k][diff] if diff < len(second[k]) else None)
        res['groups'].append(entry)
    if a.repeat:
        res['repeat'] = str(a.repeat); res['repeat_sha256'] = hashlib.sha256(a.repeat.read_bytes()).hexdigest()
    a.out.write_text(json.dumps(res, indent=None, separators=(',', ':')) + '\n')
    print(json.dumps([dict(label=g['label'], units=g['units'], visits=g['visits'], allMatch=g['allMatch'], repeatEqual=g.get('repeatEqual'),
                           firstDifference=(g.get('firstDifference') or {}).get('index')) for g in res['groups']], indent=1))


if __name__ == '__main__':
    main()
