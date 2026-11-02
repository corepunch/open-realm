"""Public target teleports retain distinct order policy and exact Follow motion."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_follow_target_teleport_trace import render_header,verify_teleport


class FollowTargetTeleportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-follow-target-teleport-1.27.json').read_text())

    def check(self,name='travel_xy',records=None,markers=None,target=None):
        s=self.fixture['journeys'][name]
        verify_teleport(s['policy'] if records is None else records,s['markers'] if markers is None else markers,
                       s['target_markers'] if target is None else target,s['tick'],s['mode'])

    def test_all_four_public_journeys_are_verified(self):
        for name in self.fixture['journeys']:self.check(name)
        self.assertEqual(len(self.fixture['cases']),8)
        self.assertEqual(sum(len(self.fixture['motions'][s['motion']]) for s in self.fixture['journeys'].values()),4091)

    def test_teleport_cannot_retire_the_follow_head(self):
        marks=[r.replace('order=851971','order=0') if 'label=after_target_teleport ' in r else r for r in self.fixture['journeys']['travel_xy']['markers']]
        with self.assertRaises(ValueError):self.check(markers=marks)

    def test_axis_teleport_must_preserve_target_travel(self):
        target=[r.replace('order=851986','order=0') if 'tick=90 ' in r else r for r in self.fixture['journeys']['travel_xy']['target_markers']]
        with self.assertRaises(ValueError):self.check(target=target)

    def test_position_teleport_must_stop_target_travel(self):
        target=[r.replace('order=0','order=851986') if 'tick=90 ' in r else r for r in self.fixture['journeys']['travel_position']['target_markers']]
        with self.assertRaises(ValueError):self.check('travel_position',target=target)

    def test_target_must_reach_the_requested_teleport_position(self):
        target=[r.replace('x=-1600.000','x=-1936.000') if 'tick=90 ' in r else r for r in self.fixture['journeys']['travel_xy']['target_markers']]
        with self.assertRaises(ValueError):self.check(target=target)

    def test_replan_readiness_retains_timestamp_policy(self):
        rows=copy.deepcopy(self.fixture['journeys']['travel_xy']['policy'])
        next(r for r in rows if r['event']=='replan-check' and r['changed'])['ready']=0
        with self.assertRaises(ValueError):self.check(records=rows)

    def test_engine_header_retains_all_three_distinct_motion_sequences(self):
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_follow_target_teleport.h').read_text(),render_header(self.fixture))


if __name__=='__main__':unittest.main()
