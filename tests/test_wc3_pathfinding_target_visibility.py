"""Native visibility evidence must retain the hidden destination and cadence."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from research.verify_target166_live import hidden_policy
sys.path.insert(0, str(ROOT / 'tools/ghidra/research'))
from verify_target167_live import producer_contract


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


class TargetLossEvidence(unittest.TestCase):
    def rows(self):
        frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-target-loss167-1.27.json').read_text())
        rows=[]
        for contract in frozen['contracts']:
            scene=contract['scene'];counter=contract['producer_c'];target='target';move='move'
            rows.append(dict(event='marker',c=counter-200,value=f'T03 tick=0 s={scene} l=0 label=begin-setup'))
            for c,r,p,s in contract['tasks']:
                rows.append(dict(event='begin-task',c=c,range=r,persistent=p,a3=s,move=move,target=target))
            rows.append(dict(event='marker',c=counter-1,value=contract['before']))
            if scene==8:
                rows.append(dict(event='marker',c=counter,value=f'T03 tick=0 s={scene} l=60 label=begin-produce'))
            rows.append(dict(event='target-lost',c=counter,caller=0x688373,unit=target,
                             a0=0xffffffff,a1=0xffffffff,w20=contract['published'][0],w5c=contract['published'][1]))
            rows.append(dict(event='validate',c=counter,move=move,t=target,result=contract['validation']))
            rows.append(dict(event='on-target-lost',c=counter,move=move,t=target,code=0xd01a4,caller=0x5fdc90))
            if scene==8:
                rows.append(dict(event='marker',c=counter,value=f'T03 tick=0 s={scene} l=60 label=end-produce'))
            rows.append(dict(event='marker',c=counter,value=contract['after']))
        return rows,frozen['contracts']

    def test_completed_loss_and_subscribed_handoffs(self):
        rows,expected=self.rows();self.assertEqual(producer_contract(rows),expected)

    def test_missing_or_duplicate_handler(self):
        for duplicate in (False,True):
            rows,_=self.rows();index=next(i for i,r in enumerate(rows) if r['event']=='on-target-lost')
            if duplicate:rows.insert(index,copy.deepcopy(rows[index]))
            else:del rows[index]
            with self.assertRaisesRegex(ValueError,'handler'):producer_contract(rows)

    def test_loss_cannot_precede_world_presence_publication(self):
        for field in ('w20','w5c'):
            rows,_=self.rows();r=next(r for r in rows if r['event']=='target-lost');r[field]=0
            with self.assertRaisesRegex(ValueError,'published'):producer_contract(rows)

    def test_validation_must_match_target_and_loss_clock(self):
        for field,value in [('t','other'),('c',123),('result',0)]:
            rows,_=self.rows();next(r for r in rows if r['event']=='validate')[field]=value
            with self.assertRaisesRegex(ValueError,'validation'):producer_contract(rows)

    def test_showunit_cannot_return_before_delivery(self):
        rows,_=self.rows();index=next(i for i,r in enumerate(rows) if r['event']=='on-target-lost')
        rows[index],rows[index+1]=rows[index+1],rows[index]
        with self.assertRaisesRegex(ValueError,'returned'):producer_contract(rows)

    def test_public_undo_cannot_reactivate_follow(self):
        rows,_=self.rows();index=next(i for i,r in enumerate(rows) if r['event']=='marker' and 'l=60 label=sample' in r['value'])
        rows[index]['value']=rows[index]['value'].replace(',0 t=',',851971 t=')
        with self.assertRaisesRegex(ValueError,'cancellation'):producer_contract(rows)

    def test_handoff_must_preserve_subscription_and_phase(self):
        for field,value in [('a3',0),('persistent',0),('target','other')]:
            rows,_=self.rows();[r for r in rows if r['event']=='begin-task'][1][field]=value
            with self.assertRaisesRegex(ValueError,'subscribe'):producer_contract(rows)


if __name__=='__main__':
    unittest.main()
