#!/usr/bin/env python3
"""Copy constructed routes from unchanged original detour and public captures."""
import argparse
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[3]
SOURCES = {
    'detour': 'tools/ghidra/fixtures/retail-primary-owner-route-1.27.json',
    'blocked': 'tools/ghidra/fixtures/retail-blocked-goal-1.27.json',
    'disconnected': 'tools/ghidra/fixtures/retail-gate-group-wall-live-1.27.json',
}


def routes(root=ROOT):
    result = {}
    for name, path in SOURCES.items():
        spec = json.loads((root / path).read_text())
        if name == 'detour':
            points = spec['cases'][0]['route']
            result[name] = [dict(kind=0, budget=700, pops=None, complete=True,
                                 points=list(zip(points[::2], points[1::2])))]
            continue
        events = spec['lifecycle'] if name == 'blocked' else spec['observations']
        builds = {r['request']: r for r in events if r['event'] == 'search'}
        result[name] = []
        for row in events:
            if row['event'] != 'route' or row['request'] not in builds:
                continue  # Denied admissions have no search: no constructed route.
            query = builds.pop(row['request'])
            if row['truncated'] or len(row['points']) != row['count']:
                raise ValueError('incomplete original route')
            result[name].append(dict(kind=int(row['kind'] == 'acc'), budget=query['budget'],
                                     pops=query['pops'], complete=query['result'] >= 0,
                                     points=row['points']))
        if builds:
            raise ValueError('missing original route return')
    if [len(result[n]) for n in SOURCES] != [1, 6, 30]:
        raise ValueError('original route extent differs')
    return result


def render(root=ROOT):
    text = ['/* Literal routes copied from unchanged retail fixtures; no expected-policy model. */',
            'typedef struct { uint32_t kind,budget,pops,count; bool complete; uint32_t const (*points)[2]; } baseline203Route_t;']
    for name, rows in routes(root).items():
        for i, row in enumerate(rows):
            text.append(f'static uint32_t const baseline203_{name}_points{i}[][2]={{')
            for point in row['points']:
                words = [struct.unpack('<I', struct.pack('<f', value))[0] for value in point]
                text.append('    {' + ','.join(f'0x{w:08x}u' for w in words) + '},')
            text.append('};')
        text.append(f'static baseline203Route_t const baseline203_{name}[]={{')
        for i, row in enumerate(rows):
            pops = 'UINT32_MAX' if row['pops'] is None else str(row['pops'])
            text.append(f'    {{{row["kind"]},{row["budget"]},{pops},{len(row["points"])},{str(row["complete"]).lower()},baseline203_{name}_points{i}}},')
        text.append('};')
    spec = json.loads((root / SOURCES['blocked']).read_text())
    retry = [r for r in spec['lifecycle'] if r['event'] == 'retry-result']
    if len(retry) != 2:
        raise ValueError('missing blocked-goal retry events')
    text.append('static uint32_t const baseline203_blocked_retries[][8]={')
    for row in retry:
        words = row['nativeSource'] + row['nativeGoal'] + [row['before'], row['after'], row['result'], row['members']]
        text.append('    {' + ','.join(f'0x{w:08x}u' for w in words) + '},')
    text.append('};')
    return '\n'.join(text) + '\n'


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--header', type=Path, required=True)
    a = p.parse_args()
    if render() != a.header.read_text():
        raise ValueError('engine E2E route expectations differ from retail')
    print('37 complete original route returns match the C fixture')
