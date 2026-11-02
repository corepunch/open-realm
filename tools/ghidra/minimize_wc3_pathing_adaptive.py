#!/usr/bin/env python3
"""Reduce a size-2 adaptive false-negative witness by deleting blocked cells.

Runs the original-x86 oracle for each candidate. Keeps raw reports and a
one-cell-minimal map; it does not claim a globally smallest counterexample.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--witness', type=Path, required=True)
    parser.add_argument('--directory', type=Path, required=True, help='new directory for owned experiment artifacts')
    args = parser.parse_args()
    source = json.loads(args.witness.read_text())
    if len(source['searches']) != 1 or source.get('size_input') != 1:
        parser.error('requires one size-input-1 witness')
    name = source['searches'][0]['fixture']
    current = [tuple(p) for p in source['fixture_cells'][name]]
    args.directory.mkdir(parents=True, exist_ok=False)
    oracle = Path(__file__).with_name('verify_wc3_pathing_adaptive.py')
    candidate_file = args.directory / 'candidate.json'
    history, calls = [], 0

    def probe(blocked, flat=False):
        nonlocal calls
        calls += 1
        candidate_file.write_text(json.dumps({'blocked': blocked}) + '\n')
        report = args.directory / f'case-{calls:04d}.json'
        command = [sys.executable, str(oracle), '--binary', str(args.binary), '--size-input', '1',
                   '--lane', '0', '--map-json', str(candidate_file), '--report', str(report)]
        if flat:
            command.append('--disable-promotion')
        run = subprocess.run(command, text=True, capture_output=True, timeout=30)
        if run.returncode not in (0, 1) or not report.exists():
            raise RuntimeError('oracle failed: ' + run.stdout + run.stderr)
        result = json.loads(report.read_text())
        row = result['searches'][0]
        retained = row['result'] == 0 and row['reference_reached']
        history.append(dict(call=calls, cells=len(blocked), flat=flat, retained=retained, report=report.name))
        return retained, result

    original_count = len(current)
    retained, _ = probe(current)
    if not retained:
        raise RuntimeError('input does not reproduce the target false negative')
    parts = 2
    while len(current) >= 2:
        chunk = math.ceil(len(current) / parts)
        reduced = False
        for first in range(0, len(current), chunk):
            candidate = current[:first] + current[first + chunk:]
            retained, _ = probe(candidate)
            if retained:
                current = candidate
                parts = max(2, parts - 1)
                reduced = True
                print(f'{calls} probes: retained with {len(current)} blocked cells', flush=True)
                break
        if not reduced:
            if parts >= len(current):
                break
            parts = min(len(current), parts * 2)
    retained, witness = probe(current)
    flat_retained, flat = probe(current, flat=True)
    if not retained or flat_retained or flat['searches'][0]['result'] != 1:
        raise RuntimeError('final hierarchy-versus-flat contrast did not reproduce')
    (args.directory / 'minimal.json').write_text(json.dumps({'blocked': current}, indent=2) + '\n')
    (args.directory / 'minimal-witness.json').write_text(json.dumps(witness, indent=2) + '\n')
    (args.directory / 'minimal-flat.json').write_text(json.dumps(flat, indent=2) + '\n')
    (args.directory / 'reduction.json').write_text(json.dumps(dict(original_cells=original_count,
        remaining_cells=len(current), probes=calls, minimality='no single blocked cell can be deleted while preserving the predicate',
        history=history), indent=2) + '\n')
    print(f'{original_count} -> {len(current)} blocked cells in {calls} probes; hierarchy fails, flat succeeds')


if __name__ == '__main__':
    main()
