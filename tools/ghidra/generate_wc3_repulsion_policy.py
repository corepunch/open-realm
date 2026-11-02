#!/usr/bin/env python3
"""Export the frozen live policy matrix without recomputing retail outcomes."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'tools/ghidra/fixtures/research/SEP-01.3-expected.json'
TARGET = ROOT / 'games/warcraft-3/game/tests/retail_repulsion_policy.h'


def render():
    data = json.loads(SOURCE.read_text())
    lines = ['/* Generated from frozen SEP-01.3 live observations. Do not edit. */',
             '#ifndef BZ_RETAIL_REPULSION_POLICY_H', '#define BZ_RETAIL_REPULSION_POLICY_H',
             'static struct {',
             '    struct { int owner, enabled, selector, group, rank; char const *type; float collision; } units[2];',
             '    bool eligible[2];',
             '} const retail_repulsion_policy[] = {']
    for case in data['cases']:
        units, eligible = [], []
        for unit in case['units']:
            authored = unit['authored']
            values = [unit['owner'], authored.get('urpo', 0), authored.get('urpp', 0),
                      authored.get('urpg', 0), authored.get('urpr', 0)]
            units.append('{%s,%s,%s}' % (','.join(map(str, values)),
                         json.dumps(authored.get('umvt', 'foot')), authored.get('ucol', 8)))
            decisions = next(iter(unit['decisions'].values()), {})
            # This captured matrix lists the initial policy's reason first.
            # Do not interpret the aggregate counts as a chronological trace.
            eligible.append('true' if next(iter(decisions), '') == 'eligible' else 'false')
        lines.append('    {{%s},{%s}}, /* %s */' % (','.join(units), ','.join(eligible), case['label']))
    lines.append('};')
    valid = json.loads((ROOT / 'tools/ghidra/fixtures/retail-repulsion-inert-producers-1.27.json').read_text())
    for name, fields in [('pairs', ('source', 'candidate', 'prior', 'ownerBefore', 'ownerAfter', 'vector')),
                         ('tails', ('vector', 'packed', 'vectorAfter', 'packedAfter'))]:
        lines.append('static uint32_t const retail_inert_%s[][ %d ] = {' %
                     (name, 12 if name == 'pairs' else 6))
        for row in valid['pairs'] if name == 'pairs' else data['oracle']['inert_tails']:
            words = []
            for field in fields:
                value = row[field]
                words.extend(value if isinstance(value, list) else [value])
            lines.append('    {%s},' % ','.join('0x%08xu' % word for word in words))
        lines.append('};')
    lines.extend(['#endif', ''])
    return '\n'.join(lines)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    output = render()
    if args.check:
        if TARGET.read_text() != output:
            raise SystemExit('repulsion policy export differs; rerun generator')
    else:
        TARGET.write_text(output)
