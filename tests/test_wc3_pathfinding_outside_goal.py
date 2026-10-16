"""Outside point tasks and their clipped routing coordinates have separate ownership."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_outside_goal_trace import verify_producer,render_header


class OutsideGoalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-outside-goal-1.27.json').read_text())
    def spec(self,name='west'):return copy.deepcopy(self.fixture['journeys'][name])
    def test_complete_west_and_four_edge_neighbors_are_repeated(self):
        for s in self.fixture['journeys'].values():verify_producer(s)
        self.assertEqual(len(self.fixture['cases']),4)
    def test_world_margin_comes_from_cell_size_times_four(self):
        s=self.spec();s['producer']['bounds'][0]['margin']=1065353216
        with self.assertRaises(ValueError):verify_producer(s)
    def test_original_task_cannot_be_replaced_by_a_clipped_click(self):
        s=self.spec();s['producer']['tasks'][0]['destination']=[-7040,-976]
        with self.assertRaises(ValueError):verify_producer(s)
    def test_fine_goal_must_be_clipped_in_the_routing_producer(self):
        s=self.spec();s['producer']['points'][0]=[-7.25,65.5]
        with self.assertRaises(ValueError):verify_producer(s)
    def test_all_twelve_public_neighbor_admissions_are_required(self):
        s=self.spec('matrix');s['producer']['bounds'].pop()
        with self.assertRaises(ValueError):verify_producer(s)
    def test_rejected_boundary_order_cannot_certify_admission(self):
        s=self.spec('matrix');s['producer']['public'][1]='PATHBOUND case=0 rejected'
        with self.assertRaises(ValueError):verify_producer(s)
    def test_outside_route_must_naturally_end(self):
        s=self.spec();s['producer']['markers'][-1]=s['producer']['markers'][-1].replace('order=0','order=851986')
        with self.assertRaises(ValueError):verify_producer(s)
    def test_complete_retry_cadence_is_required(self):
        s=self.spec();r=next(r for r in s['lifecycle'] if r['event']=='retry-result');r['counter']+=1
        with self.assertRaises(ValueError):verify_producer(s)
    def test_header_is_the_entire_literal_motion(self):
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_outside_goal.h').read_text(),render_header(self.fixture))
    def test_raw_boundary_oracle_retains_both_axes_and_all_sides(self):
        f=json.loads((ROOT/'tools/ghidra/fixtures/retail-point-order-clip-1.27.json').read_text())
        self.assertEqual(len(f['cases']),108)
        self.assertEqual({(c['axis'],c['side'],c['step']) for c in f['cases']},{(a,b,c) for a in range(2) for b in range(2) for c in (-1,0,1)})


if __name__=='__main__':unittest.main()
