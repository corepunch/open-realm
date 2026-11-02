#!/usr/bin/env python3
"""Verify literal target-pursuit snapshots, public controls and production replays."""
import argparse
import base64
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from verify_wc3_pathing_target_normalize218 import original_bytes
from retail_capture_source import matches_capture_source

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / 'tools/ghidra/fixtures/retail-target356-1.27.json'
BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-target356-inputs-1.27.json.gz'
SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
COUNTS = [617, 616, 617, 413, 287, 617, 617, 616, 617]
TESTS = ['wc3_movement.target356*']
ENGINE_END = 'TEST(wc3_movement, target356_public_stepped_target_complete_delay_journey) {target356_journey(true);}'


def digest(value):
    return hashlib.sha256(value).hexdigest()


def public(preload):
    return re.findall(r'call Preload\( "(T021 [^"\r\n]*)" \)', preload)


def projection(run):
    return {str(key): value['rows'] for key, value in run['scenes'].items()}


def runtime(bundle, spec):
    runs = bundle['source_runs']
    if len(runs) != 2 or set(runs[0]['scenes']) != set(map(str, range(9))):
        raise ValueError('complete repeated native scene inventory required')
    if projection(runs[0]) != projection(runs[1]):
        raise ValueError('literal original and fresh native decisions differ')
    for run in runs:
        meta = run['metadata']
        if meta.get('sha256') != SHA or meta.get('mode') != 'observe' or not meta.get('owned'):
            raise ValueError('observed owned matching retail process required')
        if meta.get('remote') not in ('127.0.0.1:27049', '127.0.0.1:27050'):
            raise ValueError('isolated B/C capture required')
        if meta.get('source_sha256', {}).get('map') not in spec['map_hashes']:
            raise ValueError('map provenance differs')
        for index, count in enumerate(COUNTS):
            rows = run['scenes'][str(index)]['rows']
            if len(rows) != count or any(len(row) != 23 for row in rows):
                raise ValueError('incomplete native owner rows')
            if any(type(word) is not int or not 0 <= word <= 0xffffffff for row in rows for word in row):
                raise ValueError('native words must be literal uint32 values')
    if digest(json.dumps(projection(runs[0]), separators=(',', ':')).encode()) != spec['projection_sha256']:
        raise ValueError('frozen native decisions differ')
    if any(run['sha256'] != case['sha256'] for run, case in zip(runs, bundle['captures'])):
        raise ValueError('native capture identity differs')
    expected = bundle['public']
    if len(expected) != 2085 or not any(' label=complete' in item for item in expected):
        raise ValueError('complete public reference required')
    for case in bundle['captures']:
        if public(case['preload']) != expected or case['summary']['public'] != expected:
            raise ValueError('public markers differ')
        meta = case['summary']['metadata']
        if (meta.get('sha256') != SHA or not meta.get('owned') or
                meta.get('remote') not in ('127.0.0.1:27049', '127.0.0.1:27050')):
            raise ValueError('capture provenance differs')
        if case['summary'].get('failed') or not case['summary'].get('complete'):
            raise ValueError('failed/incomplete runtime capture')
        ends = case['summary'].get('trace_end', [])
        if meta['mode'] == 'control':
            if case['summary'].get('observer') or ends:
                raise ValueError('control was instrumented')
        elif (len(ends) != 1 or not ends[0].get('installed') or
              any(value for key, value in ends[0].get('counts', {}).items() if key.endswith('-dropped'))):
            raise ValueError('incomplete or dropping observer')
    if [case['summary']['metadata']['mode'] for case in bundle['captures']] != ['observe', 'observe', 'control']:
        raise ValueError('original/fresh observed and unhooked control required')
    return dict(captures=3, scenes=9, public_markers=2085, native_rows=sum(COUNTS), engine_rows=1233)


def validate(spec):
    if spec.get('version') != 1 or spec.get('task') != 'TARGET-02.1' or spec.get('game_sha256') != SHA:
        raise ValueError('wrong target contract')
    if spec.get('engine_tests') != TESTS or len(spec.get('instructions', {})) != 1353:
        raise ValueError('incomplete instruction/engine inventory')
    for path, expected in spec['pins'].items():
        if not matches_capture_source(ROOT, path, expected):
            raise ValueError('source changed ' + path)
    if digest(BUNDLE.read_bytes()) != spec['bundle_sha256']:
        raise ValueError('frozen inputs changed')
    source = (ROOT / 'games/warcraft-3/game/tests/t_movement.c').read_text()
    start = source.index('#include \"fixtures/retail_target356.h\"')
    end = source.index(ENGINE_END, start) + len(ENGINE_END)
    if spec.get('engine_source_end') != ENGINE_END or digest(source[start:end].encode()) != spec['engine_source_block_sha256']:
        raise ValueError('production regression source changed')
    bundle = json.loads(gzip.decompress(BUNDLE.read_bytes()))
    runtime(bundle, spec)
    # C expectations retain raw words. They must be identical to their native
    # source rows, including data that a readable decimal view would round.
    header = (ROOT / 'games/warcraft-3/game/tests/fixtures/retail_target356.h').read_text()
    for name, scene in [('move', '2'), ('step', '1')]:
        match = re.search(r'target356_' + name + r'\[\]\[23\]=\{(.*?)\n\};', header, re.S)
        if not match:
            raise ValueError('missing production fixture ' + name)
        rows = [[int(word) for word in row.split(',')] for row in re.findall(r'\{([0-9,]+)\}', match[1])]
        if rows != bundle['source_runs'][0]['scenes'][scene]['rows']:
            raise ValueError('production fixture differs from original native words')
    return bundle


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--test-binary', type=Path, default=ROOT / 'build/bin/openwarcraft3-tests')
    ap.add_argument('--data', type=Path, default=ROOT / 'build/tests')
    args = ap.parse_args()
    if args.report.exists():
        ap.error('fresh report required')
    spec = json.loads(SPEC.read_text())
    bundle = validate(spec)
    original_bytes(args.binary, spec['instructions'])
    with tempfile.TemporaryDirectory(prefix='target356-map-') as temp:
        base = Path(temp) / 'base.w3m'
        base.write_bytes(base64.b64decode(bundle['base_map']))
        if digest(base.read_bytes()) != spec['map_hashes'][0]:
            raise ValueError('original map differs')
        output = Path(temp) / 'Target356.w3m'
        subprocess.run([sys.executable, str(ROOT / 'tools/frida/research/target356_make_map.py'),
                        '--base', str(base), '--output', str(output)], check=True, stdout=subprocess.DEVNULL)
        if digest(output.read_bytes()) != spec['map_hashes'][1]:
            raise ValueError('render-only shadow map repair differs')
    import verify_wc3_pathing_work242 as runner
    prior = runner.TESTS
    try:
        runner.TESTS = TESTS
        engine = runner.run_engine(args.test_binary, args.data, args.report)
    finally:
        runner.TESTS = prior
    report = dict(passed=True, status='verified', binary_sha256=SHA,
                  instructions=len(spec['instructions']), **runtime(bundle, spec), engine=engine)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
