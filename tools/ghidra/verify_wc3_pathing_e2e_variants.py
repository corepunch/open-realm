#!/usr/bin/env python3
"""Revalidate combined formations/crowds/gates and repeat actual engine journeys."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

from research.e2e205_contract import ROOT, CATEGORIES, validate
from run_wc3_pathfinding_corpus import DEFAULT_MANIFEST, load_manifest, run_entry
from verify_wc3_pathing_e2e import validate as validate_parent

FIXTURE = ROOT / 'tools/ghidra/fixtures/retail-e2e-variants205-1.27.json'


def engine_totals(log, code, expected_cases=3):
    found = re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===', log)
    if code or len(found) != 1:
        raise ValueError('combined engine run did not complete exactly once')
    passed, total, cases = map(int, found[0])
    if passed != total or passed == 0 or cases != expected_cases:
        raise ValueError('empty, incomplete or failed combined engine run')
    return passed


def verify(a, validator=validate, categories=CATEGORIES, prefix='wc3_e2e205'):
    case_count = len(categories)
    spec = json.loads(a.fixture.read_text())
    manifest = load_manifest(DEFAULT_MANIFEST)
    entries = validator(spec, manifest)
    parent = json.loads((ROOT / spec['parent']).read_text())
    if validate_parent(parent) != [1, 6, 30]:
        raise ValueError('static/disconnected predecessor differs')
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    if digest(a.binary) != spec['build']['game_sha256'] or digest(a.binary.with_name('msvcr120.dll')) != spec['build']['crt_sha256']:
        raise ValueError('retail executable/CRT differs')
    work = a.report.with_suffix('.work')
    work.mkdir(parents=True, exist_ok=False)
    context = dict(python=sys.executable, binary=a.binary.resolve(), archive=a.archive.resolve(),
                   output=work, timeout=900)
    original = []
    for entry in entries:
        result = run_entry(entry, context, manifest['target'])
        if not result['verified_expected_status']:
            raise ValueError('fresh cross-feature original failed: ' + entry['id'] + ': ' + result['failure'])
        original.append(result)
        print(entry['id'] + ': complete original contract passed', flush=True)
    editions = {}
    for edition in spec['engine_editions']:
        log, junit = work / (edition + '.log'), work / (edition + '.xml')
        command = [str(a.test_binary.resolve()), '-data', str(a.data.resolve())]
        if edition == 'tft':
            command += ['-tft']
        command += ['+dedicated', '1', '+test', prefix + '.*']
        with log.open('w') as out:
            child = subprocess.run(command, cwd=ROOT, env=dict(os.environ, TEST_JUNIT=str(junit)),
                                   stdout=out, stderr=subprocess.STDOUT, timeout=300)
        count = engine_totals(log.read_text(), child.returncode, case_count)
        suite = ET.parse(junit).getroot()
        cases = suite.findall('testcase')
        if (suite.attrib.get('tests') != str(case_count) or suite.attrib.get('failures') != '0' or
            suite.attrib.get('errors') != '0' or suite.attrib.get('skipped') != '0' or
            int(suite.attrib['assertions']) != count or any(list(row) for row in cases) or
            {row.attrib['name'] for row in cases} != {test for test, _ in categories.values()}):
            raise ValueError('combined engine testcase identities or counters differ')
        editions[edition] = dict(assertions=count, command=command, log_sha256=digest(log), junit_sha256=digest(junit))
        print(edition + ': complete fresh/save journeys repeated twice', flush=True)
    return dict(passed=True, binary_sha256=spec['build']['game_sha256'], crt_sha256=spec['build']['crt_sha256'],
                task=spec['task'], categories=case_count, retail_contracts=len(entries), engine_repeats_per_edition=2,
                engine_editions=2, original=original, engine=editions, fixture_sha256=digest(a.fixture),
                test_binary_sha256=digest(a.test_binary),
                game_library_sha256=digest(a.test_binary.parent.parent / 'lib/libgame-wc3-test.so'),
                exclusions=spec['exclusions'])


def main(fixture=FIXTURE, validator=validate, categories=CATEGORIES, prefix='wc3_e2e205'):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--archive', type=Path, required=True)
    p.add_argument('--fixture', type=Path, default=fixture)
    p.add_argument('--test-binary', type=Path, default=ROOT / 'build/bin/openwarcraft3-tests')
    p.add_argument('--data', type=Path, default=ROOT / 'build/tests')
    p.add_argument('--report', type=Path, required=True)
    a = p.parse_args()
    if a.report.exists():
        p.error('report must be new')
    result = verify(a, validator, categories, prefix)
    a.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
