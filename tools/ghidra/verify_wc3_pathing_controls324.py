#!/usr/bin/env python3
"""Verify crowd/five-mode observer controls and their production engine trajectories."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / 'tools/ghidra/fixtures'
SPEC = FIXTURES / 'retail-controls324-1.27.json'
sys.path.insert(0, str(ROOT / 'tools/frida/research'))
from controls324_capture import HASH, markers

SCENES = {'crowd': 1515, 'ground': 1616, 'foot': 257, 'fly': 257,
          'amph': 257, 'hover': 257, 'float': 257}
TESTS = ['wc3_repulsion_crowds.*',
         'wc3_movement.speed314_exceptional_public_speed_keeps_motion_and_restoration']
SOURCES = ['tools/frida/research/controls324_capture.py',
           'tools/ghidra/fixtures/retail-separation-crowd-inputs-1.27.json.gz',
           'tools/ghidra/fixtures/retail-separation-crowds-1.27.json.gz',
           'tools/ghidra/fixtures/retail-speed314-1.27.json',
           'tools/ghidra/fixtures/retail-speed314-1.27.json.gz',
           'games/warcraft-3/game/tests/retail_separation_crowds.h',
           'games/warcraft-3/game/tests/fixtures/retail_speed314.h']


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def require(value, message):
    if not value:
        raise ValueError(message)


def references():
    """Derive expectations from pre-existing retail fixtures, never engine output."""
    crowd = json.loads(gzip.decompress((FIXTURES / 'retail-separation-crowd-inputs-1.27.json.gz').read_bytes()))
    speed = json.loads((FIXTURES / 'retail-speed314-1.27.json').read_text())
    out = {}
    for name in ('crowd', 'ground'):
        meta = json.loads(crowd['files'][name + '/map.json'])
        public = markers(crowd['files'][name + '/control/preload.txt'], 'PATHSEP')
        for index in (1, 2):
            require(markers(crowd['files'][f'{name}/observe-{index}/preload.txt'], 'PATHSEP') == public,
                    'original observed crowd control differs')
        out[name] = dict(map='Maps/' + meta['name'] + '.w3m', map_sha256=meta['sha256'],
                         preload='sepres-' + name + '.txt', prefix='PATHSEP', public=public)
    for name, case in speed['cases'].items():
        out[name] = dict(map='Maps/Speed314-' + name + '-all.w3m', map_sha256=case['map_sha256'],
                         preload='speed314.txt', prefix='S314 ', public=case['public'])
    return out


def validate(spec, bundle):
    require(spec['task'] == 'E2E-02.3' and spec['binary_sha256'] == HASH, 'wrong identity')
    require(set(spec['scenes']) == set(bundle['controls']) == set(SCENES), 'missing scene')
    require(set(spec['source_sha256']) == set(SOURCES), 'missing original source pins')
    for path, digest in spec['source_sha256'].items():
        require(sha((ROOT / path).read_bytes()) == digest, 'source changed: ' + path)
    expected = references()
    require(json.loads(bundle['reference']) == expected, 'original reference changed')
    require(sha(bundle['reference'].encode()) == spec['reference_sha256'], 'reference bytes differ')
    for name, count in SCENES.items():
        control, scene = bundle['controls'][name], expected[name]
        for field in ('provenance', 'preload', 'controller'):
            require(sha(control[field].encode()) == spec['scenes'][name][field], 'control bytes differ: ' + field)
        provenance = json.loads(control['provenance'])
        require(provenance.get('task') == 'E2E-02.3' and provenance.get('scene') == name and
                provenance.get('mode') == 'control' and provenance.get('pid', 0) > 0 and
                'error' not in provenance and not provenance.get('already_exited'), 'failed control')
        require(provenance['transport'] == 'Frida spawn/resume/kill only; no attach, script, or hooks', 'attached control')
        require(provenance['binary_sha256'] == HASH and provenance['map'] == scene['map'] and
                provenance['map_sha256'] == scene['map_sha256'], 'different map/binary')
        require(provenance['reference_sha256'] == spec['reference_sha256'] and
                provenance['controller_sha256'] == spec['source_sha256']['tools/frida/research/controls324_capture.py'] and
                sha(control['controller'].encode()) == provenance['controller_sha256'], 'different capture producer')
        index = {'B': 2, 'C': 3}.get(provenance['env'])
        require(index is not None, 'unowned environment')
        require(provenance['display'] == ':' + str(96 + index) and
                provenance['remote'] == '127.0.0.1:' + str(27047 + index) and
                provenance['wine_prefix'] == '/home/lofcz/.local/share/open-realm/wine-pathfinding-research' + str(index),
                'environment differs')
        require(provenance['seconds'] == 180 and provenance['continue_at'] == 30 and
                0 < provenance['elapsed'] < 180, 'unbounded/incomplete control')
        require(provenance['loading_input'] and all(row['pid'] == provenance['pid'] and row['rc'] == 0
                for row in provenance['loading_input']), 'unaccepted owned loading input')
        require(provenance['helper_sha256'] == spec['helper_sha256'] and
                provenance['helper_so_sha256'] == spec['helper_so_sha256'], 'helper differs')
        actual = markers(control['preload'], scene['prefix'])
        require(len(actual) == count and actual == scene['public'] and ' label=complete' in actual[-1],
                'incomplete or different trajectory/timing: ' + name)
        require(provenance['preload_sha256'] == sha(control['preload'].encode()) and
                provenance['markers'] == count and provenance['complete'] and provenance['matches'], 'false completion')
    return dict(passed=True, status='verified', task='E2E-02.3', scenes=7,
                repeated_controls=7, repeated_markers=sum(SCENES.values()), observed_repeats=14)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--test-binary', type=Path, default=ROOT / 'build/bin/openwarcraft3-tests')
    parser.add_argument('--data', type=Path, default=ROOT / 'build/tests')
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    require(not args.report.exists(), 'fresh report required')
    require(sha(args.binary.read_bytes()) == HASH, 'unsupported binary')
    spec = json.loads(SPEC.read_text())
    packed = SPEC.with_suffix('.json.gz').read_bytes()
    require(sha(packed) == spec['bundle_sha256'], 'bundle differs')
    result = validate(spec, json.loads(gzip.decompress(packed)))
    import verify_wc3_pathing_crowds as crowds
    import verify_wc3_pathing_speed314 as speed
    import verify_wc3_pathing_work242 as engine
    speed_spec = json.loads(speed.SPEC.read_text())
    result['original_crowds'] = crowds.check(args.binary)
    result['original_modes'] = speed.validate(speed_spec, json.loads(gzip.decompress(speed.BUNDLE.read_bytes())))
    speed.original_bytes(args.binary, speed_spec['instructions'])
    engine.TESTS = TESTS
    args.report.parent.mkdir(parents=True, exist_ok=True)
    result['engine'] = engine.run_engine(args.test_binary, args.data, args.report)
    result.update(binary_sha256=HASH, fixture_sha256=sha(SPEC.read_bytes()),
                  test_binary_sha256=sha(args.test_binary.read_bytes()),
                  game_library_sha256=sha((args.test_binary.parent.parent / 'lib/libgame-wc3-test.so').read_bytes()),
                  limits=spec['limits'])
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
