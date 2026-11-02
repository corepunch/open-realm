"""Two recruits must remain private followers until one all-entered shared batch."""
import copy
from collections import Counter
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_captain_pair_trace import admission,render_header,verify_admission,verify_footprints,verify_retry_lifecycle


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


class CaptainPairExtensionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mixed=json.loads((ROOT/'tools/ghidra/fixtures/retail-captain-mixed-1.27.json').read_text())
        cls.blocked=json.loads((ROOT/'tools/ghidra/fixtures/retail-captain-blocked-1.27.json').read_text())

    def test_knight_survivor_updates_live_radius_without_rebuilding_cache(self):
        verify_footprints(self.mixed['footprint_reference'],self.mixed)
        rows=copy.deepcopy(self.mixed['footprint_reference']);rows[-1][2]=rows[-1][1]
        with self.assertRaises(ValueError):verify_footprints(rows,self.mixed)

    def test_blocked_formation_retains_other_survivor(self):
        verify_footprints(self.blocked['footprint_reference'],self.blocked)
        self.assertEqual(self.blocked['member_commits'],[254,270])
        rows=copy.deepcopy(self.blocked['footprint_reference']);rows[-1][3][0][0]=0
        with self.assertRaises(ValueError):verify_footprints(rows,self.blocked)

    def test_both_public_scenes_retain_all_entered_admission(self):
        for fixture in (self.mixed,self.blocked):verify_admission(fixture['admission_reference'],fixture)

    def retry_fixture(self):
        fixture=copy.deepcopy(self.blocked)
        fixture['event_counts']=dict(Counter(r['event'] for r in fixture['lifecycle']))
        return fixture

    def test_actual_two_member_retry_and_final_singleton(self):
        fixture=self.retry_fixture()
        self.assertEqual(verify_retry_lifecycle(fixture['lifecycle'],fixture),dict(retries=14,forced_arrivals=2))
        rows=copy.deepcopy(fixture['lifecycle'])
        next(r for r in rows if r['event']=='retry-result' and r['counter']==1326)['members']=2
        with self.assertRaises(ValueError):verify_retry_lifecycle(rows,fixture)

    def test_terminal_retry_cannot_drop_cached_buffers(self):
        fixture=self.retry_fixture();rows=copy.deepcopy(fixture['lifecycle'])
        next(r for r in rows if r['event']=='route-step' and r['counter']==1310)['after'][0]=4294967295
        with self.assertRaises(ValueError):verify_retry_lifecycle(rows,fixture)
