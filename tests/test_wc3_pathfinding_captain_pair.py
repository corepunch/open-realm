"""Two recruits must remain private followers until one all-entered shared batch."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_captain_pair_trace import admission,render_header,verify_admission,verify_footprints


class CaptainPairTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-captain-pair-1.27.json').read_text())

    def rows(self):return copy.deepcopy(self.fixture['admission_reference'])

    def test_complete_pair_and_retained_gate(self):
        verify_admission(self.rows(),self.fixture)
        self.assertEqual([sum(r[0]==i for r in self.fixture['motion']) for i in range(2)],[185,184])
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_captain_pair.h').read_text(),render_header(self.fixture))

    def test_first_enter_cannot_publish_the_batch(self):
        rows=self.rows();begins=[i for i,r in enumerate(rows) if r['event']=='captain-range-enter-begin']
        index=next(i for i,r in enumerate(rows) if r['event']=='captain-prepare-begin')
        rows.insert(begins[0]+1,rows.pop(index))
        with self.assertRaises(ValueError):verify_admission(rows,self.fixture)

    def test_requested_two_is_not_one_actual_count_transition(self):
        rows=self.rows();next(r for r in rows if r['event']=='captain-roster-ranges-begin')['delta']=2
        with self.assertRaises(ValueError):verify_admission(rows,self.fixture)

    def test_both_prepares_must_share_one_cohort(self):
        rows=self.rows();[r for r in rows if r['event']=='captain-prepare-begin'][1]['sharedWrapper']='different'
        with self.assertRaises(ValueError):verify_admission(rows,self.fixture)

    def test_both_prepares_must_share_one_point_request(self):
        rows=self.rows();[r for r in rows if r['event']=='captain-prepare-end'][1]['wrapper']='different'
        with self.assertRaises(ValueError):verify_admission(rows,self.fixture)

    def test_second_callback_must_complete_membership(self):
        rows=self.rows();[r for r in rows if r['event']=='captain-range-enter-end'][1]['countsAfter'][3]=1
        with self.assertRaises(ValueError):verify_admission(rows,self.fixture)

    def test_callback_clock_is_not_the_sampled_primary(self):
        rows=self.rows();next(r for r in rows if r['event']=='captain-range-enter-begin')['clock'][0]=0x3ffffff0
        with self.assertRaises(ValueError):verify_admission(rows,self.fixture)

    def test_shared_wrapper_generation_cannot_change_mid_batch(self):
        rows=self.rows();[r for r in rows if r['event']=='captain-prepare-begin'][1]['sharedIdentity'][1]+=1
        with self.assertRaises(ValueError):verify_admission(rows,self.fixture)

    def test_shared_footprint_and_survivor_are_retained(self):
        verify_footprints(copy.deepcopy(self.fixture['footprint_reference']),self.fixture)
        rows=copy.deepcopy(self.fixture['footprint_reference']);rows[-1][1]=0
        with self.assertRaises(ValueError):verify_footprints(rows,self.fixture)

    def test_follower_owners_cannot_join_before_the_gate(self):
        rows=copy.deepcopy(self.fixture['footprint_reference']);rows[0][1]=1064828928
        with self.assertRaises(ValueError):verify_footprints(rows,self.fixture)

    def test_physical_addresses_do_not_define_shared_identity(self):
        rows=self.rows()
        for r in rows:
            if r['event']=='captain-prepare-begin':r['sharedWrapper']='other-process-shared'
            if r['event']=='captain-prepare-end':r['wrapper']='other-process-request'
        self.assertEqual(admission(rows),admission(self.rows()))


if __name__=='__main__':unittest.main()
