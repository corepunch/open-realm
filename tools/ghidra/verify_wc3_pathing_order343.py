#!/usr/bin/env python3
"""Verify public decay task cancellation, original instruction guards and engine lifecycle."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import re
from verify_wc3_pathing_target_normalize218 import SHA, original_bytes

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT/'tools/ghidra/fixtures/retail-order343-1.27.json'
BUNDLE = ROOT/'tools/ghidra/fixtures/retail-order343-inputs-1.27.json.gz'
SOURCES = ['tools/frida/research/'+n for n in
           ('order343_probe.j', 'order343_make_map.py', 'order343_observer.js', 'stop284_capture.py')]
TESTS = ['wc3_unit.*', 'wc3_save.rejects_prior_save_versions', 'wc3_save.rejects_mismatched_entity_layout']

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def public(text):
    return re.findall(r'call Preload\( "(O343 [^"\r\n]*)" \)', text)

def native(rows):
    return [r for r in rows if 'seq' in r and r['event'] != 'module']

def validate_runtime(bundle, spec):
    if len(bundle) != 3:
        raise ValueError('two observed captures and control required')
    for i, capture in enumerate(bundle):
        rows = capture['rows']; meta = rows[0]; footer = rows[-1]
        if (meta.get('mode') != ('control' if i == 2 else 'observe') or
            meta.get('sha256') != SHA or not meta.get('owned') or
            (meta.get('env'), meta.get('display'), meta.get('remote')) not in
            (('B', ':98', '127.0.0.1:27049'), ('C', ':99', '127.0.0.1:27050')) or
            meta['source_sha256']['map'] != spec['map_sha256']):
            raise ValueError('capture provenance differs')
        for path in SOURCES[2:]:
            if meta['source_sha256'][Path(path).name] != spec['source_sha256'][path]:
                raise ValueError('captured source differs')
        if (footer.get('event') != 'preload-file' or not footer.get('complete') or
            footer['markers'] != len(spec['public']) or public(capture['preload']) != spec['public'] or
            hashlib.sha256(capture['preload'].encode()).hexdigest() != footer['sha256']):
            raise ValueError('public capture differs')
        if any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
            raise ValueError('capture failed')
        events = [r for r in rows if 'seq' in r]
        ends = [r for r in rows if r.get('event') == 'trace-end']
        if i == 2:
            if events or ends or any(r.get('event') == 'module' for r in rows):
                raise ValueError('instrumented control')
        else:
            if ([r['seq'] for r in events] != list(range(1, len(events)+1)) or
                len(ends) != 1 or not ends[0]['readOnly'] or
                ends[0]['counts'] != dict(Counter(r['event'] for r in events)) or
                native(rows) != spec['native']):
                raise ValueError('native trace differs')
            markers = [r['value'] for r in events if r['event'] == 'marker']
            # The JASS callback emits one final sample after PreloadGenEnd.
            if markers[:-1] != spec['public'] or 'tick=180 label=sample' not in markers[-1]:
                raise ValueError('observer/public marker mismatch')
    rows = spec['native']
    # Identify units by original registry identity, never a process-local pointer.
    identities = [r['unit']['identity'] for r in rows if r['event'] == 'restore-scale-enter'
                  and r['caller'] == '0x68ad5d']
    if len(identities) != 9 or len({tuple(x) for x in identities}) != 9:
        raise ValueError('missing unit identities')
    def own(index, event):
        return [r for r in rows if r['event'] == event and r.get('unit', {}).get('identity') == identities[index]]
    for index, ticks in ((0,[51]), (3,[80]), (4,[51,100]), (6,[51]), (7,[40,60])):
        if [r['tick'] for r in own(index,'bones-enter')] != ticks:
            raise ValueError('bone restart lifecycle changed')
    if own(5,'flesh-enter') or own(5,'wait-enter'):
        raise ValueError('living unit accepted decay')
    for index in (1,2,3,4,8):
        cancels = own(index,'restore-scale-enter')
        if not any(r['caller']=='0x67e14e' and r['unit']['scaleFlags'] & 64 for r in cancels):
            raise ValueError('missing scaled cancellation')
    for index in (3,4,7):
        waits = [r for r in own(index,'wait-enter') if r['caller']=='0x69c1ec']
        if not waits or any(r['delay'] != 0x40800000 or not r['scale'] for r in waits):
            raise ValueError('bone duration/scale changed')
    samples = [dict(re.findall(r'(\w+)=([^ ]+)', marker)) for marker in spec['public']
               if 'label=sample' in marker]
    for index, removed in enumerate((92,41,61,121,141,None,92,101,61)):
        for sample in samples:
            expected = 0 if removed is not None and int(sample['tick']) >= removed else 1751543663
            if int(sample['u'+str(index)]) != expected:
                raise ValueError('public corpse lifetime changed')
    return {'captures':2,'controls':1,'units':9,'native_records':2*len(rows),
            'public_markers':3*len(spec['public'])}

def validate(spec):
    if spec['game_sha256'] != SHA or set(spec['source_sha256']) != set(SOURCES):
        raise ValueError('evidence contract changed')
    for path, expected in spec['source_sha256'].items():
        if digest(ROOT/path) != expected:
            raise ValueError('source changed: '+path)
    if digest(BUNDLE) != spec['bundle_sha256'] or len(spec['instructions']) != 476:
        raise ValueError('capture/instruction guards changed')
    return validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())), spec)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True)
    p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    p.add_argument('--data',type=Path,default=ROOT/'build/tests')
    a=p.parse_args()
    if a.report.exists():p.error('fresh report required')
    spec=json.loads(SPEC.read_text());retail=validate(spec)
    original_bytes(a.binary,spec['instructions'])
    from wc3_pathfinding_engine_tests import run_engine
    engine=run_engine(a.test_binary,a.data,a.report,TESTS,timeout=240)
    result=dict(passed=True,status='verified',retail=retail,engine=engine,
                instructions=476,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'))
    a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
