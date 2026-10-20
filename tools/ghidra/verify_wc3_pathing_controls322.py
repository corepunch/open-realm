#!/usr/bin/env python3
"""Check E2E-02.2's four repeated observer-free controls against frozen captures."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/frida'))
from control_wc3_pathfinding import HASH, markers, validate as validate_markers

FIXTURE = ROOT / 'tools/ghidra/fixtures/retail-controls322-1.27.json'
SCENES = {'blocked_goal': 305, 'remove_reorder': 311,
          'follow_fog_reacquire': 610, 'widget_lifecycle': 310}
EVENTS = {'marker', 'crowd-marker', 'target-marker', 'widget-marker', 'stock-marker'}
REFERENCES = dict(zip(SCENES, ['capture-task-blocked-goal', 'capture-task-reorder',
                             'capture-task-follow-fog-reacquire', 'capture-widget-placement']))


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def validate(spec, bundle):
    require(spec['task'] == 'E2E-02.2' and spec['binary_sha256'] == HASH, 'identity differs')
    require(set(bundle) == set(SCENES) == set(spec['scenes']), 'scene coverage differs')
    corpus = json.loads((ROOT / 'tools/ghidra/fixtures/retail-pathfinding-corpus-1.27.json').read_text())
    reference_pins = {e['id']: e['inputs']['sha256'] for e in corpus['entries'] if e['id'] in REFERENCES.values()}
    for name, count in SCENES.items():
        scene, pins = bundle[name], spec['scenes'][name]
        reference = scene['reference']
        require(pins['reference_sha256'] == reference_pins[REFERENCES[name]], 'original corpus reference changed')
        require(sha(reference.encode()) == pins['reference_sha256'], 'reference differs')
        rows = [json.loads(line) for line in reference.splitlines()]
        metadata = next(row for row in rows if row.get('event') == 'metadata')
        require(metadata['sha256'] == HASH and metadata['owned'], 'unowned reference')
        expected = [row['value'] for row in rows if row.get('event') in EVENTS]
        validate_markers(expected)
        require(len(expected) == count, 'reference marker count differs')
        provenance = scene['reference_provenance']
        require(sha(provenance.encode()) == pins['reference_provenance_sha256'], 'reference provenance differs')
        original = json.loads(provenance)
        require(original['binary']['sha256'] == HASH and original['map']['sha256'] == pins['map_sha256'], 'reference map differs')
        require(len(scene['controls']) == 2, 'two independent controls required')
        for index, control in enumerate(scene['controls']):
            p = json.loads(control['provenance'])
            require('error' not in p and p['pid'] > 0, 'failed control')
            require(p['binary_sha256'] == HASH and p['map'] == metadata['map'] and
                    p['map_sha256'] == pins['map_sha256'], 'control map differs')
            require(p['reference_sha256'] == pins['reference_sha256'] and
                    p['reference_provenance_sha256'] == pins['reference_provenance_sha256'], 'control reference differs')
            require(p['transport'] == 'Frida spawn/resume/kill only; no attach, script, or hooks', 'attached control')
            require(p['seconds'] == 130 and p['continue_at'] == 80, 'capture window differs')
            require(p['display'] in ((':94',) if index == 0 else (':98', ':99')), 'unexpected display')
            if index:
                require(p['controller_sha256'] == spec['controller_sha256'], 'repeat controller differs')
                require(p['loading_input']['pid'] == p['pid'] and p['loading_input']['returncode'] == 0, 'unaccepted owned input')
                require(p['input_helper_sha256'] == spec['input_helper_sha256'] and
                        p['input_helper_so_sha256'] == spec['input_helper_so_sha256'], 'input helper differs')
                suffix = {'\u003a98': '2', '\u003a99': '3'}[p['display']]
                require(p['wine_prefix'] == '/home/lofcz/.local/share/open-realm/wine-pathfinding-research' + suffix, 'helper environment differs')
            require(sha(control['controller'].encode()) == p['controller_sha256'], 'controller snapshot differs')
            for field in ('preload', 'provenance', 'controller'):
                require(sha(control[field].encode()) == pins['controls'][index][field], 'control input differs: ' + field)
            actual = markers(control['preload'])
            validate_markers(actual)
            require(actual == expected, 'public trajectory or lifecycle differs: ' + name)
    return dict(passed=True, status='verified', task='E2E-02.2', scenes=4,
                controls=8, repeated_scenes=4, reference_markers=1536, control_markers=3072)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    require(not args.report.exists(), 'fresh report required')
    require(sha(args.binary.read_bytes()) == HASH, 'binary differs')
    spec = json.loads(FIXTURE.read_text())
    packed = FIXTURE.with_suffix('.json.gz').read_bytes()
    require(sha(packed) == spec['bundle_sha256'], 'bundle differs')
    require(sha((ROOT / 'tools/frida/control_wc3_pathfinding.py').read_bytes()) == spec['controller_sha256'], 'current controller differs')
    result = validate(spec, json.loads(gzip.decompress(packed)))
    result.update(binary_sha256=HASH, fixture_sha256=sha(FIXTURE.read_bytes()), limits=spec['limits'])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
