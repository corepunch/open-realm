import copy
import importlib.util
import json
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('shared_growth',ROOT/'tools/ghidra/verify_wc3_pathing_shared_growth.py')
oracle=importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


class SharedGrowth(unittest.TestCase):
    def setUp(self):
        self.report=json.loads((ROOT/'tools/ghidra/fixtures/retail-shared-growth189-1.27.json').read_text())

    def test_complete_native_growth_and_mutation(self):
        self.assertIs(oracle.validate_report(self.report),self.report)

    def test_boundaries_and_reuse_are_required(self):
        for field,value in [('owners',128),('growth_allocations',2),('reuse_allocations',1),('final_live',1)]:
            report=copy.deepcopy(self.report);report[field]=value
            with self.assertRaises(ValueError):oracle.validate_report(report)

    def test_largest_departure_is_required(self):
        self.report['publication_phases'][-1]['published'][-1]=self.report['publication_phases'][1]['published'][-1]
        with self.assertRaises(ValueError):oracle.validate_report(self.report)

    def test_reference_and_publication_order_are_required(self):
        for field,value in [('references',1),('published',[0x7f7fffff,0x3f000000,0])]:
            report=copy.deepcopy(self.report);report['publication_phases'][0][field]=value
            with self.assertRaises(ValueError):oracle.validate_report(report)


if __name__=='__main__':unittest.main()
