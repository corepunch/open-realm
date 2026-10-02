"""Blocked public Move retains its click, retry state and final angular gate."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_blocked_goal_trace import verify_contract,render_header


class BlockedGoalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-blocked-goal-1.27.json').read_text())
    def spec(self):return copy.deepcopy(self.fixture)
    def event(self,spec,name):return next(r for r in spec['lifecycle'] if r['event']==name)
    def test_complete_public_journey_is_repeated(self):
        verify_contract(self.fixture)
        self.assertEqual(len(self.fixture['cases']),2)
        self.assertEqual(len(self.fixture['motion']),207)
    def test_failed_budget_must_retain_the_entire_partial_route(self):
        s=self.spec();self.event(s,'route')['truncated']=True
        with self.assertRaises(ValueError):verify_contract(s)
    def test_clicked_goal_cannot_replace_the_adjusted_retry_goal(self):
        s=self.spec();self.event(s,'retry-init')['nativeGoal']=[1126400000,1119289344]
        with self.assertRaises(ValueError):verify_contract(s)
    def test_near_singleton_retry_must_not_draw_random(self):
        s=self.spec();self.event(s,'retry-init')['ownerAfter'][0]^=1
        with self.assertRaises(ValueError):verify_contract(s)
    def test_terminal_retry_keeps_count_one(self):
        s=self.spec();[r for r in s['lifecycle'] if r['event']=='retry-result'][-1]['after']=0
        with self.assertRaises(ValueError):verify_contract(s)
    def test_first_retry_defers_the_next_refill(self):
        s=self.spec();next(r for r in s['lifecycle'] if r['event']=='route-step' and r['counter']==1261)['after'][0]=0
        with self.assertRaises(ValueError):verify_contract(s)
    def test_terminal_retry_preserves_the_fine_index(self):
        s=self.spec();next(r for r in s['lifecycle'] if r['event']=='route-step' and r['counter']==1262)['after'][0]=4294967295
        with self.assertRaises(ValueError):verify_contract(s)
    def test_forced_arrival_requires_the_final_turn(self):
        s=self.spec();[r for r in s['lifecycle'] if r['event']=='arrival-evaluation'][-2]['result']=1
        with self.assertRaises(ValueError):verify_contract(s)
    def test_cant_path_must_remove_the_order_and_task_heads(self):
        s=self.spec();self.event(s,'task-cant-path')['after']['orderHead']=[1261,1268]
        with self.assertRaises(ValueError):verify_contract(s)
    def test_public_order_cannot_remain_active(self):
        s=self.spec();s['markers'][-1]=s['markers'][-1].replace('order=0','order=851986')
        with self.assertRaises(ValueError):verify_contract(s)
    def test_literal_header_is_the_frozen_native_motion(self):
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_blocked_goal.h').read_text(),render_header(self.fixture))


if __name__=='__main__':unittest.main()
