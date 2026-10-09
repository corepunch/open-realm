"""Follow parity protects destination buckets, range, lifetime and every raw word."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_follow_velocity_trace import render_header,verify_policy


class FollowVelocityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-follow-velocity-1.27.json').read_text())

    def check(self,records=None,markers=None):
        f=self.fixture
        verify_policy(f['policy'] if records is None else records,f['markers'] if markers is None else markers,f['target_markers'])

    def test_bounded_target_lifecycle_is_verified(self):
        self.check()
        self.assertEqual(len(self.fixture['engine_motion']),1015)
        self.assertEqual(sum(r[0]==1 for r in self.fixture['engine_motion']),48)

    def test_missing_collision_radii_are_rejected(self):
        rows=copy.deepcopy(self.fixture['policy']);next(r for r in rows if r['event']=='arrival-range')['value']=1091960832
        with self.assertRaises(ValueError):self.check(rows)

    def test_refresh_clamp_and_count_are_verified(self):
        for remove in (False,True):
            rows=copy.deepcopy(self.fixture['policy']);i=next(i for i,r in enumerate(rows) if r['event']=='target-refresh')
            if remove:rows.pop(i)
            else:rows[i]['reload']=15
            with self.subTest(remove=remove),self.assertRaises(ValueError):self.check(rows)

    def test_same_bucket_must_not_replace_destination(self):
        rows=copy.deepcopy(self.fixture['policy'])
        next(r for r in rows if r['event']=='replan-check' and r['oldDestination']!=r['destination'] and not r['changed'])['changed']=1
        with self.assertRaises(ValueError):self.check(rows)

    def test_persistent_follow_is_not_natural_order_completion(self):
        rows=copy.deepcopy(self.fixture['policy'])
        next(r for r in rows if r['event']=='group-completion' and r['flags']&1)['gateOpen']=True
        with self.assertRaises(ValueError):self.check(rows)
        marks=[r.replace('tick=299 ','tick=298 ') for r in self.fixture['markers']]
        with self.assertRaises(ValueError):self.check(markers=marks)

    def test_engine_header_retains_every_absolute_motion_word(self):
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_follow_velocity.h').read_text(),render_header(self.fixture))


if __name__=='__main__':unittest.main()
