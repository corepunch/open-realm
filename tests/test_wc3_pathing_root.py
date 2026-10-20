import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('pathing_root211',ROOT/'tools/ghidra/verify_wc3_pathing_root.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)


class RootEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.spec=json.loads(module.FIXTURE.read_text());self.rows=module.contract(self.spec)

    def test_complete_timeline_and_frozen_engine_words(self):
        self.assertEqual(module.validate(self.rows),57)
        self.assertEqual(len(self.rows),4904)

    def test_missing_physical_owner_rejected(self):
        self.rows.pop(next(i for i,r in enumerate(self.rows)if r['event']=='owner-begin'))
        with self.assertRaises(ValueError):module.validate(self.rows)

    def test_authored_heading_cannot_be_replaced_by_round_number(self):
        next(r for r in self.rows if r['event']=='prepend-facing')['heading']=0x4096cbe4
        with self.assertRaises(ValueError):module.validate(self.rows)

    def test_turn_scalar_cannot_use_unit_authored_turn_rate(self):
        next(r for r in self.rows if r['event']=='turn-request')['turn']=0x3eccccce
        with self.assertRaises(ValueError):module.validate(self.rows)

    def test_distant_facing_cannot_start_before_approach_arrival(self):
        [r for r in self.rows if r['event']=='facing-task'][-1]['tick']=50
        with self.assertRaises(ValueError):module.validate(self.rows)

    def test_final_visual_heading_cannot_snap_to_root_angle(self):
        for r in self.rows:
            if r['event']=='marker'and 'tick=260 label=unit2'in r['value']:
                r['value']=r['value'].replace('239.015','250.000')
        with self.assertRaises(ValueError):module.validate(self.rows)

    def test_empty_failed_and_partial_engine_reports_rejected(self):
        self.assertEqual(module.engine_total('=== 3030/3030 assertions passed in 19 test(s) ===',0),3030)
        for text,code in [('=== 0/0 assertions passed in 0 test(s) ===',0),
                          ('=== 3029/3030 assertions passed in 19 test(s), 1 failed ===',0),
                          ('=== 3030/3030 assertions passed in 19 test(s) ===',1)]:
            with self.assertRaises(ValueError):module.engine_total(text,code)


if __name__=='__main__':unittest.main()
