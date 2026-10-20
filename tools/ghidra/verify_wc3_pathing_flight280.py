#!/usr/bin/env python3
"""Verify original flying spell approaches and their engine/save continuations."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
from verify_wc3_pathing_target_normalize218 import original_bytes

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / 'tools/ghidra/fixtures/retail-flight280-1.27.json'
BUNDLE = SPEC.with_suffix('.json.gz')
SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
TESTS = ['wc3_movement.flight280*', 'wc3_movement.structure279*',
         'wc3_movement.spell269*', 'wc3_movement.spell185*', 'wc3_spell.owner268*',
         'wc3_spell.owner267*', 'wc3_save.rejects_invalid_physical_group_payloads',
         'wc3_save.current_format_uses_current_entity_layout']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def markers(text):
    return re.findall(r'call Preload\( "(F280 [^"\r\n]*)" \)', text)


def scenes(rows):
    result = [[] for _ in range(6)]
    scene = -1
    for row in rows:
        if row['event'] == 'marker':
            scene = int(re.search(r'scene=(-?\d+)', row['value'])[1])
        if 0 <= scene < 6:
            result[scene].append(row)
    return result


def decisions(rows):
    motion, initial = [], []
    for scene, selected in enumerate(scenes(rows)):
        point = scene in (3, 4)
        spells = [r for r in selected if r['event'] == 'spell-initial']
        tasks = [r for r in selected if r['event'] == 'begin-task']
        targets = [r for r in selected if r['event'] == 'begin-target']
        points = [r for r in selected if r['event'] == 'begin-point']
        if len(spells) != 1 or len(points) != (2 if point else 1):
            raise ValueError('native producer ownership')
        if points[0]['range'] != 0 or points[0]['point'] != [0x43900000, 0x44600000]:
            raise ValueError('superseded Move differs')
        if point:
            if tasks or targets or points[1]['range'] != 0x43a68000 or points[1]['point'] != [0x44c00000, 0x43900000]:
                raise ValueError('point captured range')
        else:
            if len(tasks) != 1 or len(targets) != 1 or spells[0]['target'] != tasks[0]['target']:
                raise ValueError('native target ownership')
            if any(r['range'] != 0x43a68000 or r['persistent'] for r in tasks + targets):
                raise ValueError('target captured range/persistence')
        visits = [r for r in selected if r['event'] == 'gtick' and r['members']
                  and r['members'][0].get('grp') == r['id']]
        clocks = {r['c']: r['clock'] for r in selected
                  if r['event'] == 'spell-owner-clock' and r['phase'] == 'begin'}
        if len(visits) != [86, 33, 33, 90, 33, 82][scene]:
            raise ValueError('physical ownership count')
        if visits[0]['members'][0]['r'] != 0x3e800000 or visits[0]['members'][0]['max'] != 0x412f0000:
            raise ValueError('flying caster profile')
        initial.append(spells[0]['clock'][:2])
        captured = []
        for row in visits:
            member = row['members'][0]
            if len(row['members']) != 1 or row['c'] not in clocks:
                raise ValueError('physical owner clock')
            captured.append([row['c'], *clocks[row['c']], *member['pos'],
                             *member['vel'], member['range'], member['facing']])
        motion.append(captured)
        if scene == 0:
            fine = next(r for r in selected if r['event'] == 'fine' and r['search']['pops'] > 0)
            if fine['cnt'] != [25, 3] or fine['idx'] != [0, 0]:
                raise ValueError('fresh flyer adaptive route')
    return dict(initial=initial, motion=motion)


def validate_runtime(bundle, spec):
    captures = bundle['captures']
    if len(captures) != 3 or [c['rows'][0]['mode'] for c in captures] != ['observe', 'observe', 'control']:
        raise ValueError('repeats/control required')
    repeated = []
    for cap in captures:
        rows = cap['rows']
        meta, footer = rows[0], rows[-1]
        public = markers(cap['preload'])
        if (meta['task'] != 'FLYING-SPELL' or meta['sha256'] != SHA or not meta['owned']
                or meta['env'] not in ('B', 'C') or meta['source_sha256']['map'] != spec['map_sha256']
                or not footer.get('complete') or footer['markers'] != len(public)
                or hashlib.sha256(cap['preload'].encode()).hexdigest() != footer['sha256']
                or any(r.get('type') == 'error' or r['event'] == 'trace-failed' for r in rows)):
            raise ValueError('capture provenance/completion')
        for name in ('flight280_capture.py', 'flight280_observer.js', 'flight280_make_map.py', 'flight280_probe.j'):
            if meta['source_sha256'][name] != spec['source_sha256']['tools/frida/research/' + name]:
                raise ValueError('captured source differs')
        if public != spec['public'] or len(public) != 634:
            raise ValueError('original public movement differs')
        if meta['mode'] == 'control':
            if any(r['event'] in ('trace-end', 'gtick', 'begin-point', 'begin-target', 'spell-initial') for r in rows):
                raise ValueError('hooked control')
        else:
            live = [r['value'] for r in rows if r['event'] == 'marker' and ' label=start' not in r['value']]
            if live != public:
                raise ValueError('live public markers differ')
            ends = [r for r in rows if r['event'] == 'trace-end']
            if len(ends) != 1 or not ends[0]['installed'] or any(k.endswith('-dropped') and v for k, v in ends[0]['counts'].items()):
                raise ValueError('observer incomplete')
            counts = Counter(r['event'] for r in rows)
            for event in ('marker', 'spell-initial', 'begin-task', 'begin-target', 'begin-point', 'gtick', 'gtick-end', 'spell-owner-clock'):
                if counts[event] != ends[0]['counts'][event]:
                    raise ValueError('observer incomplete')
            repeated.append(decisions(rows))
    if repeated[0] != repeated[1] or repeated[0] != spec['contract']:
        raise ValueError('repeated physical motion differs')
    return dict(captures=2, controls=1, public_markers=634, task_cases=6, physical_visits=357)


def validate_fixture(path, spec):
    text = path.read_text()
    clocks = re.search(r'flight280_clock\[6\]\[2\]=\{(.*?)\};', text, re.S)[1]
    read = lambda s: [[int(w, 0) for w in re.findall(r'0x[0-9a-f]+|[0-9]+', row)]
                      for row in re.findall(r'\{([^{}]+)\}', s)]
    motion = [read(re.search(r'flight280_motion_' + str(i) + r'\[\]\[10\]=\{(.*?)\};', text, re.S)[1])
              for i in range(6)]
    if dict(initial=read(clocks), motion=motion) != spec['contract']:
        raise ValueError('engine fixture differs from original motion')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--test-binary', type=Path, default=ROOT / 'build/bin/openwarcraft3-tests')
    ap.add_argument('--data', type=Path, default=ROOT / 'build/tests')
    ap.add_argument('--report', type=Path, required=True)
    a = ap.parse_args()
    if a.report.exists():
        ap.error('fresh report required')
    spec = json.loads(SPEC.read_text())
    if spec['game_sha256'] != SHA or spec['engine_patterns'] != TESTS:
        raise ValueError('contract changed')
    for name, expected in spec['source_sha256'].items():
        if digest(ROOT / name) != expected:
            raise ValueError('source pin ' + name)
    if digest(BUNDLE) != spec['bundle_sha256']:
        raise ValueError('damaged frozen captures')
    validate_fixture(ROOT / 'games/warcraft-3/game/skills/fixtures/retail_flight280_motion.h', spec)
    evidence = validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())), spec)
    original_bytes(a.binary, spec['instructions'])
    engine = []
    for tft in (False, True):
        for i, pattern in enumerate(TESTS):
            stem = a.report.with_name(a.report.stem + ('-tft' if tft else '-classic') + f'-{i}')
            xml, log = stem.with_suffix('.xml'), stem.with_suffix('.log')
            if xml.exists() or log.exists():
                raise ValueError('fresh engine outputs required')
            with log.open('w') as out:
                r = subprocess.run([str(a.test_binary.resolve()), '-data', str(a.data.resolve()),
                                    *(['-tft'] if tft else []), '+dedicated', '1', '+test', pattern],
                                   stdout=out, stderr=subprocess.STDOUT, timeout=120,
                                   env={**os.environ, 'TEST_JUNIT': str(xml.resolve())})
            x = ET.parse(xml).getroot()
            cases = list(x.iter('testcase'))
            if r.returncode or not cases or any(c.findall('failure') for c in cases):
                raise ValueError('engine ' + str(log))
            engine.append(dict(tft=tft, pattern=pattern, tests=len(cases), assertions=int(x.get('assertions'))))
    report = dict(status='verified', passed=True, binary_sha256=SHA, instructions=len(spec['instructions']),
                  **evidence, engine=engine, scope=spec['scope'])
    a.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
