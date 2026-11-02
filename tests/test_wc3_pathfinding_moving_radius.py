"""Runtime footprint changes require actual movement, deferred morph and stable identity."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_moving_radius_trace import render_header,verify_producer


class MovingRadiusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-moving-radius-1.27.json').read_text())
    def spec(self,name='matrix'):return copy.deepcopy(self.fixture['journeys'][name])
    def test_nine_boundaries_and_research_have_repeated_public_captures(self):
        for s in self.fixture['journeys'].values():verify_producer(s)
        self.assertEqual(len(self.fixture['cases']),8)
        self.assertEqual(sum(len(r) for r in self.fixture['motions'].values()),1179)
    def test_each_boundary_really_changes_the_mover_radius(self):
        for offset in [0,118,236,354,472,590,708,826,943]:
            s=self.spec();s['states'][offset+34]['radius']=1064828928
            with self.assertRaises(ValueError):verify_producer(s)
    def test_equality_moves_to_the_next_footprint_class(self):
        for offset in [118,472,826]:
            s=self.spec();s['states'][offset+34]['fineRect'][2]-=1
            with self.assertRaises(ValueError):verify_producer(s)
    def test_same_public_mover_keeps_its_canonical_generation(self):
        s=self.spec();s['states'][34]['identity'][1]+=1
        with self.assertRaises(ValueError):verify_producer(s)
    def test_physical_group_is_replaced(self):
        s=self.spec();s['states'][34]['group']=s['states'][0]['group']
        with self.assertRaises(ValueError):verify_producer(s)
    def test_commit_cannot_be_synchronous_with_enabled_delivery(self):
        s=self.spec();s['clocks'][1]['primary']=s['clocks'][0]['primary']
        with self.assertRaises(ValueError):verify_producer(s)
    def test_research_has_a_separate_deferred_commit(self):
        s=self.spec('research');s['clocks'][1]['primary']=s['clocks'][0]['primary']
        with self.assertRaises(ValueError):verify_producer(s)
    def test_type_and_ability_consumption_are_publicly_observed(self):
        s=self.spec('grow');s['morph_markers'][-1]=s['morph_markers'][-1].replace('chaos=0','chaos=1')
        with self.assertRaises(ValueError):verify_producer(s)
    def test_natural_arrival_keeps_no_move_head(self):
        s=self.spec();s['markers'][-1]=s['markers'][-1].replace('order=0','order=851986')
        with self.assertRaises(ValueError):verify_producer(s)
    def test_header_contains_all_literal_commits(self):
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_moving_radius.h').read_text(),render_header(self.fixture))
    def test_periodic_timer_cannot_rearm_from_callback_primary_clock(self):
        s=self.spec('matrix_handoff')
        s['rearms'][17]['timerClock'][0]=s['rearms'][17]['primary'][0]
        with self.assertRaises(ValueError):verify_producer(s)
    def test_owner_rearm_cannot_be_dropped(self):
        s=self.spec('matrix_handoff');del s['rearms'][17]
        with self.assertRaises(ValueError):verify_producer(s)
    def test_owner_period_requires_the_observed_scalar_word(self):
        s=self.spec('matrix_handoff');s['rearms'][0]['request'][2]=0x3cf5c28f
        with self.assertRaises(ValueError):verify_producer(s)


if __name__=='__main__':unittest.main()
