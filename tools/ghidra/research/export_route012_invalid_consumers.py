#!/usr/bin/env python3
"""Literal original public-consumer states; no replacement path model."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'tools/ghidra/fixtures/retail-route-invalid-consumers-1.27.json'
OUTPUT = ROOT / 'games/warcraft-3/game/tests/retail_route_invalid_consumers.h'


def render():
    fixture = json.loads(SOURCE.read_text())
    assert len(fixture['rows']) == 56
    states, rows = [], []
    def words(values):
        return '{' + ','.join(f'0x{v:08x}u' for v in values) + '}'
    def state(visit):
        s = visit['state']
        assert len(s['fine_words']) <= 52 and len(s['coarse_words']) <= 34
        scalars = [visit['result'], s['fine_count'], s['fine_capacity'], s['fine_index'],
                   s['coarse_count'], s['coarse_capacity'], s['coarse_index'],
                   s['fine_time'], s['coarse_time'], s['delay'], s['retry'],
                   s['fine_bucket'][0], s['coarse_bucket'][0]]
        text = '{' + ','.join(f'0x{v:08x}u' for v in scalars)
        for values in [visit['rng'], s['output'], s['fine_words'] or [0], s['coarse_words'] or [0]]:
            text += ',' + words(values)
        text += '}'
        if text not in states:
            states.append(text)
        return states.index(text)
    for row in fixture['rows']:
        rows.append('{%s,%s,%s,%s,%s,{%s,%s}}' % (int(row['map'] == 'wall_gap'), row['cls'],
                    int(row['adaptive_enabled']), words(row['source']), words(row['rng']),
                    state(row['first']), state(row['second'])))
    return '''/* Generated from unchanged165ae0; full movement decision compares
 * buffers, scheduler work, stopped state and exact owner PRNG consumption. */
typedef struct {
    uint32_t result,count,capacity,index,coarse_count,coarse_capacity,coarse_index;
    uint32_t fine_time,coarse_time,delay,retry,fine_work,coarse_work;
    uint32_t rng[2],output[2],points[52],coarse_points[34];
} retailInvalidState_t;
static retailInvalidState_t const retail_invalid_states[] = {
''' + ',\n'.join(states) + '''
};
typedef struct { uint32_t wall,cls,adaptive,source[2],rng[2],state[2]; } retailInvalidConsumer_t;
static retailInvalidConsumer_t const retail_invalid_consumers[] = {
''' + ',\n'.join(rows) + '\n};\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    expected = render()
    if args.check:
        assert OUTPUT.read_text() == expected, 'invalid consumer fixture differs from original'
    else:
        OUTPUT.write_text(expected)


if __name__ == '__main__':
    main()
