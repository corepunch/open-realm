#!/usr/bin/env python3
"""Check original player point transport/admission and frozen engine continuations."""
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
FIXTURE = ROOT / 'tools/ghidra/fixtures/retail-player-point214-1.27.json'
BUNDLE = ROOT / 'tools/ghidra/fixtures/retail-player-point214-1.27.json.gz'
SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
POINTS = [(8, [0x44c80006, 0x4480000d]), (9, [0x44d21463, 0x4479449e]), (24, [0x44cd4841, 0x44836ba8])]
TESTS = ['wc3_point214.move_command_matches_original_journeys',
         'wc3_point214.shift_move_command_matches_original_journeys',
         'wc3_point214.smart_command_matches_original_journeys',
         'wc3_point214.shift_smart_command_matches_original_journeys']
CLIENT = 'client_input.point_commands_round_trip_fractional_world_coordinates'
SOURCES = ['tools/frida/research/point214_capture.py', 'tools/frida/research/point214_observer.js',
           'tools/frida/research/point214_make_map.py', 'tools/frida/research/point214_probe.j',
           'tools/frida/research/point214_ui_input.c', 'tools/frida/wc3_ui_input.c',
           'tools/frida/research/group032_make_map.py', 'tools/frida/make_wc3_pathfinding_map.py',
           'tools/ghidra/research/Point214Evidence.java',
           'games/warcraft-3/game/tests/retail_selected_point.h',
           'games/warcraft-3/game/tests/retail_selected_idle_shift.h',
           'tools/ghidra/fixtures/retail-selected-point-1.27.json',
           'tools/ghidra/fixtures/retail-selected-idle-shift-1.27.json']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(spec):
    if (spec['version'] != 1 or spec['task'] != 'BASE-01.1' or spec['game_sha256'] != SHA or
        spec['engine_tests'] != TESTS or spec['client_test'] != CLIENT or
        spec['point_commands'] != [[flag, point] for flag, point in POINTS] or
        set(spec['pins']) != set(SOURCES) or len(spec['instructions']) < 20):
        raise ValueError('player point contract differs')
    for path, expected in spec['pins'].items():
        if digest(ROOT / path) != expected:
            raise ValueError('changed source or existing retail expectation: ' + path)
    if digest(BUNDLE) != spec['bundle_sha256']:
        raise ValueError('original capture bundle differs')
    return spec


def validate_runtime(bundle, spec):
    cases = bundle['captures']
    if len(cases) != 3 or [c['rows'][0]['mode'] for c in cases] != ['observe', 'observe', 'control']:
        raise ValueError('missing repeated original/control capture')
    for case in cases:
        rows, preload = case['rows'], case['preload']
        meta, footer = rows[0], rows[-1]
        if (meta['event'] != 'metadata' or meta['task'] != 'BASE-01.1' or meta['sha256'] != SHA or
            not meta['owned'] or meta['env'] not in ('B', 'C') or meta['source_sha256']['map'] != spec['map_sha256'] or
            footer['event'] != 'preload-file' or not footer['complete'] or footer['markers'] != 122 or
            hashlib.sha256(preload.encode()).hexdigest() != footer['sha256'] or
            any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows)):
            raise ValueError('unowned, failed or incomplete original capture')
        for path in SOURCES[:6]:
            if meta['source_sha256'][Path(path).name] != spec['pins'][path]:
                raise ValueError('captured observer/input source differs')
        if any(meta['source_sha256'][name] != value for name, value in spec['helper_sha256'].items()):
            raise ValueError('native owned-input helper differs')
        inputs = [r for r in rows if r['event'] == 'player-input']
        if (len(inputs) != 3 or any(r['rc'] != 0 or 'down/up accepted' not in r['stdout'] for r in inputs) or
            [(r['plan']['shift'], r['plan']['alt']) for r in inputs] != [(False, False), (True, False), (False, True)]):
            raise ValueError('genuine ordinary/Shift/Alt input missing')
        public = re.findall(r'call Preload\( "(P214 [^"\r\n]*)" \)', preload)
        if (not public or 'label=complete' not in public[-1] or
            not any(all(f'u{i}=' in p and p.split(f'u{i}=')[1].split(' ')[0].endswith(',851986')
                        for i in range(6)) for p in public)):
            raise ValueError('public six-unit movement or completion missing')
        if meta['mode'] == 'control':
            if any(r['event'] in ('module', 'trace-end', 'action', 'ui-point') for r in rows):
                raise ValueError('control was instrumented')
            continue
        ends = [r for r in rows if r['event'] == 'trace-end']
        if len(ends) != 1 or ends[0].get('readOnly') is not True:
            raise ValueError('observer completion/read-only contract missing')
        counts = Counter(r['event'] for r in rows if 'seq' in r)
        if dict(counts) != ends[0]['counts']:
            raise ValueError('observer counts differ')
        stages = ['ui-point', 'submit', 'serialize', 'deserialize', 'action']
        for stage in stages:
            events = [r for r in rows if r['event'] == stage]
            if len(events) != 3:
                raise ValueError('missing command stage: ' + stage)
            for event, (flags, point) in zip(events, POINTS, strict=True):
                if (event['flags'], event['point'], event['order']) != (flags, point, 851986):
                    raise ValueError('fractional input/flags changed at ' + stage)
        ui = [r for r in rows if r['event'] == 'ui-point']
        if any(r['source'] != '0x0' or r['target'] != '0x0' or r['caller'] != '3cbdf7' for r in ui):
            raise ValueError('unexpected UI producer')
        for index, (flags, point) in enumerate(POINTS):
            sequence = [[r for r in rows if r['event'] == stage][index]['seq'] for stage in stages]
            if sequence != sorted(set(sequence)):
                raise ValueError('UI/wire/dispatch sequence differs')
        actions = [r for r in rows if r['event'] == 'action']
        for action, (flags, point) in zip(actions, POINTS, strict=True):
            if (action['entry'], action['player'], action['caller'], action['words'][2],
                action['words'][5] & 255, action['words'][8:10], action['words'][12:14]) != (
                    '6b9f70', 3, '32d660', 0xa0012, 0x12, [0xffffffff]*2, [0xffffffff]*2):
                raise ValueError('decoded action fields or synchronized dispatcher differ')
            end = next((r for r in rows if r['event'] == 'action-end' and r['seq'] > action['seq']), None)
            if end is None:
                raise ValueError('missing action end')
            nested = [r for r in rows if 'seq' in r and action['seq'] < r['seq'] < end['seq']]
            publications = [r for r in nested if r['event'] == 'publish']
            admissions = [r for r in nested if r['event'] == 'admit']
            if len(publications) != 6 or len({r['unit'] for r in publications}) != 6:
                raise ValueError('selected membership/publication differs')
            for pub in publications:
                if (pub['flags'], pub['fallback'], pub['caller'], pub['orderWords'][9:11],
                    [pub['orderWords'][18], pub['orderWords'][20]]) != (flags, 0, '6ba4c9', [851986, 3], point):
                    raise ValueError('published order payload/ABI differs')
            if flags & 1:
                if admissions:
                    raise ValueError('Shift unexpectedly replaced current orders')
            else:
                if len(admissions) != 6:
                    raise ValueError('ordinary/Alt did not reach unit admission')
                for pub, admit in zip(publications, admissions, strict=True):
                    if ((pub['unit'], pub['order']) != (admit['unit'], admit['order']) or
                        (admit['mode'], admit['dispatch'], admit['caller']) != (1, 1, '6b952f') or pub['seq'] >= admit['seq']):
                        raise ValueError('replacement unit/caller/stack ABI differs')
    return dict(captures=2, controls=1, actions=6, publications=36, replacements=24)


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
        rva = int(address, 16) - base
        raw = None
        for virtual_size, virtual_at, raw_size, raw_at in sections:
            if virtual_at <= rva < virtual_at + min(virtual_size, raw_size):
                start = raw_at + rva - virtual_at
                raw = data[start:start+len(bytes.fromhex(expected))]
                break
        if raw != bytes.fromhex(expected):
            raise ValueError('original instruction differs: ' + address)
    return len(instructions)


def run_engine(binary, data, report):
    results = {}
    for edition in ('classic', 'tft'):
        for name, pattern, expected in [('movement', 'wc3_point214.*', set(TESTS)), ('transport', CLIENT, {CLIENT})]:
            log = report.with_name(report.stem+'-'+edition+'-'+name+'.log')
            junit = log.with_suffix('.xml')
            command = [str(binary.resolve()), '-data', str(data.resolve())]
            if edition == 'tft':
                command += ['-tft']
            command += ['+dedicated', '1', '+test', pattern]
            with log.open('w') as out:
                proc = subprocess.run(command, cwd=ROOT, env=dict(os.environ, TEST_JUNIT=str(junit)),
                                      stdout=out, stderr=subprocess.STDOUT, timeout=180)
            totals = re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===', log.read_text())
            if proc.returncode or len(totals) != 1:
                raise ValueError('engine run did not complete once')
            passed, total, cases = map(int, totals[0])
            suite = ET.parse(junit).getroot()
            if (not passed or passed != total or cases != len(expected) or
                {r.attrib['name'] for r in suite.findall('testcase')} != expected or
                suite.attrib['failures'] != '0' or suite.attrib['errors'] != '0' or suite.attrib['skipped'] != '0'):
                raise ValueError('empty, failed or incomplete engine acceptance')
            results[edition+'-'+name] = dict(assertions=passed, tests=cases, log_sha256=digest(log), junit_sha256=digest(junit))
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
    spec = validate(json.loads(FIXTURE.read_text()))
    result = validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())), spec)
    result.update(instructions=original_bytes(args.binary, spec['instructions']),
                  engine=run_engine(args.test_binary, args.data, args.report), passed=True, binary_sha256=SHA,
                  fixture_sha256=digest(FIXTURE), test_binary_sha256=digest(args.test_binary),
                  game_library_sha256=digest(args.test_binary.parent.parent/'lib/libgame-wc3-test.so'),
                  exclusions=spec['exclusions'])
    args.report.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))

if __name__ == '__main__':
    main()
