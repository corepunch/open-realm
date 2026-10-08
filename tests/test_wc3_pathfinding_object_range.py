"""Keep the original executable's range outputs coupled to the game regression."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
spec=importlib.util.spec_from_file_location('object_range',ROOT/'tools/ghidra/verify_wc3_pathing_range.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-object-range184-1.27.json'
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_object_range184.h'


class ObjectRangeEvidenceTests(unittest.TestCase):
    def setUp(self):self.expected=json.loads(FIXTURE.read_text())

    def test_engine_header_contains_all_original_cases(self):
        module.check_object_export(FIXTURE,HEADER,self.expected)
        counts={}
        for row in self.expected['cases']:
            counts[row['category']]=counts.get(row['category'],0)+1
        self.assertEqual(counts,dict(ordinary=600,adjacent_boundary=144,prediction=36,
                                     same_identity=18,minimum_clamp=24))

    def test_changed_result_or_missing_case_is_rejected(self):
        for mutation in ('result','missing','input'):
            changed=copy.deepcopy(self.expected)
            if mutation=='result':changed['cases'][0]['accepted']^=1
            elif mutation=='missing':changed['cases'].pop()
            else:changed['cases'][0]['reach']^=1
            with tempfile.TemporaryDirectory() as directory:
                path=Path(directory)/'range.json';path.write_text(json.dumps(changed))
                with self.subTest(mutation=mutation),self.assertRaises(ValueError):
                    module.check_object_export(path,HEADER,self.expected)

    def test_changed_engine_header_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'range.h';path.write_text(HEADER.read_text().replace('0x00000000u','0x00000001u',1))
            with self.assertRaises(ValueError):module.check_object_export(FIXTURE,path,self.expected)


if __name__=='__main__':unittest.main()
