#!/usr/bin/env python3
"""Fresh original support oracle, literal ground fixture and live refresh-state audit."""
import argparse
import gzip
import hashlib
import json
import subprocess
import sys
from pathlib import Path

from research.export_support_ground import select, render, HEADER


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    args.report.parent.mkdir(parents=True, exist_ok=True)
    native_path = args.report.with_name(args.report.stem + '-native.json')
    subprocess.run([sys.executable, str(here / 'research/verify_map022_support.py'),
                    '--binary', str(args.binary), '--report', str(native_path)], check=True)
    native = json.loads(native_path.read_text())
    frozen_raw = gzip.decompress((here / 'fixtures/retail-ground-support-1.27.json.gz').read_bytes())
    frozen = json.loads(frozen_raw)
    assert select(native) == frozen, 'native ground/deep results changed'
    assert render(frozen) == HEADER.read_text(), 'engine expected header changed'
    live_raw = gzip.decompress((here / 'fixtures/retail-support-refresh-live-1.27.json.gz').read_bytes())
    live = json.loads(live_raw)
    assert live['metadata']['sha256'] == native['binary_sha256']
    assert live['metadata']['mode'] == 'observe'
    assert len(live['markers']) == 193
    assert live['markers'] == live['control_markers']
    assert 'label=complete' in live['markers'][-1]
    counts = dict(queried=0, cached=0)
    for row in live['refreshes']:
        before, after, out = row['before'], row['after'], row['out']
        query = bool(row['force'] or abs(out[0] - before['cache'][0]) >= .01
                     or abs(out[1] - before['cache'][1]) >= .01)
        assert query == bool(row['calls']), 'native refresh query gate differs'
        assert after['cache'] == out, 'native refresh cache publication differs'
        if query:
            assert bool(after['flags280'] & 32) == bool(row['deepWater']['result'])
            assert bool(after['flags280'] & 2) == bool(row['getterBridge'])
        else:
            assert out[2] == before['cache'][2], 'no-query refresh changes height'
            assert before['flags280'] == after['flags280'], 'no-query refresh changes flags'
        counts['queried' if query else 'cached'] += 1
    saved = json.loads((here / 'fixtures/retail-ground-support-ghidra-1.27.json').read_text())
    assert saved['program'] == 'game.dll' and not saved['unsaved']
    assert len(saved['rows']) == 3
    assert all('[Payoff145/MAP-02.2]' in row['comment'] for row in saved['rows'])
    report = dict(status='verified', passed=True, binary_sha256=native['binary_sha256'],
                  checks=dict(original_support_cases=native['total_cases'], ground_results=48,
                              deep_bytes=256, live_refreshes=len(live['refreshes']),
                              live_counts=counts, control_markers=193, saved_ghidra_rows=3),
                  fixture_sha256=hashlib.sha256(frozen_raw).hexdigest(),
                  live_sha256=hashlib.sha256(live_raw).hexdigest(),
                  exclusions=['Deck raycast/mesh geometry', 'Alternate flyer-height field',
                              'Exact terrain/water sampling', 'Structure multi-sample producers',
                              'This Python audit does not execute the engine; wc3_support does'])
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
