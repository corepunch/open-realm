#!/usr/bin/env python3
"""Export literal captured full-owner visits, ordered pairs and retry decisions."""
import argparse
import gzip
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'tools/ghidra/fixtures/retail-separation-crowds-1.27.json.gz'
OUTPUT = ROOT / 'games/warcraft-3/game/tests/retail_separation_crowds.h'


def render():
    data = json.loads(gzip.decompress(SOURCE.read_bytes()))
    def array(values):
        return '{' + ','.join(array(v) if isinstance(v, list) else str(v) if v < 0 else f'0x{v:08x}u' for v in values) + '}'
    out = ['/* Literal complete SEP-04.2/03 captures; no reconstructed route model. */',
           'typedef struct { unsigned unit,tick,visit; uint32_t before[5],after[5]; int box[4];',
           '    uint32_t random_before[2],random_after[2],endpoint[2]; int admission; } crowdVisit_t;',
           'typedef struct { unsigned unit,tick,visit,before,after,result; uint32_t random_before[2],random_after[2]; } crowdRetry_t;']
    for scene in data:
        name = scene['variant']
        out += [f'static char const {name}_script[]=']
        out += ['    ' + json.dumps(line + '\n') for line in scene['script'].splitlines()]
        out += [';', f'static crowdVisit_t const {name}_visits[]={{']
        out += [array([r[k] for k in ('unit','tick','visit','before','after','box','random_before','random_after','endpoint','admission')]) + ',' for r in scene['visits']]
        out += ['};', f'static uint32_t const {name}_pairs[][14]={{']
        out += [array(pair) + ',' for r in scene['visits'] for pair in r['pairs']]
        out += ['};', f'static crowdRetry_t const {name}_retries[]={{']
        out += [array([r[k] for k in ('unit','tick','visit','before','after','result','random_before','random_after')]) + ',' for r in scene['retries']]
        out += ['};', f'static unsigned const {name}_blocked[][2]={{']
        out += [array(cell) + ',' for cell in scene['map']['blocked_cells']]
        out += ['};', f'static struct {{ unsigned tick,unit,order,owner; float x,y; }} const {name}_samples[]={{']
        samples = sorted((values[0], int(unit), values) for unit, rows in scene['samples'].items() for values in rows)
        out += ['{%u,%u,%u,%u,%.4ff,%.4ff},' % (tick, unit, values[3], values[4], values[1], values[2])
                for tick, unit, values in samples]
        out += ['};']
    return '\n'.join(out) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    expected = render()
    if args.check:
        assert OUTPUT.read_text() == expected, 'full-owner literal fixture changed'
    else:
        OUTPUT.write_text(expected)


if __name__ == '__main__':
    main()
