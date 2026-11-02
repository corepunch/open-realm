#!/usr/bin/env python3
"""Revalidate the frozen owner baseline and three engine-integrated route variants."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

from research.e2e203_expected import SOURCES, render, routes
from run_wc3_pathfinding_corpus import relative

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tools/ghidra/fixtures/retail-e2e-baseline203-1.27.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(spec, root=ROOT):
    if (spec['version'] != 1 or spec['task'] != 'E2E-01.1' or spec['base'] != 'BASE-06.5' or
        spec['repetitions'] != dict(original=2, engine_per_edition=2) or
        spec['engine_editions'] != ['classic', 'tft']):
        raise ValueError('cross-feature baseline scope differs')
    base = json.loads((root / 'tools/ghidra/fixtures/retail-owner-baseline-1.27.json').read_text())
    if spec['build'] != base['build']:
        raise ValueError('baseline binary provenance differs')
    required = set(SOURCES.values()) | {
        'tools/ghidra/fixtures/retail-owner-baseline-1.27.json',
        'tools/ghidra/fixtures/retail-owner-baseline-states-1.27.json', spec['route_header']}
    if set(spec['pins']) != required:
        raise ValueError('baseline source inventory differs')
    for path, pin in spec['pins'].items():
        if digest(root / relative(path)) != pin:
            raise ValueError('baseline source pin differs: ' + path)
    if spec['route_header'] != 'games/warcraft-3/game/tests/retail_e2e_routes203.h':
        raise ValueError('baseline engine contract differs')
    if (root / spec['route_header']).read_text() != render(root):
        raise ValueError('engine route expectations differ from original outputs')
    observed = routes(root)
    contracts = [('static-detour', 'detour', 34, 22), ('blocked-point', 'blocked', 207, 46),
                 ('disconnected-crossing', 'disconnected', 635, 6025)]
    if len(spec['scenarios']) != len(contracts):
        raise ValueError('baseline scenario inventory differs')
    for row, (name, source, motion, suffix) in zip(spec['scenarios'], contracts, strict=True):
        if (row['id'] != name or row['routes_source'] != SOURCES[source] or
            row['constructed_routes'] != len(observed[source]) or row['motion_commits'] != motion or
            row['saved_motion_commits'] != suffix or not row['scope'] or not row['completion']):
            raise ValueError('baseline variant contract differs: ' + name)
    return [len(rows) for rows in observed.values()]


def engine_totals(log, code):
    totals = re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===', log)
    if code or len(totals) != 1:
        raise ValueError('engine baseline did not pass exactly once')
    passed, total, count = map(int, totals[0])
    if passed != total or passed == 0 or count != 3:
        raise ValueError('engine baseline selected no tests or omitted a variant')
    return passed


def verify(a):
    spec = json.loads(a.fixture.read_text())
    constructed = validate(spec)
    if digest(a.binary) != spec['build']['game_sha256'] or digest(a.binary.with_name('msvcr120.dll')) != spec['build']['crt_sha256']:
        raise ValueError('original binary/CRT pin differs')
    work = a.report.with_suffix('.work')
    work.mkdir(parents=True, exist_ok=False)
    python, binary, engine = sys.executable, str(a.binary.resolve()), str(a.engine_library.resolve())
    archive = a.archive.resolve()
    commands = {
        'owner-baseline': [python, 'tools/ghidra/verify_wc3_pathing_order_tasks.py', '--binary', binary, '--producer-baseline'],
        'static-detour': [python, 'tools/ghidra/verify_wc3_pathing_motion.py', '--binary', binary, '--engine-library', engine,
                           '--primary-route-reference', SOURCES['detour']],
        'blocked-point': [python, 'tools/frida/verify_wc3_blocked_goal_trace.py',
                          *[str(archive / 'runtime' / n) for n in ('blocked-goal-v2-first-retry-261002.jsonl', 'blocked-goal-v2-repeat-261002.jsonl')],
                          '--fixture', SOURCES['blocked'], '--engine-library', engine,
                          '--check-engine-header', 'games/warcraft-3/game/tests/retail_blocked_goal.h'],
        'disconnected-crossing': [python, 'tools/frida/verify_wc3_gate_traversal_trace.py', '--capture',
                                 *[str(archive / 'runtime' / n) for n in ('gate98-wall-live-first.jsonl', 'gate98-wall-live-second.jsonl')],
                                 '--fixture', SOURCES['disconnected']],
    }
    original = {}
    for name, command in commands.items():
        path = work / (name + '.json')
        command += ['--report', str(path)]
        with path.with_suffix('.log').open('w') as log:
            child = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=900)
        if child.returncode or not path.is_file():
            raise ValueError('fresh original baseline failed: ' + name)
        result = json.loads(path.read_text())
        if result.get('passed') is not True:
            raise ValueError('original baseline report did not pass: ' + name)
        original[name] = dict(report_sha256=digest(path), command=command)
        print(name + ': fresh original contract passed', flush=True)
    editions = {}
    for mode in spec['engine_editions']:
        path = work / (mode + '.log')
        junit = work / (mode + '.xml')
        command = [str(a.test_binary.resolve()), '-data', str(a.data.resolve())]
        if mode == 'tft':
            command += ['-tft']
        command += ['+dedicated', '1', '+test', 'wc3_e2e.*']
        with path.open('w') as log:
            child = subprocess.run(command, cwd=ROOT, env=dict(os.environ, TEST_JUNIT=str(junit)),
                                   stdout=log, stderr=subprocess.STDOUT, timeout=180)
        assertions = engine_totals(path.read_text(), child.returncode)
        suite = ET.parse(junit).getroot()
        cases = suite.findall('testcase')
        if (suite.attrib.get('tests') != '3' or suite.attrib.get('failures') != '0' or
            suite.attrib.get('errors') != '0' or suite.attrib.get('skipped') != '0' or
            int(suite.attrib['assertions']) != assertions or
            any(list(row) for row in cases)):
            raise ValueError('engine baseline JUnit disagrees with completed assertions')
        if len(cases) != 3 or {r.attrib['name'] for r in cases} != {r['test'] for r in spec['scenarios']}:
            raise ValueError('engine baseline testcase identities differ')
        editions[mode] = dict(assertions=assertions, command=command, log_sha256=digest(path), junit_sha256=digest(junit))
        print(mode + ': all three engine variants passed twice', flush=True)
    return dict(passed=True, binary_sha256=spec['build']['game_sha256'], crt_sha256=spec['build']['crt_sha256'],
                task=spec['task'], variants=3, constructed_routes=sum(constructed), exact_route_points=sum(len(r['points']) for rows in routes().values() for r in rows),
                original_repeats=2, engine_repeats_per_edition=2, engine_editions=2,
                original=original, engine=editions, fixture_sha256=digest(a.fixture),
                test_binary_sha256=digest(a.test_binary), game_library_sha256=digest(a.test_binary.parent.parent / 'lib/libgame-wc3-test.so'),
                exclusions=spec['exclusions'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('binary', 'engine-library', 'archive', 'report'):
        p.add_argument('--' + key, type=Path, required=True)
    p.add_argument('--fixture', type=Path, default=FIXTURE)
    p.add_argument('--test-binary', type=Path, default=ROOT / 'build/bin/openwarcraft3-tests')
    p.add_argument('--data', type=Path, default=ROOT / 'build/tests')
    a = p.parse_args()
    if a.report.exists():
        p.error('report must be fresh')
    result = verify(a)
    a.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: result[k] for k in ('passed', 'variants', 'constructed_routes', 'exact_route_points')}))


if __name__ == '__main__':
    main()
