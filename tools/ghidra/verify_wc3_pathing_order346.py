#!/usr/bin/env python3
"""Verify frozen arrival/failure branches and production cargo/guard ownership."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from verify_wc3_pathing_target_normalize218 import SHA, original_bytes

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT/'tools/ghidra/fixtures/retail-order346-1.27.json'
BUNDLE = ROOT/'tools/ghidra/fixtures/retail-order346-inputs-1.27.json.gz'
TESTS = ['wc3_movement.order346*', 'wc3_movement.order345*',
         'wc3_movement.order344*', 'wc3_unit.*', 'wc3_save.*',
         'wc3_order_lifecycle.*', 'wc3_spell.*']
# These captured markers lie outside the preload file's recording interval.
# UI MapM clears three startup markers; the other probes emit callbacks after
# PreloadGenEnd. They remain in the frozen stream and are not parity claims.
MARKER_WINDOWS = {
    'boarding-map-r/observe24.jsonl': (0,1),
    'deferred-load-map-ac/observe32.jsonl': (0,3),
    'direct-load-map-s-t/observe26.jsonl': (0,1),
    'load-retirement-map-u-v/observe27.jsonl': (0,1),
    'load-retirement-map-u-v/observe28.jsonl': (0,1),
    'simultaneous-map-w-x-y/observe29.jsonl': (0,1),
    'ui-drop-map-m/observe17.jsonl': (3,0),
    'ui-drop-map-m/observe18.jsonl': (3,0),
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def public(text):
    return re.findall(r'call Preload\( "(O346 [^"\r\n]*)" \)', text)


def native(rows):
    return [r for r in rows if 'seq' in r and r['event'] != 'module']


def validate(spec, files):
    if spec['game_sha256'] != SHA or spec['task'] != 'ORDER-01.2':
        raise ValueError('wrong evidence contract')
    if set(files) != set(spec['files']):
        raise ValueError('archive membership changed')
    source_hashes = {digest(text.encode()) for text in files.values()}
    captures = {}
    for name, text in files.items():
        if digest(text.encode()) != spec['files'][name]:
            raise ValueError('archive content changed: '+name)
        if not name.endswith('.jsonl'):
            continue
        rows = [json.loads(line) for line in text.splitlines()]
        meta, end = rows[0], rows[-1]
        if meta['source_sha256']['order346_observer.js'] not in source_hashes:
            raise ValueError('missing captured observer source: '+name)
        preload = files[name[:-6]+'-preload.txt']
        if (meta.get('sha256') != SHA or not meta.get('owned') or
                (meta.get('env'), meta.get('display'), meta.get('remote')) !=
                ('B', ':98', '127.0.0.1:27049') or
                end.get('event') != 'preload-file' or not end.get('complete') or
                end['sha256'] != digest(preload.encode()) or end['markers'] != len(public(preload)) or
                any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows)):
            raise ValueError('invalid capture: '+name)
        events = [r for r in rows if 'seq' in r]
        if meta['mode'] == 'control':
            if events or any(r.get('event') in ('module', 'trace-end') for r in rows):
                raise ValueError('instrumented control: '+name)
        else:
            ends = [r for r in rows if r.get('event') == 'trace-end']
            markers = [r['value'] for r in events if r['event'] == 'marker']
            prefix, suffix = MARKER_WINDOWS.get(name,(0,0))
            if (len(ends) != 1 or not ends[0].get('readOnly') or
                    [r['seq'] for r in events] != list(range(1, len(events)+1)) or
                    ends[0]['counts'] != dict(Counter(r['event'] for r in events)) or
                    len(markers) != prefix+len(public(preload))+suffix or
                    markers[prefix:len(markers)-suffix if suffix else None] != public(preload)):
                raise ValueError('incomplete native stream: '+name)
        captures[name[:-6]] = (rows, public(preload))
    groups = [('map-c/', 'observe4', 'observe5', 'control2', 979),
              ('final/', 'observe-ay', 'repeat-ay', 'control-ay', 871),
              ('final/', 'observe-bc-shore', 'repeat-bc-shore', 'control-bc', 607)]
    for folder, first, repeat, control, count in groups:
        a, b, c = [captures[folder+n] for n in (first, repeat, control)]
        left, right = native(a[0]), native(b[0])
        if first == 'observe-bc-shore':
            # The repeat adds a read-only shore-query hook. Compare every
            # shared event exactly, and independently check its two new rows.
            queries = [r for r in right if r['event'] == 'shore-query']
            if ([r['tick'] for r in queries] != [20,21] or
                    any(r['source'] != [1153957888,1137180672] or
                        r['target'] != [1137180672,1137180672] or
                        r['masks'] != [0,4,2,0,4,6,0] for r in queries)):
                raise ValueError('shore-query producer differs')
            project = lambda rows: [{k:v for k,v in r.items() if k != 'seq'}
                                    for r in rows if r['event'] != 'shore-query']
            left, right = project(left), project(right)
        if (len(a[1]) != count or a[1] != b[1] or a[1] != c[1] or
                left != right or
                len({r[0][0]['source_sha256']['map'] for r in (a,b,c)}) != 1):
            raise ValueError('repeated/control branch differs: '+folder+first)
    ay = captures['final/observe-ay'][0]
    at51 = [r for r in ay if r.get('tick') == 51]
    def one(rows, event):
        matches = [r for r in rows if r.get('event') == event]
        if len(matches) != 1:
            raise ValueError('expected one '+event)
        return matches[0]
    failure = one(at51, 'cant-path-enter')
    if (failure['unit']['head'] != [0xffffffff,0xffffffff] or not failure['flags'] & 2 or
            one(at51, 'recover-enter')['caller'] != '0x603305' or
            one(at51, 'ai-immediate')['caller'] != '0x60330e' or
            one(at51, 'arrival-task-head')['code'] != 0xd0144 or
            one(at51, 'guard-return')['point'] != [1132986368,1134034944]):
        raise ValueError('headless AI recovery differs')
    bc = captures['final/observe-bc-shore'][0]
    at32 = [r for r in bc if r.get('tick') == 32]
    if (one(at32,'recover-load-ability')['flags'] != 0x8011 or
            one(at32,'recover-load-leave')['result'] != 1 or
            one(at32,'arrival-enter')['caller'] != '0x5fb731' or
            one(at32,'arrival-task-head')['code'] != 0xd016b or
            one(at32,'arrival-leave')['flags'] != 16):
        raise ValueError('positive point-Load recovery differs')
    tasks = [r for r in bc if r.get('event') == 'point-task-create' and r['tick'] == 21]
    if [(r['code'],r['x'],r['y'],r['range']) for r in tasks] != [
            (0xd016c,1149947084,1138229248,1140457472),
            (0xd016b,1148977152,1138229248,1115684864)]:
        raise ValueError('shore point producer differs')
    if [r['tick'] for r in bc if r.get('event') == 'cant-path-enter'] != [32,71,79,88,96,104,113]:
        raise ValueError('Board retries differ')
    c = captures['map-c/observe4'][0]
    at37 = [r for r in c if r.get('tick') == 37]
    before, after = one(at37,'arrival-enter'), one(at37,'arrival-leave')
    if (before['flags'] != 2196 or before['unit']['widgetFlags'] != 536876102 or
            after['flags'] != 16 or after['unit']['widgetFlags'] != 5702 or
            after['unit']['head'] != [0xffffffff,0xffffffff] or
            after['unit']['task'] != [0xffffffff,0xffffffff]):
        raise ValueError('arrival800 release/pop differs')
    return {'captures':len(captures),'repeated_public_markers':2457,
            'headless_ai':True,'positive_point_load':True,'arrival800':True}


def archived_checks(spec, files):
    results = []
    with tempfile.TemporaryDirectory(prefix='order346-evidence-') as directory:
        root = Path(directory)
        for name, text in files.items():
            path = root/name
            if Path(name).is_absolute() or '..' in Path(name).parts:
                raise ValueError('unsafe archive path')
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text(text)
        for name in spec['semantic_verifiers']:
            result = subprocess.run([sys.executable,str(root/name)],capture_output=True,text=True,timeout=30)
            if result.returncode:
                raise ValueError(name+'\n'+result.stdout+result.stderr)
            results.append({'verifier':name,'output':result.stdout})
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    parser.add_argument('--data',type=Path,default=ROOT/'build/tests')
    args = parser.parse_args()
    if args.report.exists():
        parser.error('fresh report required')
    spec = json.loads(SPEC.read_text())
    if digest(BUNDLE.read_bytes()) != spec['bundle_sha256']:
        raise ValueError('bundle changed')
    files = json.loads(gzip.decompress(BUNDLE.read_bytes()))
    retail = validate(spec,files)
    archived = archived_checks(spec,files)
    original_bytes(args.binary,spec['instructions'])
    from wc3_pathfinding_engine_tests import run_engine
    engine = run_engine(args.test_binary,args.data,args.report,TESTS,timeout=300)
    result = dict(passed=True,status='verified',binary_sha256=SHA,retail=retail,archived=archived,
                  instructions=len(spec['instructions']),engine=engine)
    args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
