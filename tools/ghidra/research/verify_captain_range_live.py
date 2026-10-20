#!/usr/bin/env python3
"""Rebuild captain-range evidence from raw Frida repeats and public controls.

This verifies captured range/roster state, not complete engine trajectories.
The caller chooses a fresh report path; malformed/incomplete evidence fails.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
TASK = 'GROUP-03.4.6.2.1.2'
HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
FIELDS = ('authored_calls', 'task_ranges', 'arrival_ranges', 'roster',
          'reissues', 'suppression', 'markers')


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def capture_rows(path, mode):
    rows = [json.loads(line) for line in path.read_bytes().splitlines()]
    if not rows or rows[0].get('event') != 'metadata':
        raise ValueError(f'{path}: missing metadata')
    meta = rows[0]
    if sum(row.get('event') == 'metadata' for row in rows) != 1:
        raise ValueError(f'{path}: duplicated metadata')
    if meta.get('sha256') != HASH or meta.get('task') != TASK or meta.get('mode') != mode:
        raise ValueError(f'{path}: unsupported identity/mode')
    if any(row.get('type') == 'error' or row.get('event') == 'error' for row in rows):
        raise ValueError(f'{path}: capture error')
    if not any(row.get('event') == 'preload-file' and row.get('complete') is True for row in rows):
        raise ValueError(f'{path}: incomplete public markers')
    ends = [row for row in rows if row.get('event') == 'trace-end']
    if mode == 'observe' and (len(ends) != 1 or ends[0].get('installed') is not True):
        raise ValueError(f'{path}: missing installed observer completion')
    if mode == 'control' and (ends or any(row.get('event') == 'authored-range' for row in rows)):
        raise ValueError(f'{path}: control contains an observer')
    return rows


def verify(captures, expected):
    frozen = json.loads(expected.read_text())
    if frozen.get('binary_sha256') != HASH or set(frozen.get('scenes', {})) != {'f', 'g'}:
        raise ValueError('unsupported frozen evidence')
    normalizer = load_module('captain_range_normalize', ROOT / f'tools/frida/research/{TASK}_expected.py')
    control = load_module('captain_range_control', ROOT / f'tools/frida/research/{TASK}_control_compare.py')
    results = {}
    with tempfile.TemporaryDirectory(prefix='wc3-captain-range-') as work:
        for scene in ('f', 'g'):
            baseline = frozen['scenes'][scene]
            names = sorted(Path(name).name for name in baseline['captures'])
            if len(names) != 2 or not baseline.get('repeats_identical'):
                raise ValueError(f'{scene}: missing identical repeat')
            controls = list(captures.glob(f'{scene}-control-*.jsonl'))
            if len(controls) != 1:
                raise ValueError(f'{scene}: require exactly one public control')
            control_rows = capture_rows(controls[0], 'control')
            map_path = captures.parent / 'maps' / f'RS-G0346212-{scene}.w3m'
            map_hash = hashlib.sha256(map_path.read_bytes()).hexdigest()
            if control_rows[0]['source_sha256']['map'] != map_hash:
                raise ValueError(f'{scene}: control map differs')
            public = control.markers(controls[0].with_name(controls[0].stem + '-preload.txt'), 'RSG')
            if not public or not any(' label=complete' in marker for marker in public):
                raise ValueError(f'{scene}: empty/incomplete control')
            for name in names:
                path = captures / name
                rows = capture_rows(path, 'observe')
                if rows[0]['source_sha256']['map'] != map_hash:
                    raise ValueError(f'{name}: observed map differs')
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                wanted = next(value for key, value in baseline['captures'].items() if Path(key).name == name)
                if digest != wanted:
                    raise ValueError(f'{name}: original capture hash differs')
                for source, digest in rows[0]['source_sha256'].items():
                    if source == 'map':
                        continue  # Original maps are separately pinned by the corpus.
                    if hashlib.sha256((ROOT / 'tools/frida/research' / source).read_bytes()).hexdigest() != digest:
                        raise ValueError(f'{name}: observer/controller source differs: {source}')
                report = Path(work) / (path.stem + '.json')
                subprocess.run([sys.executable, str(ROOT / f'tools/frida/research/{TASK}_analyze.py'),
                                '--capture', str(path), '--report', str(report)], check=True,
                               stdout=subprocess.DEVNULL)
                actual = normalizer.normalize(json.loads(report.read_text()))
                for field in FIELDS:
                    if actual[field] != baseline[field]:
                        raise ValueError(f'{name}: frozen {field} differs')
                observed = control.markers(path.with_name(path.stem + '-preload.txt'), 'RSG')
                if observed != public:
                    raise ValueError(f'{name}: public control differs')
            results[scene] = dict(repeats=2, public_markers=len(public),
                                  authored_calls=len(baseline['authored_calls']))
        model = Path(work) / 'model.json'
        subprocess.run([sys.executable, str(ROOT / f'tools/ghidra/research/verify_{TASK}_model.py'),
                        '--expected', str(expected), '--output', str(model)], check=True,
                       stdout=subprocess.DEVNULL)
        arithmetic = json.loads(model.read_text())
        if not arithmetic['passed'] or arithmetic['authored_calls'] != 45 or arithmetic['physical_ranges'] != 45:
            raise ValueError('captured arithmetic/physical range mismatch')
    return dict(passed=True, scenes=results, authored_calls=45, physical_ranges=45)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--captures', type=Path, required=True)
    parser.add_argument('--expected', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.report.exists():
        parser.error('report must be fresh')
    try:
        result = verify(args.captures, args.expected)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        result = dict(passed=False, error=str(error))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
