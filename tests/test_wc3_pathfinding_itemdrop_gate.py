"""Require actual motion, cursor and both edition results for point-drop acceptance."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/ghidra'))
import verify_wc3_pathing_itemdrop_point as gate


class ItemDropGateTests(unittest.TestCase):
    def setUp(self):
        self.spec = json.loads(gate.SPEC.read_text())
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.report = Path(self.temp.name) / 'report.json'
        for edition in ('classic', 'tft'):
            for index in range(2):
                suite = ET.Element('testsuite')
                if index == 0:
                    for name in self.spec['required_engine_cases']:
                        ET.SubElement(suite, 'testcase', name=name)
                ET.ElementTree(suite).write(self.report.with_name(f'report-{edition}-{index}.xml'))

    def test_current_contract_and_complete_execution(self):
        native, guards = gate.validate(self.spec)
        self.assertEqual(len(guards), 8)
        self.assertFalse(native['point']['engine_parity'])
        gate.check_engine(gate.EXPECTED_ENGINE, self.spec, self.report)

    def test_changed_engine_totals_rejected(self):
        engine = copy.deepcopy(gate.EXPECTED_ENGINE)
        engine['classic']['tests'] = 0
        with self.assertRaisesRegex(ValueError, 'engine totals differ'):
            gate.check_engine(engine, self.spec, self.report)

    def test_missing_actual_motion_execution_rejected(self):
        name = next(n for n in self.spec['required_engine_cases'] if 'original_clock' in n)
        path = self.report.with_name('report-tft-0.xml')
        tree = ET.parse(path)
        tree.getroot().remove(next(e for e in tree.getroot() if e.attrib['name'] == name))
        tree.write(path)
        with self.assertRaisesRegex(ValueError, 'were not executed'):
            gate.check_engine(gate.EXPECTED_ENGINE, self.spec, self.report)

    def test_unpinned_implementation_rejected(self):
        self.spec['pins'].pop('games/warcraft-3/game/skills/s_inventory.c')
        with self.assertRaisesRegex(ValueError, 'missing point-drop source pins'):
            gate.validate(self.spec)

    def test_reduced_expected_totals_rejected(self):
        self.spec['engine_expected']['classic']['assertions'] -= 1
        with self.assertRaisesRegex(ValueError, 'wrong point-drop evidence contract'):
            gate.validate(self.spec)

    def test_cursor_ingress_cannot_be_removed_from_contract(self):
        self.spec['required_engine_cases'] = [n for n in self.spec['required_engine_cases'] if 'inventory_drag' not in n]
        with self.assertRaisesRegex(ValueError, 'missing exact-motion/client ingress'):
            gate.check_engine(gate.EXPECTED_ENGINE, self.spec, self.report)
