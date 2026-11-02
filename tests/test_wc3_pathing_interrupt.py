import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('pathing_interrupt209',ROOT/'tools/ghidra/verify_wc3_pathing_interrupt.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)


class PathingInterruptEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.spec=json.loads(module.FIXTURE.read_text());self.rows=module.contract(self.spec)

    def test_complete_original_timeline_and_engine_producer(self):
        self.assertEqual(module.validate(self.rows),1140)
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_interrupt209_scene.h').read_text(),
                         module.scene(ROOT/'tools/frida/research/interrupt209_probe.j'))

    def test_missing_boundary_rejected(self):
        self.rows.pop(0)
        with self.assertRaises(ValueError):module.validate(self.rows)

    def test_pending_head_must_remain_suspended(self):
        row=next(r for r in self.rows if r['event']=='append-order-end'and r['c']==1140)
        row['unit']['count']=0
        with self.assertRaises(ValueError):module.validate(self.rows)

    def test_pending_order_cannot_dispatch(self):
        identity=next(r['unit']['id']for r in self.rows if r['event']=='remove-tasks-begin')
        row=next(r for r in self.rows if r['event']=='dispatch-order-begin'and r['c']==1140)
        row['unit']['id']=identity
        with self.assertRaises(ValueError):module.validate(self.rows)

    def test_release_cannot_run_inside_completion(self):
        identity=next(r['unit']['id']for r in self.rows if r['event']=='remove-tasks-begin')
        row=next(r for r in self.rows if r['event']=='destroy-wrapper'and r['id']==identity)
        row['insideOwner']=True
        with self.assertRaises(ValueError):module.validate(self.rows)

    def test_duplicate_release_rejected(self):
        destroyed=[r for r in self.rows if r['event']=='destroy-wrapper']
        destroyed[-1]['id']=destroyed[0]['id'][:]
        with self.assertRaises(ValueError):module.validate(self.rows)

    def test_successor_cannot_join_current_owner_frontier(self):
        new=next(r['groups'][0]['id']for r in self.rows if r['event']=='marker'and 'channel-after-mutation'in r['value'])
        row=next(r for r in self.rows if r['event']=='group-begin'and r['c']==1140)
        row['group']['id']=new
        with self.assertRaises(ValueError):module.validate(self.rows)

    def test_engine_empty_failure_and_return_code_rejected(self):
        self.assertEqual(module.engine_total('=== 111/111 assertions passed in 2 test(s) ===',0),111)
        for text,code in [('=== 0/0 assertions passed in 0 test(s) ===',0),
                          ('=== 110/111 assertions passed in 2 test(s), 1 failed ===',0),
                          ('=== 111/111 assertions passed in 2 test(s) ===',1)]:
            with self.assertRaises(ValueError):module.engine_total(text,code)

    def test_portable_expectation_pin_cannot_be_replaced(self):
        wrong=copy.deepcopy(self.spec);wrong['timeline_sha256']='0'*64
        with self.assertRaises(ValueError):module.contract(wrong)


if __name__=='__main__':unittest.main()
