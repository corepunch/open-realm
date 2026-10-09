#!/usr/bin/env python3
"""Export original MAP-04.1 snapshots; never calculate expected cell policy."""
import argparse
import gzip
import json
from pathlib import Path

FINE = ('before', 'after_self_inc', 'after_target_inc', 'search_entry',
        'after_target_dec', 'after_self_dec')
COARSE = ('after_clear_self', 'after_clear_target', 'search_entry',
          'after_rebuild_self', 'after_rebuild_target')


def header(stages, frozen):
    if len(stages['cases']) != 45 or len(frozen['fine_cases']) != 45:
        raise ValueError('incomplete exclusion matrix')
    rows = []
    roles = {'A': 0, 'B': 1, 'C': 2, None: -1}
    for stage, fine, coarse in zip(stages['cases'], frozen['fine_cases'], frozen['coarse_cases']):
        for key in ('self', 'target', 'occupancy', 'link_order'):
            if stage[key] != fine[key] or stage[key] != coarse[key]:
                raise ValueError('fixture ordering differs')
        flags = [int(fine['counter_states']['before'][k], 16) for k in 'ABC']
        counters = [[stage['fine_counters'][label][k] for k in 'ABC'] for label in FINE]
        cells = [[v for row in stage['fine_cells'][label] for v in row] for label in FINE]
        classes = [stage['coarse_cells'][label] for label in COARSE]
        if any(len(row) != 324 for row in cells) or any(len(row) != 1360 for row in classes):
            raise ValueError('incomplete cell snapshot')
        # The older frozen report stores complete changed cells and counters.
        # Require the newly exported full grids to retain all of that evidence.
        baseline = cells[0]
        for label, values in zip(FINE[1:], cells[1:]):
            diff = [[12+i%18, 12+i//18, old, new]
                    for i, (old, new) in enumerate(zip(baseline, values)) if old != new]
            if diff != fine['changed_cells_vs_before'][label]:
                raise ValueError('native fine window differs from frozen changes')
        values = [roles[fine['self']], roles[fine['target']],
                  [roles[k] for k in fine['link_order']], flags, counters, cells, classes,
                  fine['route_count'], fine['fine_index'], fine['route_words'],
                  coarse['coarse_count'], coarse['coarse_words']]
        rows.append(json.dumps(values, separators=(',', ':')).replace('[', '{').replace(']', '}'))
    return ('/* Generated from original MAP-04.1 requests by export_exclusion_stages.py.\n'
            ' * Supplied rectangles, category06 and outer counters are oracle fixtures,\n'
            ' * not claims about public producers. All cell words are original outputs. */\n'
            'typedef struct {\n'
            '    int self,target;\n'
            '    unsigned order[3],flags[3],counters[6][3];\n'
            '    uint8_t fine[6][324],coarse[5][1360];\n'
            '    unsigned fine_count,fine_index,fine_words[128],coarse_count,coarse_words[128];\n'
            '} retailExclusionCase_t;\n'
            'static retailExclusionCase_t const retail_exclusion_cases[45]={\n'
            + ',\n'.join(rows) + '\n};\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stages', type=Path, required=True)
    parser.add_argument('--frozen', type=Path, required=True)
    parser.add_argument('--header', type=Path, required=True)
    args = parser.parse_args()
    raw = args.stages.read_bytes()
    stages = json.loads(gzip.decompress(raw) if args.stages.suffix == '.gz' else raw)
    args.header.write_text(header(stages, json.loads(args.frozen.read_text())))


if __name__ == '__main__':
    main()
