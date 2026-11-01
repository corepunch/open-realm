#!/usr/bin/env python3
"""Run exactly one full native299 C replay with private retail MPQs.

The original install is read only: only its MPQ files are linked into a temporary
writable overlay. The accepted map is copied there. No original game is launched.
A fresh log and JUnit report are required; zero/skipped tests cannot pass.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

MAP_SHA256 = '9e3690734d1211998fc85cac2630219639170c94b6b6fafaf4e58ab3b3d33f1f'
TEST = 'stress_wc3_attack_recovery299.full_public_and_ordered_motion'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_report(path):
    root = ET.parse(path).getroot()
    cases = list(root.iter('testcase'))
    if len(cases) != 1:
        raise ValueError('C299 must execute exactly one test')
    case = cases[0]
    name = case.get('name', '')
    full_name = case.get('classname', '') + '.' + name
    if TEST not in (name, full_name):
        raise ValueError('JUnit contains a different test: ' + full_name)
    if next(root.iter('skipped'), None) is not None:
        raise ValueError('C299 was skipped')
    for suite in root.iter('testsuite'):
        if int(suite.get('tests', '0')) != 1:
            raise ValueError('JUnit test count is not one')
        if int(suite.get('failures', '0')) or int(suite.get('errors', '0')):
            raise ValueError('C299 failed')
    if next(root.iter('failure'), None) is not None or next(root.iter('error'), None) is not None:
        raise ValueError('C299 failed')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--map', type=Path, required=True)
    ap.add_argument('--data', type=Path, required=True, help='Read-only retail installation containing MPQs')
    ap.add_argument('--log', type=Path, required=True)
    ap.add_argument('--timeout', type=int, default=600)
    args = ap.parse_args()
    binary, native_map, data, log = [p.resolve() for p in (args.binary, args.map, args.data, args.log)]
    if sha(native_map) != MAP_SHA256:
        ap.error('wrong native299 map SHA256')
    archives = sorted(p for p in data.iterdir() if p.is_file() and p.suffix.lower() == '.mpq')
    if not archives:
        ap.error('retail MPQs missing')
    report = log.with_suffix('.xml')
    summary = log.with_suffix('.summary.json')
    if any(p.exists() for p in (log, report, summary)):
        ap.error('log, JUnit, and summary paths must be fresh')
    log.parent.mkdir(parents=True, exist_ok=True)
    before = sha(binary)
    env = os.environ.copy()
    env.pop('TEST_SHARD', None)
    env.update(WC3_ATTACK299_MAP='Maps/AttackRecovery299.w3m', TEST_JUNIT=str(report),
               TEST_JUNIT_SUITE='native299-full-public-motion')
    error = None
    code = None
    command = []
    try:
        # The engine recursively discovers archives beneath -data. Keeping the
        # overlay below build/tests would contaminate concurrent fixture runs.
        with tempfile.TemporaryDirectory(prefix='native299-data-') as directory:
            overlay = Path(directory)
            for archive in archives:
                (overlay / archive.name).symlink_to(archive)
            (overlay / 'Maps').mkdir()
            shutil.copyfile(native_map, overlay / 'Maps/AttackRecovery299.w3m')
            command = [str(binary), '-data', str(overlay), '+dedicated', '1', '+test', TEST]
            with log.open('x') as output:
                output.write('COMMAND ' + repr(command) + '\n')
                output.flush()
                code = subprocess.run(command, env=env, stdout=output,
                                      stderr=subprocess.STDOUT, timeout=args.timeout).returncode
        if code:
            raise ValueError('engine exited with code ' + str(code))
        if not report.is_file():
            raise ValueError('C299 did not produce a fresh execution report')
        validate_report(report)
        if sha(binary) != before:
            raise ValueError('test binary changed during execution')
    except (ValueError, OSError, ET.ParseError, subprocess.TimeoutExpired) as exc:
        error = str(exc)
    summary.write_text(json.dumps({'test': TEST, 'passed': error is None,
        'error': error, 'exit_code': code, 'command': command,
        'map_sha256': MAP_SHA256, 'binary_sha256': before,
        'retail_data': str(data), 'archives': [p.name for p in archives],
        'junit': str(report), 'log': str(log)}, indent=2) + '\n')
    if error:
        raise SystemExit(error)


if __name__ == '__main__':
    main()
