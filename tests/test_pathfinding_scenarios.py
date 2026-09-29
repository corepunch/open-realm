"""Frozen producer-baseline contract and rejection checks, without retail assets."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
from wc3_pathing_scenario import DEFAULT_MANIFEST,load_manifest,verify_case,first_difference


class ScenarioTests(unittest.TestCase):
    def setUp(self):
        self.manifest,self.expected=load_manifest()

    def test_frozen_baseline_has_both_arrivals_and_returns_to_idle(self):
        self.assertEqual([c['output']['arrival_tick'] for c in self.expected['cases']],[7,27])
        for case in self.expected['cases']:
            verify_case(case['output'],case)
            states=[case['output']['initial_state']]+case['output']['normalized_states']
            for state in (states[0],states[-1]):
                self.assertEqual(state['registry_live'],14)
                self.assertEqual(state['queue']['count'],0)
                self.assertFalse(state['group']['active'])
                self.assertFalse(state['visual_linked'])
                self.assertEqual(state['motion']['velocity'],[0,0])
            self.assertTrue(any(state['group']['members'] for state in states[1:]))
            self.assertTrue(any(state['events'] for state in states[1:]))
            if case['id']=='queued_successor':
                self.assertEqual(len(case['output']['arrivals']),2)
                self.assertEqual(len(case['output']['next_order_admission']),1)

    def test_raw_motion_cells_budget_events_and_membership_mutations_are_rejected(self):
        case=self.expected['cases'][0]
        for field in ('motion','grids','budgets','events','group','paths','dispatch'):
            changed=copy.deepcopy(case['output'])
            changed['normalized_states'][0][field]=None
            with self.assertRaisesRegex(ValueError,field):verify_case(changed,case)
        changed=copy.deepcopy(case['output'])
        changed['normalized_states'][1]['motion']['position'][0]^=1
        with self.assertRaisesRegex(ValueError,'motion.position.0'):verify_case(changed,case)
        changed=copy.deepcopy(case['output']);changed['normalized_states'].pop()
        with self.assertRaisesRegex(ValueError,'length'):verify_case(changed,case)

    def test_manifest_rejects_changed_build_terrain_clock_and_expectations(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/DEFAULT_MANIFEST.name
            golden=Path(temp)/self.manifest['expectations']['file']
            golden.write_bytes((DEFAULT_MANIFEST.parent/golden.name).read_bytes())
            for field in ('version','build','map','clock'):
                changed=copy.deepcopy(self.manifest)
                if field=='version':changed[field]=2
                elif field=='build':changed[field]['game_sha256']='0'*64
                elif field=='map':changed[field]['terrain_words'][0][0]^=1
                else:changed[field]['advance_bits']+=1
                path.write_text(json.dumps(changed))
                with self.assertRaises(ValueError):load_manifest(path)
            path.write_text(json.dumps(self.manifest));golden.write_text('{}')
            with self.assertRaisesRegex(ValueError,'expectations hash'):load_manifest(path)
            golden.unlink()
            with self.assertRaises(FileNotFoundError):load_manifest(path)

    def test_historical_motion_snapshot_preserves_unobserved_state(self):
        fixture=json.loads((DEFAULT_MANIFEST.parent/'retail-motion-snapshot-1.27.json').read_text())
        snapshot=fixture['snapshot']
        self.assertEqual(snapshot['motion']['position'],[1082627815,1081798842])
        self.assertEqual(snapshot['motion']['velocity'],[1089657923,3223448675])
        self.assertEqual(snapshot['paths'][0]['indices'][0],7)
        self.assertIsNone(snapshot['events'])
        self.assertIsNone(snapshot['clock'])
        self.assertIsNone(snapshot['group'])
        self.assertEqual(first_difference(snapshot,snapshot),None)


if __name__=='__main__':unittest.main()
