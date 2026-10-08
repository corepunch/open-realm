"""Native visibility evidence must retain the hidden destination and cadence."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from research.verify_target166_live import hidden_policy


class TargetVisibilityEvidence(unittest.TestCase):
    def rows(self, visible=False, countdown=7):
        return [dict(event='gtick', g='g', cd=countdown, unseen=3, path=dict(dest=[11,22])),
                dict(event='vis-query', role='group-callback', flags=0, mode=4, result=int(visible)),
                dict(event='sample', dest=[11,22]),
                dict(event='gtick-end', g='g', cd=max(0,countdown-1), unseen=0 if visible else 4)]

    def test_hidden_visits_keep_destination_and_count_down(self):
        self.assertEqual(hidden_policy(self.rows()),1)
        self.assertEqual(hidden_policy(self.rows(countdown=0)),1)

    def test_reacquisition_clears_unseen_without_forcing_resample(self):
        self.assertEqual(hidden_policy(self.rows(visible=True)),0)
        rows=self.rows(visible=True,countdown=0)
        rows[2]['dest']=[33,44]
        self.assertEqual(hidden_policy(rows),0)

    def test_hidden_target_cannot_replace_cached_destination(self):
        rows=self.rows();rows[2]['dest']=[33,44]
        with self.assertRaisesRegex(ValueError,'sampled'):
            hidden_policy(rows)

    def test_hidden_countdown_cannot_reset_or_underflow(self):
        for value in (7,99,-1):
            rows=self.rows();rows[-1]['cd']=value
            with self.assertRaisesRegex(ValueError,'cadence'):
                hidden_policy(rows)

    def test_hidden_counter_cannot_reset(self):
        rows=self.rows();rows[-1]['unseen']=0
        with self.assertRaisesRegex(ValueError,'counter'):
            hidden_policy(rows)

    def test_visible_counter_and_countdown_remain_observable(self):
        for field,value in [('unseen',4)]:
            rows=self.rows(visible=True);rows[-1][field]=value
            with self.assertRaisesRegex(ValueError,'reacquisition'):
                hidden_policy(rows)
        rows=self.rows(visible=True);rows[2]['dest']=[33,44]
        with self.assertRaisesRegex(ValueError,'reacquisition'):
            hidden_policy(rows)

    def test_incomplete_owner_visit_and_duplicate_query_are_rejected(self):
        with self.assertRaisesRegex(ValueError,'incomplete'):
            hidden_policy(self.rows()[:-1])
        rows=self.rows();rows.insert(2,copy.deepcopy(rows[1]))
        with self.assertRaisesRegex(ValueError,'incomplete'):
            hidden_policy(rows)

    def test_missing_or_duplicate_sampler_is_rejected(self):
        rows=self.rows();del rows[2]
        with self.assertRaisesRegex(ValueError,'sampler'):
            hidden_policy(rows)
        rows=self.rows();rows.insert(3,copy.deepcopy(rows[2]))
        with self.assertRaisesRegex(ValueError,'sampler'):
            hidden_policy(rows)

    def test_wrong_visibility_policy_is_rejected(self):
        for field,value in [('flags',1),('flags',2),('mode',3)]:
            rows=self.rows();rows[1][field]=value
            with self.assertRaisesRegex(ValueError,'policy'):
                hidden_policy(rows)

    def test_frozen_fixture_keeps_repeats_controls_and_raw_owner_words(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-target-visibility166-1.27.json').read_text())
        modes=[p['metadata']['mode'] for p in frozen['captures'].values()]
        self.assertEqual(modes.count('observe'),4)
        self.assertEqual(modes.count('control'),2)
        self.assertEqual(len(frozen['fog_owner_rows']),238)
        self.assertTrue(all(len(row)==25 for row in frozen['fog_owner_rows']))
        self.assertEqual(len(frozen['families']['loss']),17)
        self.assertEqual(len(frozen['families']['reacquire']),5)


if __name__=='__main__':
    unittest.main()
