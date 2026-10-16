#!/usr/bin/env python3
"""GROUP-03.4.7.3: summarize one observed captain-policy capture (read-only JSONL -> JSON report).

Orders every captain decision (SetHome, home re-evaluation 9d08e0 with engagement test 9d73c0,
GoHome, empty-roster actor placement, point requests, CaptainAttack, member removal, roster
attach/detach, member reissues, CaptainRetreating results) on the JASS tick/owner-counter axis and
extracts the attack captain's actor/request/flag timeline and per-member physical positions from the
marker snapshots.  Raw words are retained; floats are decoded alongside.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def f32(word):
    return struct.unpack('<f', struct.pack('<I', word & 0xffffffff))[0]


def cap(c):
    if not c:
        return None
    out = dict(state=c['state'], flags=hex(c['flags']), order=hex(c['order']), counts=c['counts'],
               current=[round(f32(c['current'][0]), 3), round(f32(c['current'][2]), 3)] if 'current' in c else None,
               request=[round(f32(v), 3) for v in c['request']], request_range=round(f32(c['requestRange']), 3),
               home=[round(f32(v), 3) for v in c['home']],
               targets=[t != [0xffffffff, 0xffffffff] for t in c['targets']])
    a = c.get('actor')
    if a and 'position' in a:
        out['actor_fine'] = [round(f32(a['position'][0]), 4), round(f32(a['position'][1]), 4)]
        out['actor_words'] = a['position']
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    args = ap.parse_args()
    raw = args.capture.read_bytes()
    rows = [json.loads(x) for x in raw.splitlines()]
    tick, decisions, timeline, ai = 0, [], [], []
    attack = None
    for r in rows:
        e = r.get('event')
        if e == 'marker':
            v = r['value']
            if v.startswith('RSH tick='):
                tick = int(v.split()[1][5:])
            if v.startswith('RSH ai '):
                ai.append(dict(tick=tick, counter=r.get('counter'), value=v))
            elif ' label=sample ' not in v:
                decisions.append(dict(tick=tick, counter=r.get('counter'), kind='marker', value=v))
        elif e == 'captain-call':
            row = dict(tick=tick, counter=r.get('counter'), kind=r['name'], caller=hex(r['caller']), result=r['result'],
                       before=cap(r['before']), after=cap(r['after']))
            for k in ('x', 'y', 'range'):
                if k in r:
                    row[k] = round(f32(r[k]), 3)
            for k in ('retain', 'prepare', 'unit'):
                if k in r:
                    row[k] = r[k]
            decisions.append(row)
        elif e in ('engagement-test', 'actor-place', 'reissue', 'retreating-native'):
            row = dict(tick=tick, counter=r.get('counter'), kind=e, **{k: v for k, v in r.items() if k not in ('event', 'ms', 'counter', 'tick')})
            for k in ('x', 'y'):
                if k in row and isinstance(row[k], int):
                    row[k + '_f'] = round(f32(row[k]), 3)
            if e != 'retreating-native':
                decisions.append(row)
        elif e == 'roster':
            attack = attack or r['captain']['captain']
            decisions.append(dict(tick=tick, counter=r.get('counter'), kind='roster-' + r['name'], unit=r['unit'],
                                  caller=hex(r['caller']), captain=r['captain']['captain'], counts=r['captain']['counts']))
        elif e == 'snapshot':
            caps = {c['captain']: cap(c) for c in r['captains'] if 'state' in c}
            members = []
            for m in r['members']:
                mv = m.get('mover') or {}
                members.append([m['unit'], [round(f32(mv['position'][0]), 4), round(f32(mv['position'][1]), 4)] if 'position' in mv else None])
            timeline.append(dict(tick=tick, counter=r.get('counter'), reason=r['reason'][:60], captains=caps, members=members))
    report = dict(capture=str(args.capture), sha256=hashlib.sha256(raw).hexdigest(), rows=len(rows),
                  complete=any(d.get('value', '').endswith('label=complete') for d in decisions), footer=rows[-1],
                  decisions=decisions, ai_reports=ai, timeline=timeline)
    args.report.write_text(json.dumps(report, indent=1) + '\n')
    for d in decisions:
        if d['kind'] in ('marker',):
            print(d['tick'], d['value'][:100])
        elif d['kind'] in ('roster-attach', 'roster-detach', 'reissue'):
            continue
        else:
            b, a = d.get('before') or {}, d.get('after') or {}
            print(d['tick'], d.get('counter'), d['kind'], d.get('result'), b.get('state'), b.get('flags'), '->', a.get('state'), a.get('flags'),
                  b.get('counts'), d.get('x'), d.get('y'), d.get('range'), a.get('request'), a.get('actor_fine'))
    for x in ai:
        print('AI', x['tick'], x['value'][7:])


if __name__ == '__main__':
    main()
