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
from wc3_pathing_pair import load_fixture as load_pair,verify as verify_pair


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

    def test_fresh_shared_pair_decides_both_members_before_committing(self):
        fixture=load_pair();case=fixture['output'];verify_pair(case,fixture)
        self.assertEqual(case['arrival_tick'],7)
        self.assertTrue(case['shared_pair_completed'])
        self.assertEqual(len(case['arrivals']),1)
        self.assertEqual(len(case['second_arrivals']),1)
        initial=case['initial_state'];final=case['normalized_states'][-1]
        self.assertEqual(initial['registry_live'],20)
        self.assertEqual(final['registry_live'],20)
        self.assertTrue(all(not p['fine'] and not p['adaptive'] for p in initial['paths']))
        fresh=case['normalized_states'][0]
        self.assertEqual(len(fresh['group']['members']),2)
        self.assertTrue(all(p['fine'] or p['adaptive'] for p in fresh['paths']))
        for state in case['normalized_states'][:8]:
            self.assertEqual(state['decision_commit_order'],[
                ['decision','first'],['decision','second'],['commit','first'],['commit','second']])
        for state in (initial,final):
            self.assertEqual(state['queue']['count'],0)
            self.assertEqual(state['second_unit']['queue_count'],0)
            self.assertFalse(state['visual_linked'])
            self.assertFalse(state['group']['active'])
            self.assertEqual(state['motion']['velocity'],[0,0])
            self.assertEqual(state['second_unit']['motion'][4:6],[0,0])

    def test_shared_pair_rejects_second_actor_and_ordering_mutations(self):
        fixture=load_pair()
        for field in ('second_unit','decision_commit_order','second_dispatch','auxiliary_dispatch'):
            changed=copy.deepcopy(fixture['output'])
            changed['normalized_states'][0][field]=None
            with self.assertRaisesRegex(ValueError,field):verify_pair(changed,fixture)
        changed=copy.deepcopy(fixture['output'])
        changed['normalized_states'][0]['decision_commit_order'][1:3]=reversed(changed['normalized_states'][0]['decision_commit_order'][1:3])
        with self.assertRaisesRegex(ValueError,'decision_commit_order'):verify_pair(changed,fixture)
        changed=copy.deepcopy(fixture['output'])
        changed['trajectory'][1]['second_velocity'][0]^=1
        with self.assertRaisesRegex(ValueError,'second_velocity'):verify_pair(changed,fixture)
        changed=copy.deepcopy(fixture['output']);changed['second_arrivals']=[]
        with self.assertRaisesRegex(ValueError,'second_arrivals'):verify_pair(changed,fixture)

    def test_ground_wall_changes_member_routes_and_reversal_restores_travel(self):
        directory=DEFAULT_MANIFEST.parent
        fixtures={name:load_pair(directory/('retail-shared-pair-'+name+'-1.27.json'))
                  for name in ('wall','wall-maskless','ground-open','ground-reversal')}
        for fixture in fixtures.values():verify_pair(fixture['output'],fixture)
        wall,maskless,opened,reversed_wall=(fixtures[name]['output'] for name in ('wall','wall-maskless','ground-open','ground-reversal'))
        self.assertEqual([wall['arrival_tick'],maskless['arrival_tick'],opened['arrival_tick'],reversed_wall['arrival_tick']],[25,7,7,7])
        self.assertEqual(opened['trajectory'],reversed_wall['trajectory'])
        self.assertEqual(maskless['trajectory'],load_pair()['output']['trajectory'])
        self.assertNotEqual(wall['trajectory'],opened['trajectory'])
        self.assertEqual(wall['arrivals'][0]['clock_bits'],0x3f180000)
        self.assertEqual(wall['second_arrivals'][0]['clock_bits'],0x3f480000)
        self.assertIn(1,[len(s['group']['members']) for s in wall['normalized_states']])
        self.assertEqual([p['flags'][6] for p in wall['initial_state']['paths'] if p['owner']!='group'],[0x02000002]*2)
        self.assertEqual([p['flags'][6] for p in maskless['initial_state']['paths'] if p['owner']!='group'],[0]*2)
        for y in range(3,6):
            self.assertEqual(wall['initial_state']['grids'][1]['cells'][16*y+5]>>24,2)
            self.assertEqual(reversed_wall['initial_state']['grids'][1]['cells'][16*y+5]>>24,0)
        for state in wall['normalized_states']:
            events=state['decision_commit_order'];committed=False
            for kind,role in events:
                if kind=='commit':committed=True
                else:self.assertFalse(committed)
        # Rebuild history remains observable even when travel is restored.
        self.assertEqual(opened['normalized_states'][0]['grids'][1]['object_flags'][1],33)
        self.assertEqual(reversed_wall['normalized_states'][0]['grids'][1]['object_flags'][1],41)

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
