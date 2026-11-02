#!/usr/bin/env python3
"""Execute complete retail66fc50 with the original rawcode cache lookup.

The supplied cache and unit fields are inputs, not a public constructor witness.
No instruction or callee is replaced. The separate immobile Frida map establishes
whether zero-speed public units reach this producer and retain a repulsor.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
from sep_research_oracle import Oracle


def verify(binary):
    oracle = Oracle(binary)
    unit, keyptr, bucket, record = [oracle.system + n for n in (0x100, 0x1000, 0x1100, 0x1200)]
    key = 0x6f6f6668
    oracle.write(keyptr, key)
    codehash, _ = oracle.call(0x6f198420, keyptr)
    oracle.write(bucket, 0, 0, record)
    oracle.write(record, codehash)
    oracle.write(record + 0x14, key)
    oracle.write(0x6fd709f4, bucket)
    oracle.write(0x6fd709fc, 0)
    oracle.write(unit, 0x6fb77eb0)
    oracle.write(unit + 0x30, key)
    cases = []
    values = itertools.product((0, 1, 2, -1, -2147483648, 2147483647),
                               (0, 0x40000000), (-1, 0, 1), (0, 0x100000),
                               (-1, 0, 1), (0, 0x200000))
    for repulse, channel, depth, latch, suspend, paused in values:
        oracle.write(record + 0x224, repulse)
        oracle.write(unit + 0x20, channel)
        oracle.write(unit + 0x198, depth)
        oracle.write(unit + 0x5c, latch | paused)
        oracle.write(unit + 0x54, suspend)
        enabled, esp = oracle.call(0x6f66fc50, unit)
        assert esp == oracle.stack + 4
        assert enabled == int(bool(repulse) and not channel and depth <= 0
                              and not latch and suspend <= 0 and not paused)
        cases.append([repulse, channel, depth, latch, suspend, paused, enabled])
    return dict(binary_sha256=oracle.sha, cases=cases,
                scope='Complete66fc50 and native rawcode lookup; supplied unit/cache inputs. '
                      'Returns boolean1, not the raw authored integer. Public reachability is separate.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.binary)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(passed=True, cases=len(result['cases']),
                         sha256=hashlib.sha256(args.report.read_bytes()).hexdigest())))
