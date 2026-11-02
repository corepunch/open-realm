#!/usr/bin/env python3
"""Verify held recovery region callbacks against two native repeats and control."""
import argparse, gzip, hashlib, json, re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes, SHA
ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT/'tools/ghidra/fixtures/retail-map342-1.27.json'
BUNDLE = ROOT/'tools/ghidra/fixtures/retail-map342-inputs-1.27.json.gz'
SOURCES = ['tools/frida/research/'+n for n in (
    'map342_probe.j', 'map342_make_map.py', 'map342_observer.js', 'stop284_capture.py')]
TESTS = ['wc3_movement.map342*', 'wc3_movement.stop284*', 'wc3_movement.scope299*',
         'wc3_movement.scope300*', 'wc3_movement.scope301*', 'wc3_order_reentry.*',
         'wc3_order_lifecycle.*', 'wc3_interrupt.*', 'wc3_api.*',
         'wc3_unit.public_speed_uses_empty_status_discovery_and_live_custom_windwalk']
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def normalized(rows):
    # Raw addresses vary with allocation. Retain native identity pairs and
    # normalize each raw pointer consistently within its scene (units from
    # completed scenes may reuse an allocation).
    pointers = {}
    result = []
    scene = None
    for row in rows:
        if 'seq' not in row and row.get('event') != 'marker': continue
        row = dict(row)
        if "scene" in row and row["scene"] != scene:
            scene = row["scene"]; pointers = {}
        for key in ('unit', 'ability'):
            if key in row:
                value = (key, row[key])
                row[key] = pointers.setdefault(value, len(pointers))
        result.append(row)
    return result

def validate_runtime(bundle, spec):
    if len(bundle) != 3: raise ValueError('two repeats and control required')
    for i, capture in enumerate(bundle):
        rows = capture['rows']; meta = rows[0]; footer = rows[-1]
        public = re.findall(r'call Preload\( "(M342 [^"\r\n]*)" \)', capture['preload'])
        if meta.get('mode') != ('control' if i == 2 else 'observe') or meta.get('sha256') != SHA or not meta.get('owned') or meta['env'] not in ('B','C') or meta['source_sha256']['map'] != spec['map_sha256']:
            raise ValueError('capture provenance differs')
        for path in SOURCES[2:]:
            if meta['source_sha256'][Path(path).name] != spec['source_sha256'][path]:
                raise ValueError('capture source differs')
        if footer.get('event') != 'preload-file' or not footer.get('complete') or footer['markers'] != 50 or public != spec['public'] or hashlib.sha256(capture['preload'].encode()).hexdigest() != footer['sha256']:
            raise ValueError('public capture differs')
        if any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
            raise ValueError('observer failed')
        native = [r for r in rows if 'seq' in r]
        ends = [r for r in rows if r.get('event') == 'trace-end']
        if i == 2:
            if native or ends or any(r.get('event') == 'module' for r in rows): raise ValueError('instrumented control')
            continue
        if [r['seq'] for r in native] != list(range(712)) or len(ends) != 1 or not ends[0]['installed'] or not ends[0]['readOnly'] or ends[0]['depth'] or ends[0]['counts'] != dict(Counter(r['event'] for r in native)):
            raise ValueError('incomplete native trace')
        if normalized(rows) != spec['chronology'] or [r['value'] for r in rows if r.get('event') == 'marker'] != public:
            raise ValueError('native chronology differs')
        filters = [r for r in rows if r.get('event') == 'marker' and 'label=filter-' in r['value']]
        if len(filters) != 16 or any(r['heldDepth'] != 1 or r['held']['counter'] != 3 for r in filters):
            raise ValueError('filter did not run inside exclusion scope')
    return dict(captures=2, controls=1, native_events=1424, public_markers=150, scenes=8)

def validate(spec):
    if spec['version'] != 1 or spec['task'] != 'MAP-04.2' or spec['binary_sha256'] != SHA or spec['engine_patterns'] != TESTS or set(spec['source_sha256']) != set(SOURCES): raise ValueError('contract differs')
    for path, sha in spec['source_sha256'].items():
        if digest(ROOT/path) != sha: raise ValueError('source differs: '+path)
    if digest(BUNDLE) != spec['bundle_sha256']: raise ValueError('capture bundle differs')
    bundle = json.loads(gzip.decompress(BUNDLE.read_bytes()))
    validate_runtime(bundle, spec)
    header = (ROOT/'games/warcraft-3/game/tests/fixtures/retail_map342_probe.h').read_text()
    literals = re.findall(r'^".*"$', header, re.M)
    if ''.join(json.loads(line) for line in literals) != (ROOT/SOURCES[0]).read_text(): raise ValueError('engine probe differs')
    return bundle

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--test-binary', type=Path, default=ROOT/'build/bin/openwarcraft3-tests')
    p.add_argument('--data', type=Path, default=ROOT/'build/tests')
    p.add_argument('--report', type=Path, required=True)
    a=p.parse_args()
    if a.report.exists(): p.error('fresh report required')
    spec=json.loads(SPEC.read_text()); bundle=validate(spec)
    original_bytes(a.binary, spec['instructions'])
    import verify_wc3_pathing_work242 as runner
    previous=runner.TESTS
    try:
        runner.TESTS=TESTS; a.report.parent.mkdir(parents=True, exist_ok=True)
        engine=runner.run_engine(a.test_binary,a.data,a.report)
    finally: runner.TESTS=previous
    result=dict(passed=True,status='verified',**validate_runtime(bundle,spec),engine=engine,
                instructions=len(spec['instructions']),binary_sha256=SHA,
                test_binary_sha256=digest(a.test_binary),
                game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'))
    a.report.write_text(json.dumps(result,indent=2)+'\n'); print(json.dumps(result))
if __name__ == '__main__': main()
