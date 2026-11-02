#!/usr/bin/env python3
"""Check the C stage fixture against frozen, repeated original function outputs."""
import argparse
import gzip
import json
from pathlib import Path


def render(fixture):
    rows = fixture['cases']
    if len(rows) != 47 or [sum(r['scenario'] == s for r in rows) for s in (0, 1)] != [20, 27]:
        raise ValueError('incomplete retry lifetimes')
    out = ['/* Derived solely from repeated original retry-lifetime196 exports. */',
           'static const struct {',
           '    uint32_t scenario,step,count,counter,flags[3],ranges[3],completed,notify_counter;',
           '    uint32_t change_member,change_world;',
           '} retry196_rows[]={']
    words = lambda values: ','.join('0x%08xu' % x for x in values)
    for row in rows:
        decisions = row['decisions']
        flags = [d[10] for d in decisions] + [0] * (3 - len(decisions))
        completed = 0
        for notify in row['notifications']:
            completed |= 1 << next(d[0] for d in decisions if d[5] == notify[1])
        change = row['changes'][0] if row['changes'] else [0xffffffff, 0, 0]
        values = [row['scenario'], row['step'], len(decisions), row['counter']]
        out.append('    {%s,{%s},{%s},%s,%s,%s,%s},' % (
            words(values), words(flags), words(row['ranges'] + [0] * (3 - len(row['ranges']))),
            hex(completed), row['notification_counters'][0] if row['notifications'] else 0,
            hex(change[0]), hex(change[1])))
    return '\n'.join(out + ['};', ''])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fixture', type=Path, required=True)
    p.add_argument('--header', type=Path, required=True)
    a = p.parse_args()
    raw = a.fixture.read_bytes()
    if a.fixture.suffix == '.gz':
        raw = gzip.decompress(raw)
    if render(json.loads(raw)) != a.header.read_text():
        raise ValueError('engine stage fixture differs from retail')
    print('47 original retry/range/completion stages match the C fixture')


if __name__ == '__main__':
    main()
