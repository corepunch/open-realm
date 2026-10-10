#!/usr/bin/env python3
"""Verify original UnitShareVision reveal, alliance snapshots and engine/save state."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT/'tools/ghidra/fixtures/retail-target-reveal217-1.27.json'
BUNDLE = FIXTURE.with_suffix('.json.gz')
SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
TESTS = ['wc3_movement.target217_direct_shared_vision_admits_invisible_target_and_revokes_without_loss_event',
         'wc3_movement.target217_shared_reveal_retains_alliance_snapshot_across_save_until_refresh',
         'wc3_movement.target217_public_visibility_inverse_includes_fog_and_invalid_handles']
SOURCES = ['tools/frida/research/target217_capture.py', 'tools/frida/research/target217_observer.js',
           'tools/frida/research/target217_make_map.py', 'tools/frida/research/target217_probe.j',
           'tools/frida/research/point214_ui_input.c', 'tools/frida/wc3_ui_input.c',
           'tools/frida/research/group032_make_map.py', 'tools/frida/make_wc3_pathfinding_map.py',
           'tools/ghidra/research/Target217Evidence.java']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def markers(preload):
    return re.findall(r'call Preload\( "(T217 [^"\r\n]*)" \)', preload)


def validate(spec):
    if (spec['version'] != 1 or spec['task'] != 'TARGET-03.1' or spec['game_sha256'] != SHA or
        spec['engine_tests'] != TESTS or set(spec['pins']) != set(SOURCES) or
        len(spec['instructions']) != 437 or len(spec['markers']) != 62):
        raise ValueError('target reissue contract differs')
    for path, expected in spec['pins'].items():
        if digest(ROOT/path) != expected:
            raise ValueError('changed source: '+path)
    if digest(BUNDLE) != spec['bundle_sha256']:
        raise ValueError('original capture bundle differs')
    return spec


def validate_runtime(bundle, spec):
    captures = bundle['captures']
    if len(captures) != 3 or [c['rows'][0]['mode'] for c in captures] != ['observe', 'observe', 'control']:
        raise ValueError('missing repeated original/control capture')
    sequences = []
    for case in captures:
        rows, preload = case['rows'], case['preload']
        meta, footer = rows[0], rows[-1]
        if (meta['event'] != 'metadata' or meta['task'] != spec['task'] or meta['sha256'] != SHA or
            not meta['owned'] or meta['env'] not in ('B', 'C') or meta['source_sha256']['map'] != spec['map_sha256'] or
            footer['event'] != 'preload-file' or not footer['complete'] or footer['markers'] != 62 or
            hashlib.sha256(preload.encode()).hexdigest() != footer['sha256'] or
            markers(preload) != spec['markers'] or
            any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows)):
            raise ValueError('failed, unowned or incomplete original capture')
        for path in SOURCES[:6]:
            if meta['source_sha256'][Path(path).name] != spec['pins'][path]:
                raise ValueError('captured source differs')
        if any(meta['source_sha256'][name] != value for name, value in spec['helper_sha256'].items()):
            raise ValueError('owned input helper differs')
        if meta['mode'] == 'control':
            if any('seq' in r or r['event'] == 'trace-end' for r in rows):
                raise ValueError('control was instrumented')
            continue
        ends = [r for r in rows if r['event'] == 'trace-end']
        if len(ends) != 1 or ends[0].get('readOnly') is not True:
            raise ValueError('observer completion missing')
        if dict(Counter(r['event'] for r in rows if 'seq' in r)) != ends[0]['counts']:
            raise ValueError('observer counts differ')
        public = [r['value'] for r in rows if r['event'] == 'marker']
        if public != spec['markers'] or len(public) != 62:
            raise ValueError('live/preload marker disagreement')
        normalized = [{k: v for k, v in r.items() if k != 'seq'} for r in rows
                      if r['event'] in ('marker', 'query', 'share', 'lost', 'reveal-state') or
                      (r['event'] == 'fallback' and r['caller'] == '66fe40')]
        if normalized != spec['sequence']:
            raise ValueError('shared reveal/alliance refresh sequence differs')
        sequences.append(normalized)
        queries = [r for r in rows if r['event'] == 'query']
        shares = [r for r in rows if r['event'] == 'share']
        lost = [r for r in rows if r['event'] == 'lost']
        if (len(queries) != 734 or Counter((r['caller'], r['flags'], r['mode']) for r in queries) != {('23a7e9',0,4):700, ('5fbc09',0,4):22, ('5fd364',1,4):6, ('5fd4ee',0,4):6} or
            len(shares) != 18 or any(r['caller'] != '218ffd' for r in shares) or
            len(lost) != 40 or any(r['caller'] not in ('68b7d8', '688373', '69c553') for r in lost)):
            raise ValueError('public reveal/loss ownership differs')
    if sequences[0] != sequences[1]:
        raise ValueError('unexplained repeat difference')
    return dict(captures=2, controls=1, queries=1468, shares=36, losses=80, public_markers=186)


def original_bytes(binary, instructions):
    data = binary.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA:
        raise ValueError('retail executable differs')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe+6)[0]
    opt_size = struct.unpack_from('<H', data, pe+20)[0]
    base = struct.unpack_from('<I', data, pe+24+28)[0]
    sections = [struct.unpack_from('<IIII', data, pe+24+opt_size+i*40+8) for i in range(count)]
    for address, expected in instructions.items():
        rva = int(address, 16)-base
        raw = None
        for virtual_size, virtual_at, raw_size, raw_at in sections:
            if virtual_at <= rva < virtual_at+min(virtual_size, raw_size):
                start = raw_at+rva-virtual_at
                raw = data[start:start+len(bytes.fromhex(expected))]
                break
        if raw != bytes.fromhex(expected):
            raise ValueError('original instruction differs: '+address)
    return len(instructions)


def run_engine(binary, data, report):
    results = {}
    for edition in ('classic', 'tft'):
        log = report.with_name(report.stem+'-'+edition+'.log')
        junit = log.with_suffix('.xml')
        command = [str(binary.resolve()), '-data', str(data.resolve())]
        if edition == 'tft':
            command += ['-tft']
        command += ['+dedicated', '1', '+test', 'wc3_movement.target217*']
        with log.open('w') as out:
            proc = subprocess.run(command, cwd=ROOT, env=dict(os.environ, TEST_JUNIT=str(junit)),
                                  stdout=out, stderr=subprocess.STDOUT, timeout=180)
        totals = re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===', log.read_text())
        if proc.returncode or len(totals) != 1:
            raise ValueError('engine run did not complete once')
        passed, total, cases = map(int, totals[0])
        suite = ET.parse(junit).getroot()
        if (passed < 65 or passed != total or cases != len(TESTS) or
            {r.attrib['name'] for r in suite.findall('testcase')} != set(TESTS) or
            any(suite.attrib[key] != '0' for key in ('failures', 'errors', 'skipped'))):
            raise ValueError('empty, failed or incomplete engine acceptance')
        results[edition] = dict(assertions=passed, tests=cases, log_sha256=digest(log), junit_sha256=digest(junit))
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--test-binary', type=Path, default=ROOT/'build/bin/openwarcraft3-tests')
    ap.add_argument('--data', type=Path, default=ROOT/'build/tests')
    args = ap.parse_args()
    if args.report.exists():
        ap.error('report must be new')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    spec = validate(json.loads(FIXTURE.read_text()))
    result = validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())), spec)
    result.update(instructions=original_bytes(args.binary, spec['instructions']),
                  engine=run_engine(args.test_binary, args.data, args.report), passed=True,
                  binary_sha256=SHA, fixture_sha256=digest(FIXTURE), test_binary_sha256=digest(args.test_binary),
                  game_library_sha256=digest(args.test_binary.parent.parent/'lib/libgame-wc3-test.so'),
                  exclusions=spec['exclusions'])
    args.report.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))

if __name__ == '__main__':
    main()
