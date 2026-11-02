#!/usr/bin/env python3
"""FORM-01.3 policy-word witness table (research tool, new file).

From group032_observer.js / form05_observer.js captures, lists every distinct canonical request+100 / physical group+80
policy word at cohort activation (16b7b0 after 16bdb0), keyed by the producer return-address chain from 16bd83 upward,
with the request-flag setter calls (bit, value, call return address) since the previous activation of the same
request object, the first probe tick, count and member counts. Target bookkeeping 1000 (16bf70/16da10) is listed
separately because it is set/cleared at runtime rather than by a producer policy setter.
Nested 16b7b0 neighbour binds (return 16ba86) are not separate activations and are skipped.
"""
import argparse, hashlib, json, re
from collections import OrderedDict, defaultdict
from pathlib import Path

PRODUCERS = {  # return-address chain prefix (after 16be48,16b7b0,16bd83) -> producer description (assembly-mapped)
    ('5a77b', '5fc755', '5ffa99', '5fdb38'): 'CAbilityMove d016f target task (0,0,0) via 5ff8b0 -> 5fc640 -> 05a5c0',
    ('5a77b', '5fc755', '5ffa99', '5fdb4d'): 'CAbilityMove d0171 target task (1,0,0)',
    ('5a77b', '5fc755', '5ffa99', '5fdb62'): 'CAbilityMove d0170 target task (1,1,0) persistent',
    ('5a77b', '5fc755', '5ffa99', '5fdb77'): 'CAbilityMove d0172 target task (0,0,1)',
    ('5a77b', '5fc755', '5ffa99', '5fdb8c'): 'CAbilityMove d0174 target task (1,0,1) approach',
    ('5a77b', '5fc755', '5ffa99', '5fdba1'): 'CAbilityMove d0173 target task (1,1,1) persistent Follow',
    ('5a77b', '5fc755', '49a2cb'): 'CAbilityAttack 49a240 attack chase -> 5fc640 (persistent 0, range sentinel)',
    ('5bb90', '5ffdb4', '5fdbc3'): 'CAbilityMove d016b point task (0,1) via 5ffb60 -> 05b970',
    ('5bb90', '5ffdb4', '5fdbd6'): 'CAbilityMove d016c point task (0,0)',
    ('5bb90', '5ffdb4', '5fdbe9'): 'CAbilityMove d016d point task (1,1)',
    ('5bb90', '5ffdb4', '5fdbfc'): 'CAbilityMove d016e point task (1,0)',
    ('89cd34', '89ccfc', '6692a7'): 'shared point request 89cbf0/89cce0 (GroupPointOrder / UI selection / captain packet)',
    ('5bb90', '9d4580'): 'CaptainAI_PublishPointRequest 9d44d0 -> 05b970',
}


def key_of(chain):
    rest = [c for c in chain if c not in ('16be48', '16b7b0', '16bd83', 'ext')]
    for k in sorted(PRODUCERS, key=len, reverse=True):
        if tuple(rest[:len(k)]) == k:
            return k, PRODUCERS[k]
    return tuple(rest[:4]), 'unmapped'


def table(paths):
    out = OrderedDict()
    for path in paths:
        pending = defaultdict(list)
        tick = -1
        for line in Path(path).read_text().splitlines():
            r = json.loads(line)
            ev = r.get('event')
            if ev == 'marker':
                m = re.search(r'tick=(\d+)', r['value'])
                tick = int(m.group(1)) if m else tick
            elif ev == 'req-flag':
                pending[r['request']].append((r['setter'], r['bit'], r.get('value'), r['caller']))
            elif ev == 'cohort' and r.get('from') == '16be48':
                # Only the outer 16bdb0 activation (return 16be48); nested 16b7b0 neighbour binds return to 16ba86.
                req = r['requestFlags']
                grp = r.get('groupFlags', r.get('flags'))
                k, desc = key_of(r['chain'])
                setters = [s for s in pending.pop(r['request'], []) if s[0] not in ('16bf70', '16da10')]
                sig = (req, grp, desc, tuple(setters))
                e = out.setdefault(sig, dict(request_flags=req, group_flags=grp, producer=desc, chain_key=list(k),
                                             setters=[list(s) for s in setters], first=[Path(path).parent.parent.name + '/' + Path(path).name, tick], count=0,
                                             member_counts=set(), captures=set()))
                e['count'] += 1
                e['member_counts'].add(r['count'])
                e['captures'].add(Path(path).parent.parent.name + '/' + Path(path).name)
    rows = []
    for e in out.values():
        e['member_counts'] = sorted(e['member_counts'])
        e['captures'] = sorted(e['captures'])
        rows.append(e)
    return sorted(rows, key=lambda e: (int(e['group_flags'], 16), e['producer']))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('captures', nargs='+', type=Path)
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    rows = table(args.captures)
    res = dict(captures=[[p.parent.parent.name + '/' + p.name, hashlib.sha256(p.read_bytes()).hexdigest()] for p in args.captures], witnesses=rows,
               policy_words=sorted({r['group_flags'] for r in rows}, key=lambda v: int(v, 16)))
    text = json.dumps(res, indent=1)
    if args.output:
        args.output.write_text(text + '\n')
    for r in rows:
        print(r['group_flags'], r['request_flags'], r['count'], r['member_counts'], r['producer'], r['setters'], r['first'])


if __name__ == '__main__':
    main()
