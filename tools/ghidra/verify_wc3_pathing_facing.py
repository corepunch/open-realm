#!/usr/bin/env python3
"""Revalidate unchanged original angular/visual streams and actual engine saves."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/frida/research'))
from facing207_fixture import extract, visual, render, render_visual

FIXTURE = ROOT / 'tools/ghidra/fixtures/retail-facing207-1.27.json'
SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def words_digest(rows):
    return hashlib.sha256(json.dumps(rows, separators=(',', ':')).encode()).hexdigest()


def validate(spec):
    if (spec['version'] != 1 or spec['build']['game_sha256'] != SHA or
            spec['physical_count'] != 174 or spec['visual_count'] != 381 or
            spec['visual_counts'] != [182, 92, 38, 26, 43] or spec['public_markers'] != 89 or
            len(spec['captures']) != 2 or len(set(spec['captures'])) != 2 or
            spec['engine_tests'] != [
                'wc3_facing.public_timed_cohorts_match_original_and_saved_suffix',
                'wc3_facing.timed_native_retains_pose_and_authored_turn_at_admission',
                'wc3_facing.paused_idle_head_resumes_without_canceling_angular_owner']):
        raise ValueError('angular acceptance contract differs')
    for path, expected in spec['pins'].items():
        if digest(ROOT / path) != expected:
            raise ValueError('repository input differs: ' + path)


def markers(path):
    # PreloadGen container includes environment-specific framing; its F207
    # payload is the public, observer-free comparison contract.
    values = []
    for line in path.read_text().splitlines():
        match = re.search(r'F207 .*?(?="\s*\))', line)
        if match:
            values.append(match[0])
    if len(values) != 89 or 'label=complete' not in values[-1]:
        raise ValueError('incomplete original public output')
    return values


def original(spec, archive):
    validate(spec)
    for path, expected in spec['archive_pins'].items():
        candidate = (archive / path).resolve()
        if not candidate.is_relative_to(archive.resolve()) or digest(candidate) != expected:
            raise ValueError('original archive input differs: ' + path)
    physical, settling, public = None, None, None
    for name in spec['captures'] + [spec['control']]:
        path = archive / name
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        meta, footer = rows[0], rows[-1]
        mode = 'control' if name == spec['control'] else 'observe'
        if (meta['event'] != 'metadata' or meta['sha256'] != SHA or not meta['owned'] or
                meta['task'] != 'FORM-01.3/payoff207' or meta['mode'] != mode or
                footer['event'] != 'preload-file' or footer['complete'] is not True or footer['markers'] != 89):
            raise ValueError('incomplete/unowned original capture')
        source_root = path.parent / 'sources'
        for source, expected in meta['source_sha256'].items():
            if source == 'facing207_observer.js' and mode == 'control':
                continue  # Historical source was not installed in this control.
            source_path = path.parent / 'Facing207.w3m' if source == 'map' else source_root / source
            if digest(source_path) != expected:
                raise ValueError('capture source differs: ' + source)
        preload = path.with_name(path.stem + '-preload.txt')
        if digest(preload) != footer['sha256']:
            raise ValueError('original Preload file differs')
        current = markers(preload)
        if public is not None and current != public:
            raise ValueError('observed output differs from repeat/control')
        public = current
        if mode == 'control':
            if any(row['event'] in ('installed', 'trace-end', 'owner-begin') for row in rows):
                raise ValueError('control installed observer')
            continue
        trace = rows[-2]
        if trace['event'] != 'trace-end' or not trace['installed']:
            raise ValueError('observer did not finish')
        counts = {}
        for row in rows:
            if row['event'] in trace['counts']:
                counts[row['event']] = counts.get(row['event'], 0) + 1
        if counts != trace['counts'] or counts['visual-begin'] != 381 or counts['owner-begin'] != 305:
            raise ValueError('dropped original visits')
        if [row['value'] for row in rows if row['event'] == 'marker'] != public:
            raise ValueError('observer/Preload output differs')
        p, v = extract(path), visual(path)
        if (words_digest(p) != spec['physical_sha256'] or words_digest(v) != spec['visual_sha256'] or
                (physical is not None and (p != physical or v != settling))):
            raise ValueError('original intermediate words differ')
        if (render(path) != (ROOT / 'games/warcraft-3/game/tests/retail_facing207.h').read_text() or
                render_visual(path) != (ROOT / 'games/warcraft-3/game/tests/retail_facing_visual207.h').read_text()):
            raise ValueError('engine fixture differs from original capture')
        physical, settling = p, v
    return dict(physical_visits=len(physical), visual_visits=sum(map(len, settling)), public_markers=len(public))


def engine_totals(log, code):
    found = re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===', log)
    if code or len(found) != 1:
        raise ValueError('engine run did not complete once')
    passed, total, cases = map(int, found[0])
    if passed != total or passed < 14000 or cases != 3:
        raise ValueError('empty/incomplete/failed angular run')
    return passed


def verify(a):
    spec = json.loads(a.fixture.read_text())
    if digest(a.binary) != SHA or digest(a.binary.with_name('msvcr120.dll')) != spec['build']['crt_sha256']:
        raise ValueError('retail executable/CRT differs')
    evidence = original(spec, a.archive)
    work = a.report.with_suffix('.work')
    work.mkdir(parents=True, exist_ok=False)
    editions = {}
    for edition in ('classic', 'tft'):
        log, junit = work / (edition + '.log'), work / (edition + '.xml')
        command = [str(a.test_binary.resolve()), '-data', str(a.data.resolve())]
        if edition == 'tft':
            command += ['-tft']
        command += ['+dedicated', '1', '+test', 'wc3_facing.*']
        with log.open('w') as out:
            child = subprocess.run(command, cwd=ROOT, env=dict(os.environ, TEST_JUNIT=str(junit)),
                                   stdout=out, stderr=subprocess.STDOUT, timeout=300)
        count = engine_totals(log.read_text(), child.returncode)
        suite = ET.parse(junit).getroot()
        if (suite.attrib['tests'] != '3' or suite.attrib['failures'] != '0' or
                suite.attrib['errors'] != '0' or suite.attrib['skipped'] != '0' or
                int(suite.attrib['assertions']) != count or any(list(row) for row in suite.findall('testcase')) or
                {row.attrib['name'] for row in suite.findall('testcase')} != set(spec['engine_tests'])):
            raise ValueError('engine identities/counters differ')
        editions[edition] = dict(assertions=count, command=command, log_sha256=digest(log), junit_sha256=digest(junit))
    return dict(passed=True, **evidence, engine_editions=2, engine=editions,
                binary_sha256=SHA, fixture_sha256=digest(a.fixture),
                test_binary_sha256=digest(a.test_binary),
                game_library_sha256=digest(a.test_binary.parent.parent / 'lib/libgame-wc3-test.so'),
                exclusions=spec['exclusions'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--archive', type=Path, required=True)
    p.add_argument('--fixture', type=Path, default=FIXTURE)
    p.add_argument('--test-binary', type=Path, default=ROOT / 'build/bin/openwarcraft3-tests')
    p.add_argument('--data', type=Path, default=ROOT / 'build/tests')
    p.add_argument('--report', type=Path, required=True)
    a = p.parse_args()
    if a.report.exists():
        p.error('report must be new')
    result = verify(a)
    a.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
