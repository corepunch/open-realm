"""Corpus failures stay visible; stale reports and damaged captures never certify evidence."""
import copy
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
from run_wc3_pathfinding_corpus import (DEFAULT_MANIFEST,load_manifest,check_report,
                                        check_capture,run_entry)


class CorpusTests(unittest.TestCase):
    def test_mechanical201_preserves_ordered_selection_and_latent_category(self):
        sys.path.insert(0,str(ROOT/'tools/frida/research'))
        from mechanical201_verify import stages
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-mechanical-critter201-1.27.json').read_text())
        for scene in fixture['scenes']:
            self.assertEqual(len(stages(scene['stages'])),89)
            self.assertEqual(len(scene['public_markers']),57)
        rows=fixture['scenes'][1]['stages']
        altered=copy.deepcopy(rows)
        next(r for r in altered if r['event']=='apply-end')['unit']['sep']=0x2f300000
        with self.assertRaises(ValueError):stages(altered)
        altered=copy.deepcopy(rows)
        next(r for r in altered if r['event']=='candidate')['code']=0
        with self.assertRaises(ValueError):stages(altered)
        altered=copy.deepcopy(rows)
        next(r for r in altered if r['event']=='range-begin')['index']=35
        with self.assertRaises(ValueError):stages(altered)

    def test_explicit_swing199_keeps_windup_and_non_swing_producers_distinct(self):
        sys.path.insert(0,str(ROOT/'tools/frida/research'))
        from swing199_verify import stages
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-explicit-swing199-1.27.json').read_text())
        self.assertEqual(len(stages(fixture['stages'])),127)
        self.assertEqual(len(fixture['public_markers']),83)
        altered=copy.deepcopy(fixture['stages'])
        next(r for r in altered if r['event']=='cap-arm')['duration']=0
        with self.assertRaises(ValueError):stages(altered)
        altered=copy.deepcopy(fixture['stages'])
        next(r for r in altered if r['event']=='cooldown-begin' and not r['swing'])['swing']=1
        with self.assertRaises(ValueError):stages(altered)

    @classmethod
    def setUpClass(cls):
        # Repository inputs remain fixed during this suite. Verify all pins
        # once; each test receives an independent manifest for mutations.
        cls.frozen_manifest=load_manifest(DEFAULT_MANIFEST)

    def setUp(self):
        self.manifest=copy.deepcopy(self.frozen_manifest)
        self.entry=next(e for e in self.manifest['entries'] if e['id']=='oracle-grid')
        self.target=self.manifest['target']

    def test_target_visibility_entry_keeps_controls_and_scope_limits(self):
        entry=next(e for e in self.manifest['entries'] if e['id']=='live-target-visibility-cached-arrival')
        self.assertEqual(entry['expected_status'],'live-exact-target-visibility')
        self.assertEqual(entry['checks']['raw_owner_rows'],dict(equal=238))
        self.assertEqual(entry['checks']['hidden_visits'],dict(equal=754))
        captures=[entry['inputs']]+[c['inputs'] for c in entry['additional_captures']]
        self.assertEqual(sum(c['metadata']['mode']=='control' for c in captures),2)
        self.assertEqual(sum(c['metadata']['mode']=='observe' for c in captures),4)
        self.assertTrue(any('no new live' in x for x in entry['exclusions']))
        self.assertTrue(any('this engine chunk' in x for x in entry['exclusions']))

    def test_selection_and_independent_move_require_complete_owner_streams(self):
        entry=next(e for e in self.manifest['entries'] if e['id']=='live-formation-selection-independent')
        report={field:rule['equal'] for field,rule in entry['checks'].items()}
        check_report(report,entry,self.target)
        for field in ('scenes','owner_visits','member_commits','physical_groups',
                      'saved_suffix_visits','saved_suffix_commits','independent_repeats'):
            changed=dict(report);changed[field]-=1
            with self.assertRaises(ValueError):check_report(changed,entry,self.target)
        changed=dict(report,passed=False)
        with self.assertRaises(ValueError):check_report(changed,entry,self.target)
        captures=[entry['inputs']]+[c['inputs'] for c in entry['additional_captures']]
        self.assertEqual(len(captures),10)
        self.assertEqual(sum(c['metadata']['mode']=='control' for c in captures),2)
        self.assertTrue(any('No new live' in x for x in entry['exclusions']))

    def test_blocked_completion_export_retains_observed_notification_counter(self):
        import gzip
        fixture=ROOT/'tools/ghidra/fixtures/retail-blocked-completion195-1.27.json.gz'
        rows=json.loads(gzip.decompress(fixture.read_bytes()))['cases']
        self.assertEqual(len(rows),1536)
        self.assertEqual(sum(r['retry'] for r in rows),32)
        for row in rows:
            active=not row['gate'][0] or row['gate'][1]>32
            retry=active and row['count']>1 and bool(row['member_flags']&0x20000) and row['counter']+1<20 and row['distance']>16
            complete=active and not retry
            self.assertEqual(row['retry'],retry)
            self.assertEqual(row['complete'],complete)
            self.assertEqual(row['next_counter'],0 if complete else row['counter']+active)
            self.assertEqual(row['notification_counters'],[row['counter']+1] if complete else [])
            self.assertEqual(bool(row['notifications']),complete)
            self.assertEqual(row['member_identity'],[0xffffffff]*2 if complete else [0,100])
            if retry:
                self.assertEqual(row['indices'],[0xffffffff]*2)
                self.assertEqual(row['retry_delay'],[0,0])
                self.assertEqual(row['next_path_flags'],row['path_flags']&0xcfffffff)
                self.assertEqual(row['destination'],[0x41400000,0x41000000])
        entry=next(e for e in self.manifest['entries'] if e['id']=='oracle-blocked-completion195')
        self.assertEqual(entry['checks']['cases'],dict(equal=1536))
        self.assertTrue(any('public' in x for x in entry['exclusions']))

    def test_retry_lifetime196_fixture_is_the_original_stage_input(self):
        import gzip
        sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
        from retry196_expected import render
        fixture=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-retry-lifetime196-1.27.json.gz').read_bytes()))
        self.assertEqual(render(fixture),(ROOT/'games/warcraft-3/game/tests/retail_retry_lifetime196.h').read_text())
        rows=fixture['cases']
        self.assertEqual([sum(r['scenario']==s for r in rows)for s in (0,1)],[20,27])
        self.assertEqual([r['step']for r in rows if r['notifications']],[20,7,27])
        events=[e[1]for r in rows for e in r['events']]
        self.assertEqual(events.count(0x40190065),1)
        self.assertEqual(events.count(0x40190066),4)
        self.assertEqual([r['counter']for r in rows if r['notifications']],[0,0,0])
        self.assertEqual(rows[20+6]['notification_counters'],[7])
        self.assertEqual(rows[20+7]['decisions'][0][:2],[0,100])
        self.assertEqual(len(rows[20+7]['decisions']),2)
        self.assertEqual(rows[8]['changes'],[[1,0x42000000,0x3f800000]])
        self.assertEqual(rows[13]['changes'],[[1,0x45000000,0x42800000]])

    def test_blocked_spell196_verifier_rejects_arrival_and_missing_failure(self):
        import gzip
        sys.path.insert(0,str(ROOT/'tools/frida/research'))
        from completion196_verify import timeline
        fixture=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-blocked-spell196-1.27.json.gz').read_bytes()))
        rows=copy.deepcopy(fixture['timeline'])
        for row in rows:
            if row['event']=='cant-path':row['ability']='0x12345678'
        self.assertEqual(timeline(rows),fixture['timeline'])
        self.assertEqual(len(fixture['captures']),3)
        changed=copy.deepcopy(rows)
        next(r for r in changed if r['event']=='blocked')['event']='arrival'
        with self.assertRaises(ValueError):timeline(changed)
        changed=[r for r in rows if r['event']!='unit-completion-event']
        with self.assertRaises(ValueError):timeline(changed)
        missing=next(r for r in rows if r['event']=='owner-begin')
        changed=[r for r in rows if r is not missing]
        with self.assertRaises(ValueError):timeline(changed)

    def test_inventory_covers_oracles_archives_and_native_differences(self):
        entries=self.manifest['entries']
        self.assertEqual(sum(e['kind']=='oracle' for e in entries),188)
        self.assertEqual(sum(e['id'].startswith('capture-') for e in entries),121)
        self.assertEqual(sum(e['id'].startswith('live-') for e in entries),160)
        rejected=[e for e in entries if e['expected_status']=='archive-rejected']
        self.assertEqual(len(rejected),8)
        self.assertTrue(all(not e['evidence'] for e in rejected))
        completed_rejections=[e for e in rejected if e['capture_complete']]
        self.assertEqual([e['id'] for e in completed_rejections],['capture-arrival-point-first'])
        self.assertTrue(completed_rejections[0]['capture_failures'])
        native=[e for e in entries if e['expected_status']=='known-reference-difference']
        self.assertEqual(len(native),4)
        for entry in native:
            self.assertEqual(entry['expected_exit'],1)
            check_report(dict(binary_sha256=self.target['game_sha256'],differences=[{}]*4,
                              stored_size=2,promotion_disabled=False,forced_east_boundary=False,
                              engine_exact_cases=4 if '--terrain-producer' in entry['command'] else 356,
                              cases=4 if '--terrain-producer' in entry['command'] else 356,
                              producer_classification_cases=54,producer_classification_rejected=27,producer_hierarchy_cells=2206),entry,self.target)

    def test_target_loss_requires_complete_producer_and_handoff_repeats(self):
        entry=next(e for e in self.manifest['entries'] if e['id']=='live-target-loss-ordered-subscriptions')
        report={field:rule['equal'] for field,rule in entry['checks'].items()}
        check_report(report,entry,self.target)
        for field,value in [('passed',False),('captures',5),('controls',1),
                            ('observed_repeats',1),('producer_events',3),('subscribed_handoffs',3)]:
            changed=dict(report);changed[field]=value
            with self.assertRaises(ValueError):check_report(changed,entry,self.target)
        self.assertTrue(any('no new' in x.lower() for x in entry['exclusions']))

    def test_invisibility_entries_keep_consumer_and_public_evidence_separate(self):
        oracle=next(e for e in self.manifest['entries'] if e['id']=='oracle-invisibility-fade-listener')
        self.assertEqual(oracle['checks']['cases'],dict(equal=27))
        self.assertIn('--header',oracle['command'])
        self.assertTrue(any('stand-ins' in x for x in oracle['exclusions']))
        entry=next(e for e in self.manifest['entries'] if e['id']=='live-invisibility-target-loss')
        report={field:rule['equal'] for field,rule in entry['checks'].items()}
        check_report(report,entry,self.target)
        for field,value in [('passed',False),('captures',5),('controls',1),('observed_repeats',1),('policy_events',5)]:
            changed=dict(report);changed[field]=value
            with self.assertRaises(ValueError):check_report(changed,entry,self.target)
        self.assertTrue(any('No new live' in x for x in entry['exclusions']))

    def test_yield_lifetime_requires_both_policies_and_identity_controls(self):
        entry=next(e for e in self.manifest['entries'] if e['id']=='live-moving-yield-identity-and-group-policy')
        report={field:rule['equal'] for field,rule in entry['checks'].items()}
        check_report(report,entry,self.target)
        for field,value in [('passed',False),('captures',9),('controls',1),
                            ('owner_records',379),('policy_cases',4)]:
            changed=dict(report);changed[field]=value
            with self.assertRaises(ValueError):check_report(changed,entry,self.target)

    def test_composed_blockers_require_complete_streams_and_original_exports(self):
        entry=next(e for e in self.manifest['entries'] if e['id']=='live-composed-dynamic-blockers-and-yields')
        report={field:rule['equal'] for field,rule in entry['checks'].items()}
        check_report(report,entry,self.target)
        for field,value in [('passed',False),('captures',18),('controls',6),('repeats',4),
                            ('original_owner_records',538),('observed_member_steps',13723),
                            ('engine_scenes',12),('engine_owner_steps',9880)]:
            changed=dict(report);changed[field]=value
            with self.assertRaises(ValueError):check_report(changed,entry,self.target)

    def test_target_delays_require_complete_approach_and_denied_visit_controls(self):
        entry=next(e for e in self.manifest['entries'] if e['id']=='live-target-destination-delays-and-denied-visits')
        report={field:rule['equal'] for field,rule in entry['checks'].items()}
        check_report(report,entry,self.target)
        for field,value in [('passed',False),('captures',4),('controls',1),
                            ('approach_owner_rows',234),('denied_visits',4165),('recovered_episodes',722)]:
            changed=dict(report);changed[field]=value
            with self.assertRaises(ValueError):check_report(changed,entry,self.target)

    def test_warp_markers_require_repeated_complete_portal_lifetimes(self):
        entry=next(e for e in self.manifest['entries'] if e['id']=='live-formation-warp-marker-lifetimes')
        for capture in [entry]+entry['additional_captures']:
            self.assertNotIn('pid',capture['inputs']['metadata'])
        report={field:rule['equal'] for field,rule in entry['checks'].items()}
        check_report(report,entry,self.target)
        for field,value in [('passed',False),('captures',3),('controls',0),
                            ('observed_repeats',2),('warp_transitions',11),('retained_marker_commits',179)]:
            changed=dict(report);changed[field]=value
            with self.assertRaises(ValueError):check_report(changed,entry,self.target)

    def test_large_captain_rosters_require_complete_batches_and_controls(self):
        entry=next(e for e in self.manifest['entries'] if e['id']=='live-captain-larger-roster-batches')
        report={field:rule['equal'] for field,rule in entry['checks'].items()}
        check_report(report,entry,self.target)
        for field,value in [('passed',False),('scenes',1),('repeats',1),
                            ('prepared_members',217),('public_markers',2566)]:
            changed=dict(report);changed[field]=value
            with self.assertRaises(ValueError):check_report(changed,entry,self.target)

    def test_captain_speed_requires_complete_policies_and_controls(self):
        entry=next(e for e in self.manifest['entries'] if e['id']=='live-captain-retained-point-speed')
        report={field:rule['equal'] for field,rule in entry['checks'].items()}
        check_report(report,entry,self.target)
        for field,value in [('passed',False),('scenes',4),('repeats',1),
                            ('speed_publications',41),('public_markers',1132)]:
            changed=dict(report);changed[field]=value
            with self.assertRaises(ValueError):check_report(changed,entry,self.target)

    def test_spatial_storage_requires_complete_differential_and_controls(self):
        entry=next(e for e in self.manifest['entries'] if e['id']=='oracle-spatial-storage')
        report={'binary_sha256':self.target['game_sha256']}
        for field,rule in entry['checks'].items():
            report[field]=rule['equal'] if 'equal' in rule else [None]*rule['length']
        check_report(report,entry,self.target)
        for field,value in [('mutation_stages',77),('queries',130),('boundary_capacity',131072),
                            ('live_capture_count',1),('control_markers',361),('saved_functions',13),
                            ('fine_saved_functions',14),('allocation_sites',27),('metadata_before',2099),
                            ('metadata_after',35),('allocation_expected_equal',False),('growth_expected_equal',False)]:
            changed=dict(report);changed[field]=value
            with self.assertRaises(ValueError):check_report(changed,entry,self.target)

    def test_category_contract_requires_complete_controls_and_saved_evidence(self):
        entry=next(e for e in self.manifest['entries'] if e['id']=='oracle-object-categories')
        report={'binary_sha256':self.target['game_sha256']}
        for field,rule in entry['checks'].items():
            report[field]=rule['equal'] if 'equal' in rule else [None]*rule['length']
        check_report(report,entry,self.target)
        for field,value in (('cases',50000),('composed_scenarios',15),('live_captures',1),
                            ('live_consumer_checks',164),('control_markers',59),
                            ('saved_functions',30),('input_files',5)):
            changed=dict(report);changed[field]=value
            with self.assertRaises(ValueError):check_report(changed,entry,self.target)

    def test_blocker_contract_rejects_missing_target_cases_and_changed_order(self):
        entry=next(e for e in self.manifest['entries'] if e['id']=='oracle-blocker-candidates')
        report={'binary_sha256':self.target['game_sha256']}
        for field,rule in entry['checks'].items():
            report[field]=rule['equal'] if 'equal' in rule else [None]*rule['length']
        check_report(report,entry,self.target)
        for field,value in (('expected_sha256','0'*64),('partB_cases',13),('partF',[])):
            changed=dict(report); changed[field]=value
            with self.assertRaises(ValueError):
                check_report(changed,entry,self.target)

    def test_constructed_map_boundary_literals_match_original_words(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-constructed-map-coordinates-1.27.json').read_text())
        source=(ROOT/'games/warcraft-3/game/tests/retail_constructed_maps.h').read_text()
        table=source.split('retail_constructed_corners[][100]={',1)[1].split('};',1)[0]
        expected=[v for m in frozen['maps'] for c in m['boundary_cases'] for v in c['input']+c['output']]
        self.assertEqual([int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',table)],expected)
        indices=[int(v) for v in re.findall(r'\},(-?\d+)\},',table)]
        self.assertEqual(indices,[c['index'] for m in frozen['maps'] for c in m['boundary_cases']])
        table=source.split('retail_constructed_edits[][16][3]={',1)[1].split('};',1)[0]
        self.assertEqual([int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',table)],
            [v for m in frozen['maps'] for e in m['edits'] for v in e])

    def test_constructed_map_complete_grids_match_engine_numeric_fixture(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-constructed-map-coordinates-1.27.json').read_text())
        source=(ROOT/'games/warcraft-3/game/tests/retail_constructed_maps.h').read_text()
        table=source.split('retail_constructed_runs[][2]={',1)[1].split('};',1)[0]
        self.assertEqual([[int(a),int(b)] for a,b in re.findall(r'\{(\d+),(\d+)\}',table)],
            [r for m in frozen['maps'] for k in ('initial_classes','edited_classes','edited_fine') for r in m[k]])
        table=source.split('retail_constructed_maps[]={',1)[1].split('};',1)[0]
        rows=[line for line in table.splitlines() if line.strip()]
        self.assertEqual(len(rows),25)
        offset=0
        for line,m in zip(rows,frozen['maps']):
            self.assertEqual([int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',line)],m['bounds'])
            actual=[[int(a),int(b)] for a,b in re.findall(r'\{(\d+),(\d+)\}',line)]
            expected=m['dimensions'][:]
            for key in ('initial_classes','edited_classes','edited_fine'):
                expected.append([offset,len(m[key])]);offset+=len(m[key])
            self.assertEqual(actual,expected)
            cells=sum(w*h for w,h in m['dimensions'][2:])
            self.assertEqual(sum(n for n,v in m['initial_classes']),cells)
            self.assertEqual(sum(n for n,v in m['edited_classes']),cells)
            self.assertEqual(sum(n for n,v in m['edited_fine']),m['dimensions'][1][0]*m['dimensions'][1][1])

    def test_constructed_map_matrix_retains_every_corner_and_rejections(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-constructed-map-coordinates-1.27.json').read_text())
        self.assertEqual(len(frozen['maps']),25)
        self.assertEqual(sum(c['index']>=0 for m in frozen['maps'] for c in m['boundary_cases']),675)
        for m in frozen['maps']:
            self.assertEqual(len(m['boundary_cases']),100)
            for corner in ([0,0],[0,1],[1,0],[1,1]):
                self.assertEqual(sum(c['corner']==corner for c in m['boundary_cases']),25)

    def test_public_oblique_engine_words_and_geometry_match_frozen_original(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-public-oblique-1.27.json').read_text())
        source=(ROOT/'games/warcraft-3/game/tests/retail_public_oblique.h').read_text()
        block=source.split('public_oblique_motion[][6]={',1)[1].split('};',1)[0]
        words=[int(w,16) for w in re.findall(r'0x([0-9a-f]+)u',block)]
        self.assertEqual(len(words),689*6)
        rows=[words[i:i+6] for i in range(0,len(words),6)]
        digest=hashlib.sha256(json.dumps(rows,separators=(',',':')).encode()).hexdigest()
        self.assertEqual(digest,frozen['engine_motion_sha256'])
        block=source.split('public_oblique_terrain_runs[][2]={',1)[1].split('};',1)[0]
        actual=bytes(value for n,value in re.findall(r'\{(\d+),(\d+)\}',block) for _ in range(int(n)) for value in (int(value),))
        def expand(hex_runs):
            raw=bytes.fromhex(hex_runs)
            return bytes(raw[i+2] for i in range(0,len(raw),3) for _ in range(int.from_bytes(raw[i:i+2],'little')))
        geometry=frozen['terrain']['geometry']
        terrain,objects=expand(geometry['terrainRuns']),expand(geometry['objectRuns'])
        self.assertEqual(len(actual),384*256)
        self.assertEqual(actual,bytes(a|b for a,b in zip(terrain,objects)))

    def test_public_spawn_motion_engine_words_match_original_lifetimes(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-public-spawn-motion-1.27.json').read_text())
        source=(ROOT/'games/warcraft-3/game/tests/t_movement.c').read_text()
        table=source.split('spawn_motion[][6]={',1)[1].split('};',1)[0]
        actual=[int(w,16) for w in re.findall(r'0x([0-9a-f]+)u',table)]
        expected=[word for r in frozen['rows'] if r['event']=='velocity-commit'
            for word in [r['clock'][0],*r['after'][2:6],r['after'][7]]]
        self.assertEqual(len(expected),247*6)
        self.assertEqual(actual,expected)
        terrain=source.split('uint32_t const spawn_terrain_rows[]={',1)[1].split('};',1)[0]
        self.assertEqual([int(w,16) for w in re.findall(r'0x([0-9a-f]+)u',terrain)],frozen['terrain_control']['rows'])
        self.assertEqual(frozen['terrain_control']['bounds'],[152,56,184,72])
        self.assertEqual(frozen['terrain_control']['dimensions'],[384,256])

    def test_zero_query_placement_retains_bounds_against_authored_blockage(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-zero-query-placement-1.27.json').read_text())
        self.assertEqual(len(frozen['cases']),288); self.assertEqual(frozen['original_calls'],576)
        self.assertEqual({r['input'][5] for r in frozen['cases']},set(range(4)))
        self.assertTrue(all(r['input'][6]==0 and r['terrain_mask']==2 for r in frozen['cases']))
        sealed=[r for r in frozen['cases'] if r['terrain']=='sealed' and r['input'][2:4]==[0x41440000,0x414c0000]]
        self.assertEqual(len(sealed),24)
        self.assertTrue(all(r['output']==[1,*r['input'][2:4]] and r['visits']==1 for r in sealed))
        outside=[r for r in frozen['cases'] if r['input'][2]==0xbe000000 and r['input'][4]==1]
        self.assertTrue(outside); self.assertTrue(all(r['output'][0]==0 for r in outside))

    def test_fine_trajectory_matches_engine_word_reference(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-fine-route-trajectory-1.27.json').read_text())
        case=frozen['cases'][0]
        source=(ROOT/'games/warcraft-3/game/tests/t_movement.c').read_text()
        table=source.split('expected[34][6]={',1)[1].split('};',1)[0]
        actual=[int(word,16) for word in re.findall(r'0x([0-9a-f]+)u',table)]
        expected=[]
        for row in case['steps']:
            expected.extend(row['position_bits']+row['velocity_bits']+[row['heading_bits'],row['waypoint']])
        self.assertEqual(case['ticks'],34)
        self.assertEqual(actual,expected)

    def test_primary_owner_trajectory_matches_engine_word_reference(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-primary-owner-route-1.27.json').read_text())
        case=frozen['cases'][0]
        source=(ROOT/'games/warcraft-3/game/tests/t_movement.c').read_text()
        table=source.split('primary_expected[34][9]={',1)[1].split('};',1)[0]
        actual=[int(word,16) for word in re.findall(r'0x([0-9a-f]+)u',table)]
        expected=[]
        for row in case['steps']:
            expected.extend(row['position_bits']+row['velocity_bits']+[row['heading_bits'],row['waypoint']]+row['clock'])
        self.assertEqual(frozen['phases_per_owner'],6)
        self.assertEqual(case['ticks'],34)
        self.assertEqual(actual,expected)

    def test_adaptive_handoff_matches_engine_full_route_reference(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-adaptive-handoff-1.27.json').read_text())
        source=(ROOT/'games/warcraft-3/game/tests/t_movement.c').read_text()
        table=source.split('handoff_words[]={',1)[1].split('};',1)[0]
        words=[int(word,16) for word in re.findall(r'0x([0-9a-f]+)u',table)]
        canonical=[row for row in frozen['cases'] if row['lane']==0]
        self.assertEqual(len(frozen['cases']),48)
        self.assertEqual(words,[word for row in canonical for word in row['fine_words']])
        for row in canonical:
            self.assertEqual(row['fine_words'][:2],row['fine_goal_bits'])
            self.assertEqual(row['fine_words'][-2:],row['source_bits'])
            others=[r for r in frozen['cases'] if (r['fixture'],r['size_class'])==(row['fixture'],row['size_class'])]
            self.assertEqual(len(others),4)
            self.assertTrue(all(r['fine_words']==row['fine_words'] for r in others))

    def test_adaptive_progress_matches_engine_full_refill_reference(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-adaptive-progress-1.27.json').read_text())
        source=(ROOT/'games/warcraft-3/game/tests/t_movement.c').read_text()
        table=source.split('progress_words[]={',1)[1].split('};',1)[0]
        words=[int(word,16) for word in re.findall(r'0x([0-9a-f]+)u',table)]
        canonical=[row for row in frozen['cases'] if row['lane']==0]
        self.assertEqual(len(frozen['cases']),32)
        self.assertEqual(words,[word for row in canonical for step in row['steps'] for word in step['fine_words']])
        for row in frozen['cases']:
            self.assertEqual(len(row['steps']),1)
            step=row['steps'][0]
            self.assertEqual(step['coarse_index'],0)
            self.assertEqual(step['fine_words'][-2:],step['source_bits'])
            self.assertEqual(step['result'],0)

    def test_long_adaptive_refills_match_all_engine_reference_words(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-adaptive-long-progress-1.27.json').read_text())
        source=(ROOT/'games/warcraft-3/game/tests/t_movement.c').read_text()
        table=source.split('long_words[]={',1)[1].split('};',1)[0]
        words=[int(word,16) for word in re.findall(r'0x([0-9a-f]+)u',table)]
        expected=[]
        for row in frozen['cases']:
            if row['lane']:continue
            expected+=row['coarse_words']+row['fine_words']
            expected+=[word for step in row['steps'] for word in step['fine_words']]
        self.assertEqual(words,expected)
        self.assertEqual(len(frozen['cases']),32)
        self.assertEqual(sum(len(row['steps']) for row in frozen['cases']),160)
        self.assertTrue(all(len(row['steps'])==5 for row in frozen['cases']))
        for row in frozen['cases']:
            self.assertTrue(any(step['coarse_index']>0 for step in row['steps']))
            self.assertEqual(row['steps'][-1]['coarse_index'],0)
            others=[r for r in frozen['cases'] if (r['fixture'],r['size_class'])==(row['fixture'],row['size_class'])]
            self.assertTrue(all({k:v for k,v in r.items() if k!='lane'}=={k:v for k,v in row.items() if k!='lane'} for r in others))

    def test_default_unit_route_matches_complete_engine_buffers(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-unit-default-route-1.27.json').read_text())
        source=(ROOT/'games/warcraft-3/game/tests/t_movement.c').read_text()
        table=source.split('unit_default_words[]={',1)[1].split('};',1)[0]
        words=[int(word,16) for word in re.findall(r'0x([0-9a-f]+)u',table)]
        self.assertEqual(len(frozen['cases']),8)
        self.assertEqual(words,[word for row in frozen['cases'] for word in row['coarse_words']+row['fine_words']])
        for row in frozen['cases']:
            self.assertEqual(row['limits'],(400<<16)|700)
            self.assertGreater(row['coarse_count'],1)
            self.assertLess(row['coarse_work'],400)
            self.assertLess(row['fine_work'],700)
            self.assertEqual(row['coarse_words'][-2:],[0x40080000,0x40180000])
            self.assertEqual(row['fine_words'][-2:],row['source_bits'])

    def test_ordinary_budget_partial_words_match_engine_reference(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-unit-fine-budget-1.27.json').read_text())
        source=(ROOT/'games/warcraft-3/game/tests/t_movement.c').read_text()
        table=source.split('unit_budget_words[]={',1)[1].split('};',1)[0]
        words=[int(word,16) for word in re.findall(r'0x([0-9a-f]+)u',table)]
        rows=[r for r in frozen['cases'] if r['budget']==700]
        self.assertEqual(len(frozen['cases']),16);self.assertEqual(words,[word for row in rows for word in row['fine_words']])
        for row in rows:
            control=next(r for r in frozen['cases'] if (r['pattern'],r['size_class'],r['budget'])==(row['pattern'],row['size_class'],2048))
            self.assertNotEqual(row['fine_words'][:2],control['fine_words'][:2])
            self.assertEqual(row['pops'],701); self.assertEqual(row['result'],0)
            self.assertEqual(row['fine_index'],row['fine_count']-2)
            self.assertEqual(row['fine_words'][-2:],row['source_bits'])

    def test_inventory_rejects_missing_scripts_changed_fixtures_and_hidden_differences(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'manifest.json'
            mutations=[]
            missing=copy.deepcopy(self.manifest)
            missing['entries']=[e for e in missing['entries'] if e['command'][1]!='tools/ghidra/verify_wc3_pathing_arrival.py']
            mutations.append(missing)
            changed=copy.deepcopy(self.manifest);changed['fixtures'][0]['sha256']='0'*64;mutations.append(changed)
            changed=copy.deepcopy(self.manifest)
            next(e for e in changed['entries'] if e['id']=='adaptive-size2')['expected_exit']=0
            mutations.append(changed)
            changed=copy.deepcopy(self.manifest);changed['entries'][0]['report']='../old.json';mutations.append(changed)
            for changed in mutations:
                path.write_text(json.dumps(changed))
                with self.assertRaises(ValueError):load_manifest(path)

    def test_capture_hash_metadata_and_completion_are_all_required(self):
        rows=[dict(event='metadata',sha256=self.target['game_sha256'],pid=99,owned=True),
              dict(event='trace-end',installed=True)]
        entry=dict(inputs=dict(metadata=dict(sha256=self.target['game_sha256'],owned=True)),capture_complete=True)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'trace.jsonl'
            def write(observed):
                path.write_text(''.join(json.dumps(r)+'\n' for r in observed))
                entry['inputs'].update(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),bytes=path.stat().st_size)
            write(rows);check_capture(path,entry)
            path.write_text(path.read_text()+'\n')
            with self.assertRaisesRegex(ValueError,'hash/length'):check_capture(path,entry)
            write(rows[:-1])
            with self.assertRaisesRegex(ValueError,'completion'):check_capture(path,entry)
            changed=copy.deepcopy(rows);changed[0]['owned']=False;write(changed)
            with self.assertRaisesRegex(ValueError,'metadata'):check_capture(path,entry)

    def test_expected_failure_does_not_accept_missing_or_changed_report(self):
        entry=next(e for e in self.manifest['entries'] if e['id']=='adaptive-size2')
        observed=dict(binary_sha256=self.target['game_sha256'],differences=[{}]*4,
                      stored_size=2,promotion_disabled=False,forced_east_boundary=False)
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory);report=output/entry['report']
            context=dict(output=output,python=sys.executable,binary='controlled.dll',timeout=1)
            with patch('run_wc3_pathfinding_corpus.subprocess.run',return_value=subprocess.CompletedProcess([],1)):
                self.assertIn('missing',run_entry(entry,context,self.target)['failure'])
            def completed(command,**kwargs):
                report.write_text(json.dumps(observed))
                return subprocess.CompletedProcess(command,1)
            with patch('run_wc3_pathfinding_corpus.subprocess.run',side_effect=completed):
                result=run_entry(entry,context,self.target)
                self.assertTrue(result['verified_expected_status'])
                self.assertEqual(result['expected_status'],'known-reference-difference')
                self.assertIn('stale',run_entry(entry,context,self.target)['failure'])
                report.unlink();observed['differences']=[]
                self.assertIn('length',run_entry(entry,context,self.target)['failure'])
                report.unlink()
            with patch('run_wc3_pathfinding_corpus.subprocess.run',return_value=subprocess.CompletedProcess([],0)):
                self.assertIn('unexpected process exit',run_entry(entry,context,self.target)['failure'])

    def test_report_cannot_hide_failed_assertions_or_unexpected_mismatches(self):
        observed=dict(binary_sha256=self.target['game_sha256'],cases=1,mismatches=[])
        check_report(observed,self.entry,self.target)
        for key,value in [('binary_sha256','0'*64),('cases',0),('mismatches',[{}]),('passed',False)]:
            changed=dict(observed,**{key:value})
            with self.assertRaises(ValueError):check_report(changed,self.entry,self.target)

    def test_cli_records_the_executed_profile_verifier_source(self):
        rows=json.loads((ROOT/'tools/ghidra/fixtures/retail-ground-profile-1.27.json').read_text())['observations']
        with tempfile.TemporaryDirectory() as directory:
            archive=Path(directory);capture=archive/'profile.jsonl'
            capture.write_text(''.join(json.dumps(row)+'\n' for row in rows))
            manifest=copy.deepcopy(self.manifest)
            entry=next(e for e in manifest['entries'] if e['id']=='live-stock-profile')
            entry['inputs']=dict(capture=capture.name,sha256=hashlib.sha256(capture.read_bytes()).hexdigest(),
                                 bytes=capture.stat().st_size,metadata={k:v for k,v in rows[0].items() if k not in ('event','pid')})
            entry['command'][2]='{archive}/'+capture.name
            path=archive/'manifest.json';path.write_text(json.dumps(manifest))
            output=archive/'new'
            result=subprocess.run([sys.executable,str(ROOT/'tools/ghidra/run_wc3_pathfinding_corpus.py'),
                '--manifest',str(path),'--archive',str(archive),'--output',str(output),
                '--only','live-stock-profile'],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            summary=json.loads((output/'corpus-results.json').read_text())
            self.assertTrue(summary['passed'])
            self.assertIn('tools/frida/verify_wc3_profile_trace.py',summary['source_sha256'])
            self.assertIn('tools/ghidra/generate_wc3_math_tables.py',summary['source_sha256'])


if __name__=='__main__':unittest.main()
