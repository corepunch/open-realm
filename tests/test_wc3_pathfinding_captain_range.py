"""Captain timer evidence must keep later membership and the complete actor lifetime."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_captain_range_trace import verify_ranges,render_far_header,verify_virtual_blocker


class CaptainRangeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-captain-range-1.27.json').read_text())

    def rows(self,key='near'):
        return copy.deepcopy(self.fixture['journeys'][key]['range_reference'])

    def test_creation_phase_and_both_complete_range_timelines(self):
        for key,spec in self.fixture['journeys'].items():verify_ranges(self.rows(key),spec)
        self.assertEqual(len(self.fixture['cases']),5)

    def test_farther_start_waits_for_later_callback(self):
        self.assertEqual(self.fixture['journeys']['near']['handoff_clock'],0x3ffffff8)
        self.assertEqual(self.fixture['journeys']['far']['handoff_clock'],0x403ffffc)
        with self.assertRaises(ValueError):verify_ranges(self.rows('far'),self.fixture['journeys']['near'])

    def test_callback_cannot_use_sampled_owner_clock(self):
        rows=self.rows();next(r for r in rows if r['event']=='captain-range-enter-begin')['clock'][0]=0x3ffffff0
        with self.assertRaises(ValueError):verify_ranges(rows,self.fixture['journeys']['near'])

    def test_request_quantity_cannot_replace_actual_roster_count(self):
        rows=self.rows();next(r for r in rows if r['event']=='captain-roster-ranges-begin')['delta']=2
        with self.assertRaises(ValueError):verify_ranges(rows,self.fixture['journeys']['near'])

    def test_world_and_fine_range_words_are_distinct(self):
        rows=self.rows();r=next(r for r in rows if r['event']=='captain-range-publish');r['after']['radius']=r['requested']
        with self.assertRaises(ValueError):verify_ranges(rows,self.fixture['journeys']['near'])

    def test_period_and_rearmed_deadline_cannot_drift(self):
        rows=self.rows();next(r for r in rows if r['event']=='captain-range-update')['before']['request'][1]+=1
        with self.assertRaises(ValueError):verify_ranges(rows,self.fixture['journeys']['near'])

    def test_enter_must_increment_retained_membership(self):
        rows=self.rows();next(r for r in rows if r['event']=='captain-range-enter-end')['countsAfter'][3]=0
        with self.assertRaises(ValueError):verify_ranges(rows,self.fixture['journeys']['near'])

    def test_virtual_target_is_excluded_only_before_private_point_handoff(self):
        rows=copy.deepcopy(self.fixture['blocker_reference']);verify_virtual_blocker(rows)
        rows[0]['blockers']['objectHits']=12
        with self.assertRaises(ValueError):verify_virtual_blocker(rows)

    def test_private_blocker_is_the_retained_category_two_captain(self):
        rows=copy.deepcopy(self.fixture['blocker_reference'])
        obj=next(iter(rows[1]['blockers']['objects'].values()));obj['objectMask']=0x010000ca
        with self.assertRaises(ValueError):verify_virtual_blocker(rows)

    def test_reference_includes_both_complete_journeys(self):
        self.assertEqual(len(self.fixture['journeys']['near']['motion']),178)
        self.assertEqual(len(self.fixture['journeys']['far']['motion']),250)
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_captain_range_far.h').read_text(),render_far_header(self.fixture))


if __name__=='__main__':unittest.main()
