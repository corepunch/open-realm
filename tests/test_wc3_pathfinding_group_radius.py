"""Native local maxima and cached coarse footprints have distinct lifetimes."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_group_radius_trace import verify_producer,render_header


class GroupRadiusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-group-radius-1.27.json').read_text())
    def spec(self,scene='shrink'):return copy.deepcopy(self.fixture['journeys'][scene])
    def test_three_public_mutations_repeat_with_read_only_footprint_witnesses(self):
        for spec in self.fixture['journeys'].values():verify_producer(spec)
        self.assertEqual(len(self.fixture['cases']),12)
        self.assertEqual(sum(len(s['motion']) for s in self.fixture['journeys'].values()),831)
    def test_initial_routing_uses_the_largest_member(self):
        s=self.spec();s['routing'][0]['result']=1064828928
        with self.assertRaises(ValueError):verify_producer(s)
    def test_rebound_member_keeps_identity_but_changes_physical_owner(self):
        s=self.spec();s['routing'][1]['identity']=s['routing'][0]['identity']
        with self.assertRaises(ValueError):verify_producer(s)
    def test_rebound_owner_cannot_use_old_radius(self):
        s=self.spec();s['routing'][1]['result']=1073479680
        with self.assertRaises(ValueError):verify_producer(s)
    def test_survivor_cannot_silently_replace_the_retained_coarse_footprint(self):
        s=self.spec();old=s['routing'][0]['identity']
        row=next(r for r in s['footprints'] if r['identity']==old and len(r['members'])==1)
        row['footprint']=1064828928
        with self.assertRaises(ValueError):verify_producer(s)
    def test_singleton_replacement_order_is_rejected_as_a_group_producer(self):
        s=self.spec();s['markers'].insert(2,'PATHTRACE tick=10 label=move_accepted order=851986')
        with self.assertRaises(ValueError):verify_producer(s)
    def test_removed_peer_cannot_keep_a_public_type(self):
        s=self.spec('remove');s['public'][-1]=s['public'][-1].replace('peerType=0 ','peerType=1749240903 ')
        with self.assertRaises(ValueError):verify_producer(s)
    def test_stale_member_cannot_contribute_to_current_maximum(self):
        s=self.spec();s['footprints'][-1]['members'][0]['owner']=[-1,-1]
        with self.assertRaises(ValueError):verify_producer(s)
    def test_complete_footprint_owner_extent_is_required(self):
        s=self.spec('remove');s['footprints'].pop()
        with self.assertRaises(ValueError):verify_producer(s)
    def test_header_retains_all_literal_motion_and_footprint_words(self):
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_group_radius.h').read_text(),render_header(self.fixture))


if __name__=='__main__':unittest.main()
