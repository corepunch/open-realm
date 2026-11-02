"""Mixed selection point contracts distinguish immediate and deferred admission."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_selected_mixed_trace import render_header,verify_mixed_policy


class SelectedMixedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-selected-mixed-1.27.json').read_text())
        cls.independent=json.loads((ROOT/'tools/ghidra/fixtures/retail-selected-independent-1.27.json').read_text())

    def test_idle_admission_and_active_head_preservation(self):
        for c in self.fixture['cases']:
            verify_mixed_policy(c['producer'],c['admissions'],c['neighbors'],c['joined'])
            self.assertEqual([r['countBefore'] for r in c['admissions']],[0,1])
            self.assertEqual([r['countAfter'] for r in c['admissions']],[1,2])

    def test_native_target_clicks_and_nonshift_packets_are_rejected(self):
        c=self.fixture['cases'][0]
        for field,value in [('target',[1108,1108]),('flags',8),('order',851983)]:
            with self.subTest(field=field):
                producer=copy.deepcopy(c['producer'])
                r=next(r for r in producer if r['event']==('player-order-variant' if field=='target' else 'player-point-action-begin'))
                r[field]=value
                with self.assertRaises(ValueError):verify_mixed_policy(producer,c['admissions'],c['neighbors'],False)

    def test_replacing_or_not_starting_current_heads_is_rejected(self):
        c=self.fixture['cases'][0]
        for index,key,value in [(0,'after',[-1,-1]),(0,'countBefore',1),(1,'countAfter',1),(1,'after',[-1,-1])]:
            with self.subTest(index=index,key=key):
                admissions=copy.deepcopy(c['admissions']);admissions[index][key]=value
                with self.assertRaises(ValueError):verify_mixed_policy(c['producer'],admissions,c['neighbors'],False)

    def test_completed_peer_and_live_peer_take_distinct_cohort_paths(self):
        self.assertEqual([c['joined'] for c in self.fixture['cases']],[False,False,True,True])
        c=self.fixture['cases'][2];rows=copy.deepcopy(c['neighbors'])
        accepted=next(r for r in rows if r.get('accepted'))
        accepted['candidate']['unit']=accepted['source']['unit']
        with self.assertRaises(ValueError):verify_mixed_policy(c['producer'],c['admissions'],rows,True)

    def test_individual_input_times_and_every_motion_word_are_retained(self):
        self.assertEqual([len(c['engine_motion']) for c in self.fixture['cases']],[361,361,418,370])
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_selected_mixed.h').read_text(),render_header(self.fixture))

    def test_independent_active_heads_survive_one_common_request(self):
        for c in self.independent['cases']:
            searches=verify_mixed_policy(c['producer'],c['admissions'],c['neighbors'],True,True)
            self.assertEqual([r['result'] for r in searches],[0,1])
            self.assertNotEqual(c['admissions'][0]['before'],c['admissions'][1]['before'])

    def test_independent_replacement_and_same_head_are_rejected(self):
        c=self.independent['cases'][0]
        for field,value in [('after',[-1,-1]),('before',c['admissions'][0]['before']),('countAfter',1)]:
            admissions=copy.deepcopy(c['admissions']);admissions[1][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):
                verify_mixed_policy(c['producer'],admissions,c['neighbors'],True,True)

    def test_independent_cohort_identity_and_self_matches_are_rejected(self):
        c=self.independent['cases'][0]
        for field in ('unit','previous'):
            rows=copy.deepcopy(c['neighbors']);accepted=next(r for r in rows if r.get('accepted'))
            accepted['candidate'][field]=accepted['source']['unit'] if field=='unit' else [-1,-1]
            with self.subTest(field=field),self.assertRaises(ValueError):
                verify_mixed_policy(c['producer'],c['admissions'],rows,True,True)

    def test_independent_complete_motion_repeats_and_header_is_exact(self):
        cases=self.independent['cases'];self.assertEqual(cases[0]['engine_motion'],cases[1]['engine_motion'])
        self.assertEqual([len(c['engine_motion']) for c in cases],[511,511])
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_selected_independent.h').read_text(),render_header(self.independent))


if __name__=='__main__':unittest.main()
