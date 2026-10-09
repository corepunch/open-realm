"""Collision resize evidence must exercise real type/radius changes and range policy."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_follow_target_resize_trace import render_header,verify_resize

class FollowTargetResizeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-follow-target-resize-1.27.json').read_text())
    def spec(self,name='grow_fresh'):return copy.deepcopy(self.fixture['journeys'][name])
    def test_five_repeated_public_resize_journeys(self):
        for spec in self.fixture['journeys'].values():verify_resize(spec)
        self.assertEqual(len(self.fixture['cases']),10)
        self.assertEqual(sum(len(r) for r in self.fixture['motions'].values()),5075)
    def test_resize_cannot_be_an_unchanged_visual_scale(self):
        s=self.spec();s['resize_states'][301]['radius']=1064828928
        with self.assertRaises(ValueError):verify_resize(s)
    def test_resize_preserves_the_target_canonical_generation(self):
        s=self.spec();s['resize_states'][301]['moverHandle'][1]+=1
        with self.assertRaises(ValueError):verify_resize(s)
    def test_public_type_change_is_deferred(self):
        s=self.spec();s['target_markers']=[r.replace('type=1751543663','type=1749240903') if 'label=after_resize ' in r else r for r in s['target_markers']]
        with self.assertRaises(ValueError):verify_resize(s)
    def test_replacement_public_type_must_be_the_authored_clone(self):
        s=self.spec();s['target_markers']=[r.replace('type=1749240903','type=1751543663') if 'tick=101 ' in r else r for r in s['target_markers']]
        with self.assertRaises(ValueError):verify_resize(s)
    def test_retained_range_must_not_recompute_on_resize(self):
        s=self.spec('grow_control');s['policy'][0]['value']=1095041024
        with self.assertRaises(ValueError):verify_resize(s)
    def test_nearby_fresh_growth_uses_half_edge_distance(self):
        s=self.spec();[r for r in s['policy'] if r['event']=='arrival-range'][3]['value']=1095041024
        with self.assertRaises(ValueError):verify_resize(s)
    def test_new_shrink_group_uses_the_new_collision_radius(self):
        s=self.spec('shrink_fresh');[r for r in s['policy'] if r['event']=='arrival-range'][-1]['value']=1093992448
        with self.assertRaises(ValueError):verify_resize(s)
    def test_research_must_keep_the_original_type_until_unlock(self):
        s=self.spec('grow_research');s['target_markers']=[r.replace('type=1751543663','type=1749240903') if 'tick=149 ' in r else r for r in s['target_markers']]
        with self.assertRaises(ValueError):verify_resize(s)
    def test_research_must_keep_the_original_radius_until_unlock(self):
        s=self.spec('grow_research');s['resize_states'][301]['radius']=1073479680
        with self.assertRaises(ValueError):verify_resize(s)
    def test_research_unlock_must_be_publicly_observed(self):
        s=self.spec('grow_research');s['markers']=[r.replace('target_resize_research','unrelated_marker') for r in s['markers']]
        with self.assertRaises(ValueError):verify_resize(s)
    def test_header_contains_every_literal_commit(self):
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_follow_target_resize.h').read_text(),render_header(self.fixture))
if __name__=='__main__':unittest.main()
