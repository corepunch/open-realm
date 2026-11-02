#!/usr/bin/env python3
"""Export literal native ground-support results; never calculate expected heights."""
import argparse
import gzip
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / 'tools/ghidra/fixtures/retail-ground-support-1.27.json.gz'
HEADER = ROOT / 'games/warcraft-3/game/tests/retail_ground_support.h'


def select(report):
    rows = report['functions']['66d780']['rows']
    selected = [row for row in rows if row['move'] in (1, 4, 8, 16, 32, 64)
                and row['f280'] in (0, 32) and row['f5c'] == 0 and row['ground200'] == 0
                and row['water'] == 300 and row['force'] == 1]
    assert report['total_cases'] == 8068 and not report['mismatches']
    assert len(selected) == 48
    return dict(binary_sha256=report['binary_sha256'], ground=selected,
                deep=report['functions']['64eca0']['ones'])


def render(fixture):
    types = {1: 'foot', 4: 'horse', 8: 'hover', 16: 'float', 32: 'amph', 64: 'unbuild'}
    lines = ['/* Original66d780 ground branches +64eca0 byte cases. Literal native outputs.',
             ' * Geometry inputs are planar test surfaces; mesh/air fields are not certified. */',
             '#ifndef BZ_RETAIL_GROUND_SUPPORT_H', '#define BZ_RETAIL_GROUND_SUPPORT_H',
             'static struct { char const *type; bool prior_deep, deck; float fly; uint32_t z; }',
             'const retail_ground_support[] = {']
    for row in fixture['ground']:
        z = struct.unpack('<I', struct.pack('<f', row['z']))[0]
        lines.append('    {"%s", %s, %s, %.1ff, 0x%08xu},' %
                     (types[row['move']], str(bool(row['f280'] & 32)).lower(),
                      str(row['deck'] is not None).lower(), row['fly'], z))
    lines += ['};', 'static uint8_t const retail_support_deep[256] = {']
    for offset in range(0, 256, 32):
        lines.append('    ' + ','.join(str(int(i in fixture['deep'])) for i in range(offset, offset + 32)) + ',')
    return '\n'.join(lines + ['};', '#endif', ''])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if args.report:
        fixture = select(json.loads(args.report.read_text()))
        raw = (json.dumps(fixture, sort_keys=True, separators=(',', ':')) + '\n').encode()
        FIXTURE.write_bytes(gzip.compress(raw, mtime=0))
    else:
        fixture = json.loads(gzip.decompress(FIXTURE.read_bytes()))
    header = render(fixture)
    if args.check:
        assert HEADER.read_text() == header, 'native support header is stale'
    else:
        HEADER.write_text(header)
    print('48 native ground results and256 native deep-water classifications exported')


if __name__ == '__main__':
    main()
