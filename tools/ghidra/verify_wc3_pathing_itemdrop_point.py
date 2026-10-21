#!/usr/bin/env python3
"""Verify recovered Inventory point parameters, task lifetime and native admission."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ghidra/research'))
import itemdrop_evidence as native
import itemdrop_motion_fixture as motion
from verify_wc3_pathing_target_normalize218 import original_bytes
from wc3_pathfinding_engine_tests import run_engine

SPEC = ROOT / 'tools/ghidra/fixtures/retail-itemdrop-point-1.27.json'
PATTERNS = ['wc3_items.*', 'wc3_cursor.*']
EXPECTED_ENGINE = {edition: dict(tests=133, assertions=11432) for edition in ('classic', 'tft')}
EXPECTED_NATIVE = dict(observations=8, controls=4, captures=12, public_markers=5340,
                       motion_cases=14, mover_writes=441, instruction_ranges=8)


def require(value, reason):
    if not value:
        raise ValueError(reason)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(spec):
    require(spec['version'] == 1 and spec['task'] == 'TARGET-01.2' and
            spec['binary_sha256'] == native.SHA and spec['patterns'] == PATTERNS and
            spec['native_expected'] == EXPECTED_NATIVE and spec['engine_expected'] == EXPECTED_ENGINE, 'wrong point-drop evidence contract')
    core = {'games/warcraft-3/game/skills/s_inventory.c', 'games/warcraft-3/game/skills/s_move.c',
            'games/warcraft-3/game/g_items.c', 'games/warcraft-3/game/g_local.h',
            'games/warcraft-3/game/api/api_unit.h', 'games/warcraft-3/tests/resources-src/Units/AbilityData.slk',
            'games/warcraft-3/game/m_unit.c',
            'games/warcraft-3/game/g_save.c', 'games/warcraft-3/game/tests/t_itemdrop_point.c',
            'games/warcraft-3/game/tests/fixtures/retail_itemdrop_motion.h'}
    require(core <= set(spec['pins']), 'missing point-drop source pins')
    for name, expected in spec['pins'].items():
        require(digest(ROOT / name) == expected, 'point-drop source changed: ' + name)
    point = native.audit()
    admission = native.audit_admission()
    immobile = native.audit_admission(immobile=True)
    require(point['public_markers'] == 3702 and admission['public_markers'] == immobile['public_markers'] == 819,
            'missing original range/admission/immobile coverage')
    header = ROOT / 'games/warcraft-3/game/tests/fixtures/retail_itemdrop_motion.h'
    text = header.read_text()
    require(text == motion.render() and len(re.findall(r'^    \{0x', text, re.M)) == 441 and
            len(re.findall(r'^    \{[01],\d+,\d+,\{', text, re.M)) == 14,
            'literal original-clock mover fixture differs')
    data = json.loads(gzip.decompress(native.ADMISSION_BUNDLE.read_bytes()))
    guards = {f'{0x6f000000 + int(address, 16):08x}': raw
              for address, raw in data['instruction_guards'].items()}
    require(len(guards) == 8, 'incomplete original admission instruction guards')
    return dict(point=point, admission=admission, immobile=immobile), guards


def check_engine(engine, spec, report):
    require(engine == spec['engine_expected'], 'point-drop engine totals differ')
    required = set(spec['required_engine_cases'])
    require(required and any('original_clock' in name for name in required) and
            any('inventory_drag' in name for name in required), 'missing exact-motion/client ingress cases')
    for edition in ('classic', 'tft'):
        seen = set()
        for index in range(len(PATTERNS)):
            path = report.with_name(report.stem + '-' + edition + '-' + str(index) + '.xml')
            suite = ET.parse(path).getroot()
            seen.update(row.attrib['name'] for row in suite.findall('testcase'))
        require(required <= seen, 'required point-drop engine cases were not executed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--test-binary', type=Path, default=ROOT / 'build/bin/openwarcraft3-tests')
    parser.add_argument('--data', type=Path, default=ROOT / 'build/tests')
    args = parser.parse_args()
    require(not args.report.exists(), 'fresh point-drop report required')
    for edition in ('classic', 'tft'):
        for index in range(len(PATTERNS)):
            for suffix in ('.log', '.xml'):
                path = args.report.with_name(args.report.stem + '-' + edition + '-' + str(index) + suffix)
                require(not path.exists(), 'stale point-drop child report exists')
    spec = json.loads(SPEC.read_text())
    retail, guards = validate(spec)
    original_bytes(args.binary, guards)
    engine = run_engine(args.test_binary, args.data, args.report, PATTERNS, timeout=300)
    check_engine(engine, spec, args.report)
    result = dict(passed=True, status='verified', binary_sha256=native.SHA,
                  **EXPECTED_NATIVE, retail=retail, engine=engine, engine_checked=True,
                  test_binary_sha256=digest(args.test_binary),
                  game_library_sha256=digest(args.test_binary.parent.parent / 'lib/libgame-wc3-test.so'),
                  remaining_owner='TARGET-01.2', scope=spec['scope'])
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
