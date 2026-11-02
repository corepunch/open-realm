#!/usr/bin/env python3
"""Verify every natural36 pursuit owner decision and its production replay."""
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
SPEC = ROOT / 'tools/ghidra/fixtures/retail-crowd361-1.27.json'
BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-crowd361-inputs-1.27.json.gz'
SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
TESTS = ['wc3_movement.crowd361*']


def digest(value):
    return hashlib.sha256(value).hexdigest()


def public(preload):
    return re.findall(r'call Preload\( "(T021 [^"\r\n]*)" \)', preload)


def runtime(bundle, spec):
    runs = bundle['source_runs']
    if len(runs) != 3 or any(run['rows'] != runs[0]['rows'] for run in runs[1:]):
        raise ValueError('two original repeats and one identical fresh owner stream required')
    rows = runs[0]['rows']
    if len(rows) != 22772 or any(len(row) != 22 for row in rows):
        raise ValueError('complete natural36 owner frontier required')
    if any(type(word) is not int or not 0 <= word <= 0xffffffff for row in rows for word in row):
        raise ValueError('literal uint32 native words required')
    if set(row[1] for row in rows) != set(range(36)):
        raise ValueError('all36 independently scheduled followers required')
    if digest(json.dumps(rows, separators=(',', ':')).encode()) != spec['projection_sha256']:
        raise ValueError('native owner decisions changed')
    captures = bundle['captures']
    if len(captures) != 4 or [c['summary']['metadata'].get('mode') for c in captures] != ['observe']*3+['control']:
        raise ValueError('original repeats, fresh observed and unhooked control required')
    reference = bundle['public']
    if len(reference) != 544 or not any(' label=complete' in item for item in reference):
        raise ValueError('complete public journey required')
    for index, case in enumerate(captures):
        summary = case['summary']; meta = summary['metadata']
        if (meta.get('sha256') != SHA or not meta.get('owned') or
                meta.get('remote') not in ('127.0.0.1:27049', '127.0.0.1:27050') or
                meta.get('source_sha256', {}).get('map') not in spec['map_hashes']):
            raise ValueError('owned isolated matching retail provenance required')
        if summary.get('failed') or not summary.get('complete'):
            raise ValueError('failed or incomplete capture')
        if summary['public'] != reference or public(case['preload']) != reference:
            raise ValueError('public markers differ')
        if index < 3 and runs[index]['sha256'] != case['sha256']:
            raise ValueError('native capture identity differs')
        ends = summary.get('trace_end', [])
        if index == 3:
            if summary.get('observer') or ends:
                raise ValueError('control was instrumented')
        elif (len(ends) != 1 or not ends[0].get('installed') or
              any(value for key, value in ends[0].get('counts', {}).items() if key.endswith('-dropped'))):
            raise ValueError('incomplete or dropping observer')
    return dict(captures=4, followers=36, public_markers=544, native_rows=22772)


def validate(spec):
    if spec.get('version') != 1 or spec.get('task') != 'TARGET-02.1' or spec.get('game_sha256') != SHA:
        raise ValueError('wrong crowd contract')
    if spec.get('engine_tests') != TESTS or len(spec.get('instructions', {})) < 1353:
        raise ValueError('incomplete instruction or engine inventory')
    for path, expected in spec['pins'].items():
        if not matches_capture_source(ROOT, path, expected):
            raise ValueError('source changed '+path)
    if digest(BUNDLE.read_bytes()) != spec['bundle_sha256']:
        raise ValueError('frozen inputs changed')
    bundle = json.loads(gzip.decompress(BUNDLE.read_bytes()))
    runtime(bundle, spec)
    header = (ROOT / 'games/warcraft-3/game/tests/fixtures/retail_crowd361.h').read_text()
    match = re.search(r'crowd361_rows\[\]\[22\]=\{(.*?)\n\};', header, re.S)
    if not match:
        raise ValueError('missing production owner rows')
    rows = [[int(word) for word in row.split(',')] for row in re.findall(r'\{([0-9,]+)\}', match[1])]
    if rows != bundle['source_runs'][0]['rows']:
        raise ValueError('production fixture differs from native owner rows')
    return bundle


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--test-binary', type=Path, default=ROOT/'build/bin/openwarcraft3-tests')
    ap.add_argument('--data', type=Path, default=ROOT/'build/tests')
    args = ap.parse_args()
    if args.report.exists():
        ap.error('fresh report required')
    spec = json.loads(SPEC.read_text()); bundle = validate(spec)
    original_bytes(args.binary, spec['instructions'])
    with tempfile.TemporaryDirectory(prefix='crowd361-map-') as temp:
        base = Path(temp)/'base.w3m'; base.write_bytes(base64.b64decode(bundle['base_map']))
        if digest(base.read_bytes()) != spec['map_hashes'][0]:
            raise ValueError('original map differs')
        output = Path(temp)/'Crowd361.w3m'
        subprocess.run([sys.executable, str(ROOT/'tools/frida/research/target356_make_map.py'),
                        '--base',str(base),'--output',str(output)],check=True,stdout=subprocess.DEVNULL)
        if digest(output.read_bytes()) != spec['map_hashes'][1]:
            raise ValueError('render-only shadow repair differs')
    import verify_wc3_pathing_work242 as runner
    prior = runner.TESTS
    try:
        runner.TESTS = TESTS
        engine = runner.run_engine(args.test_binary,args.data,args.report)
    finally:
        runner.TESTS = prior
    report = dict(passed=True,status='verified',binary_sha256=SHA,
                  instructions=len(spec['instructions']),**runtime(bundle,spec),engine=engine)
    args.report.write_text(json.dumps(report,indent=2)+'\n'); print(json.dumps(report))


if __name__ == '__main__':
    main()
