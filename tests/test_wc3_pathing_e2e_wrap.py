"""Combined wrap acceptance retains real evidence and nonempty engine runs."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
from research.e2e212_contract import validate,BOUNDARIES,EVIDENCE
from verify_wc3_pathing_e2e_wrap import FIXTURE
from verify_wc3_pathing_e2e_variants import engine_totals


class WrapBaselineTests(unittest.TestCase):
    def setUp(self):
        self.spec=json.loads(FIXTURE.read_text())
        self.manifest=json.loads((ROOT/'tools/ghidra/fixtures/retail-pathfinding-corpus-1.27.json').read_text())

    def test_complete_original_binding_inventory(self):
        self.assertEqual(len(validate(self.spec,self.manifest)),10)

    def test_every_literal_source_pin_is_required(self):
        for path in self.spec['pins']:
            spec=copy.deepcopy(self.spec);spec['pins'][path]='0'*64
            with self.assertRaises(ValueError):validate(spec,self.manifest)

    def test_each_category_and_completion_are_required(self):
        for index in range(4):
            for field in ('test','scope','completion'):
                spec=copy.deepcopy(self.spec);spec['categories'][index][field]=''
                with self.assertRaises(ValueError):validate(spec,self.manifest)
        spec=copy.deepcopy(self.spec);spec['categories'].pop()
        with self.assertRaises(ValueError):validate(spec,self.manifest)

    def test_weakened_wrap_contract_is_rejected(self):
        for category in self.spec['categories']:
            for identity in category['entries']:
                manifest=copy.deepcopy(self.manifest)
                next(e for e in manifest['entries']if e['id']==identity)['checks']={'passed':{'equal':True}}
                with self.assertRaises(ValueError):validate(self.spec,manifest)

    def test_each_forced_boundary_is_required(self):
        for key in BOUNDARIES:
            spec=copy.deepcopy(self.spec);spec['boundaries'][key]^=1
            with self.assertRaises(ValueError):validate(spec,self.manifest)

    def test_forced_and_live_evidence_cannot_be_interchanged(self):
        for identity in EVIDENCE:
            spec=copy.deepcopy(self.spec);spec['evidence_by_id'][identity]='L' if EVIDENCE[identity]=='O' else 'O'
            with self.assertRaises(ValueError):validate(spec,self.manifest)
            manifest=copy.deepcopy(self.manifest)
            next(e for e in manifest['entries']if e['id']==identity)['evidence']=[]
            with self.assertRaises(ValueError):validate(self.spec,manifest)

    def test_engine_empty_failed_duplicate_or_partial_categories_are_rejected(self):
        good='=== 10000/10000 assertions passed in 4 test(s) ==='
        self.assertEqual(engine_totals(good,0,4),10000)
        for bad in ('',good+good,good.replace('4 test','3 test'),good.replace('10000/10000','9999/10000'),
                    '=== 0/0 assertions passed in 4 test(s) ==='):
            with self.assertRaises(ValueError):engine_totals(bad,0,4)
        with self.assertRaises(ValueError):engine_totals(good,1,4)


if __name__=='__main__':unittest.main()
