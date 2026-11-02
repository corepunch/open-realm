#!/usr/bin/env python3
"""Run a full frozen public protocol (331 by default) using read-only retail MPQs.

Exports expected rows only through the checked native evidence bundle. Uses TFT
and a temporary writable overlay outside build/tests; never launches retail.
C comparison adapts only code= FourCC integer byte order, as in C299. No saved
mode is offered until a corresponding engine test exists.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
SPEC = ROOT / 'tools/ghidra/fixtures/research/invisibility331-public-original.json'
CHECKER = Path(__file__).with_name('invisibility331_capture_check.py')
TEST = 'stress_wc3_invisibility331.full_public_protocol'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_report(path, expected=TEST):
    root = ET.parse(path).getroot()
    cases = list(root.iter('testcase'))
    if len(cases) != 1:
        raise ValueError('C331 must execute exactly one test')
    case = cases[0]
    name = case.get('name', '')
    full_name = case.get('classname', '') + '.' + name
    if expected not in (name, full_name):
        raise ValueError('JUnit contains a different test: ' + full_name)
    if next(root.iter('skipped'), None) is not None:
        raise ValueError('C331 was skipped')
    for suite in root.iter('testsuite'):
        if int(suite.get('tests', '0')) != 1:
            raise ValueError('JUnit test count is not one')
        if int(suite.get('failures', '0')) or int(suite.get('errors', '0')):
            raise ValueError('C331 failed')
    if next(root.iter('failure'), None) is not None or next(root.iter('error'), None) is not None:
        raise ValueError('C331 failed')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--map', type=Path, required=True)
    ap.add_argument('--data', type=Path, required=True, help='Read-only retail installation containing MPQs')
    ap.add_argument('--log', type=Path, required=True)
    ap.add_argument('--timeout', type=int, default=600)
    ap.add_argument('--reference', type=Path, default=SPEC, help='Accepted public protocol specification')
    ap.add_argument('--capture-checker', type=Path, default=CHECKER, help='Matching native capture verifier/exporter')
    args = ap.parse_args()
    test = TEST
    reference, checker = args.reference.resolve(), args.capture_checker.resolve()
    reference_before, checker_before = sha(reference), sha(checker)
    spec = json.loads(reference.read_text())
    expected_count = spec['public_records']
    MAP_SHA256 = spec['map_sha256']
    if type(expected_count) is not int or expected_count <= 0:
        ap.error('native public record count must be a positive integer')
    binary, native_map, data, log = [p.resolve() for p in (args.binary, args.map, args.data, args.log)]
    if sha(native_map) != MAP_SHA256:
        ap.error('wrong native331 map SHA256')
    archives = sorted(p for p in data.iterdir() if p.is_file() and p.suffix.lower() == '.mpq')
    if not archives:
        ap.error('retail MPQs missing')
    report = log.with_suffix('.xml')
    summary = log.with_suffix('.summary.json')
    if any(p.exists() for p in (log, report, summary)):
        ap.error('log, JUnit, and summary paths must be fresh')
    log.parent.mkdir(parents=True, exist_ok=True)
    before = sha(binary)
    library = binary.parent.parent / 'lib/libgame-wc3-test.so'
    if not library.is_file():
        ap.error('matching test game module missing: ' + str(library))
    library_before = sha(library)
    source_paths = sorted(p for p in (ROOT / 'games/warcraft-3/game').rglob('*')
                          if p.is_file() and p.suffix in ('.c', '.h'))
    source_before = {str(p.relative_to(ROOT)): sha(p) for p in source_paths}
    env = os.environ.copy()
    env.pop('TEST_SHARD', None)
    env['LD_LIBRARY_PATH'] = str(library.parent) + os.pathsep + env.get('LD_LIBRARY_PATH', '')
    env.update(WC3_INVISIBILITY331_MAP='Maps/Invisibility331.w3m', TEST_JUNIT=str(report),
               TEST_JUNIT_SUITE='native-public-protocol')
    error = None
    code = None
    command = []
    try:
        # The engine recursively discovers archives beneath -data. Keeping the
        # overlay below build/tests would contaminate concurrent fixture runs.
        with tempfile.TemporaryDirectory(prefix='native331-data-') as directory:
            overlay = Path(directory)
            if overlay.is_relative_to(ROOT / 'build/tests'):
                raise ValueError('retail overlay must be outside build/tests')
            expected = overlay / 'native-public.txt'
            subprocess.run([sys.executable, str(checker), '--spec', str(reference), '--check', '--write-public', str(expected)],
                           check=True, capture_output=True, text=True)
            if sha(expected) != spec['public_sha256'] or len(expected.read_text().splitlines()) != expected_count:
                raise ValueError('exported native stream mismatch')
            env['WC3_INVISIBILITY331_EXPECTED'] = str(expected)
            for archive in archives:
                (overlay / archive.name).symlink_to(archive)
            (overlay / 'Maps').mkdir()
            shutil.copyfile(native_map, overlay / 'Maps/Invisibility331.w3m')
            command = [str(binary), '-data', str(overlay), '-tft', '+dedicated', '1', '+test', test]
            with log.open('x') as output:
                output.write('COMMAND ' + repr(command) + '\n')
                output.flush()
                code = subprocess.run(command, env=env, stdout=output,
                                      stderr=subprocess.STDOUT, timeout=args.timeout).returncode
        if code:
            raise ValueError('engine exited with code ' + str(code))
        if not report.is_file():
            raise ValueError('C331 did not produce a fresh execution report')
        validate_report(report, test)
        suites = list(ET.parse(report).getroot().iter('testsuite'))
        if not suites or any(int(s.get('assertions', '0')) != 7 for s in suites):
            raise ValueError('C331 must complete exactly seven assertions')
        contents = log.read_text()
        counts = re.findall(r'INVISIBILITY331 COUNTS public=(\d+) expected=(\d+) mismatches=(\d+) complete=(\d+)', contents)
        if counts != [(str(expected_count),str(expected_count),'0','1')]:
            raise ValueError('missing or mismatching complete public protocol')
        if re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===', contents) != [('7','7','1')]:
            raise ValueError('stdout did not confirm one seven-assertion test')
        if sha(binary) != before:
            raise ValueError('test binary changed during execution')
    except (ValueError, OSError, ET.ParseError, subprocess.TimeoutExpired, subprocess.CalledProcessError) as exc:
        error = str(exc)
    if sha(binary) != before:
        error = (error + '; ' if error else '') + 'test binary changed during execution'
    try:
        if sha(reference) != reference_before or sha(checker) != checker_before:
            raise ValueError('reference or capture checker changed during execution')
        library_after = sha(library)
        source_after = {str(p.relative_to(ROOT)): sha(p) for p in
                        sorted((ROOT / 'games/warcraft-3/game').rglob('*'))
                        if p.is_file() and p.suffix in ('.c', '.h')}
        if library_after != library_before or source_after != source_before:
            raise ValueError('game module or C/header sources changed during execution')
    except (OSError, ValueError) as exc:
        error = (error + '; ' if error else '') + str(exc)
    summary.write_text(json.dumps({'test': test, 'mode': 'normal', 'passed': error is None,
        'error': error, 'exit_code': code, 'command': command,
        'map_sha256': MAP_SHA256, 'public_sha256': spec['public_sha256'], 'public_records': expected_count,
        'reference_path': str(reference), 'reference_sha256': reference_before,
        'capture_checker_path': str(checker), 'capture_checker_sha256': checker_before,
        'comparison_adapter': 'C code= FourCC byte-order only; no other normalization', 'binary_sha256': before, 'edition': 'tft',
        'library_path': str(library), 'library_sha256': library_before,
        'source_sha256': source_before, 'source_file_count': len(source_before),
        'retail_data': str(data), 'archives': [p.name for p in archives],
        'junit': str(report), 'log': str(log)}, indent=2) + '\n')
    if error:
        raise SystemExit(error)


if __name__ == '__main__':
    main()
