"""Combined order acceptance retains real evidence and nonempty engine runs."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
from research.e2e210_contract import validate,scheduler
from verify_wc3_pathing_e2e_orders import FIXTURE
from verify_wc3_pathing_e2e_variants import engine_totals


class OrderBaselineTests(unittest.TestCase):
    def setUp(self):
        self.spec=json.loads(FIXTURE.read_text())
        self.manifest=json.loads((ROOT/'tools/ghidra/fixtures/retail-pathfinding-corpus-1.27.json').read_text())

    def test_complete_original_binding_inventory(self):
        self.assertEqual(len(validate(self.spec,self.manifest)),3)

    def test_every_literal_source_pin_is_required(self):
        for path in self.spec['pins']:
            spec=copy.deepcopy(self.spec);spec['pins'][path]='0'*64
            with self.assertRaises(ValueError):validate(spec,self.manifest)

    def test_each_category_and_completion_are_required(self):
        for index in range(3):
            for field in ('test','scope','completion'):
                spec=copy.deepcopy(self.spec);spec['categories'][index][field]=''
                with self.assertRaises(ValueError):validate(spec,self.manifest)
        spec=copy.deepcopy(self.spec);spec['categories'].pop()
        with self.assertRaises(ValueError):validate(spec,self.manifest)

    def test_weakened_order_contract_is_rejected(self):
        for category in self.spec['categories']:
            for identity in category['entries']:
                manifest=copy.deepcopy(self.manifest)
                next(e for e in manifest['entries']if e['id']==identity)['checks']={'passed':{'equal':True}}
                with self.assertRaises(ValueError):validate(self.spec,manifest)

    def test_scheduling_repeat_cannot_be_omitted_or_replaced(self):
        for operation in ('omit','replace'):
            spec=copy.deepcopy(self.spec);key=next(iter(spec['scheduler_archives']))
            if operation=='omit':spec['scheduler_archives'].pop(key)
            else:spec['scheduler_archives'][key]='0'*64
            with self.assertRaises(ValueError):validate(spec,self.manifest)

    def test_empty_or_foreign_archive_is_rejected(self):
        with tempfile.TemporaryDirectory()as tmp:
            with self.assertRaises(FileNotFoundError):scheduler(self.spec,Path(tmp))
            p=Path(tmp)/next(iter(self.spec['scheduler_archives']));p.parent.mkdir();p.write_text('{}\n')
            with self.assertRaises(ValueError):scheduler(self.spec,Path(tmp))

    def test_engine_empty_failed_duplicate_or_partial_categories_are_rejected(self):
        good='=== 10000/10000 assertions passed in 3 test(s) ==='
        self.assertEqual(engine_totals(good,0,3),10000)
        for bad in ('',good+good,good.replace('3 test','2 test'),good.replace('10000/10000','9999/10000'),
                    '=== 0/0 assertions passed in 3 test(s) ==='):
            with self.assertRaises(ValueError):engine_totals(bad,0,3)
        with self.assertRaises(ValueError):engine_totals(good,1,3)


if __name__=='__main__':unittest.main()
