#!/usr/bin/env python3
"""Initialized invalid-start public consumers, including the owner PRNG state.

Run unchanged retail165ae0 with an ordinary request as the retained kernel
history. Only the path tables are reconstructed before the invalid request.
The supplied owner PRNG words are explicit inputs, not inferred retry counts.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path

from verify_route01_2_buffers import Retail, advance, build, prepare, fw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--expected', type=Path)
    args = parser.parse_args()
    retail = Retail(args.binary, 64)
    retail.run(0x6f0040d0, 0)
    assert retail.read(0x6fd54194)[0] == fw(10)
    def kernel_state(system, coarse):
        offsets = [0xc4, 0xd0, 0x6c, 0x98] if coarse else [0x90, 0x9c, 0x40, 0x68]
        return dict(zip(['source', 'nearest', 'count', 'budget'],
                        [retail.read(system + offset)[0] for offset in offsets]))
    constructors = {'fine': kernel_state(retail.fine_system, False)}
    source, goal = (30.125, 33.875), (51.25, 42.75)
    starts = [('blocked_fine_cell', (40.5, 20.5)), ('outside_negative_x', (-3.5, 10.25)),
              ('outside_negative_y', (10.25, -.5)), ('outside_beyond_x', (70.25, 10.25))]
    rows = []
    for name, cls in itertools.product(['open', 'wall_gap'], range(4)):
        build(retail, name)
        for (label, start), enabled in itertools.product(starts, (False, True)):
            if label == 'blocked_fine_cell' and name == 'open':
                continue
            prepare(retail, cls, goal, enabled)
            retail.write(retail.owner, 0x12345678, 0)
            prior = advance(retail, source, goal, 100)
            assert 'fault' not in prior
            prepare(retail, cls, goal, enabled)
            before_rng = retail.read(retail.owner, 2)
            first = advance(retail, start, goal, 200)
            first['rng'] = retail.read(retail.owner, 2)
            second = advance(retail, start, goal, 210)
            second['rng'] = retail.read(retail.owner, 2)
            assert 'fault' not in first and 'fault' not in second
            rows.append(dict(map=name, cls=cls, label=label, adaptive_enabled=enabled,
                             source=[fw(v) for v in start], rng=before_rng,
                             prior=prior, first=first, second=second))
    assert len(rows) == 56
    retail.run(0x6f14f570, retail.system)
    constructors['coarse'] = kernel_state(retail.system, True)
    assert all(state == dict(source=0xffffffff, nearest=0xffffffff, count=0, budget=0)
               for state in constructors.values())
    payload = dict(binary_sha256=retail.digest, source=[fw(v) for v in source],
                   goal=[fw(v) for v in goal], consumer_initialization=dict(
                       entry='6f0040d0', address='6fd54194', word=fw(10)), constructors=constructors, rows=rows)
    if args.expected:
        assert json.loads(args.expected.read_text()) == payload, 'original invalid consumer fixture changed'
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(payload, sort_keys=True, separators=(',', ':')) + '\n')
    print(json.dumps(dict(rows=len(rows), sha256=hashlib.sha256(args.report.read_bytes()).hexdigest())))


if __name__ == '__main__':
    main()
