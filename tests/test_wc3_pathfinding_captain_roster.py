"""Larger retail rosters must retain complete preparation and public timelines."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
from research.verify_captain_roster161_live import DLL,check_rows


class CaptainRosterEvidence(unittest.TestCase):
    def fixture(self,name='twentyfive'):
        scene=json.loads((ROOT/'tools/ghidra/fixtures/retail-captain-roster161-1.27.json').read_text())['scenes'][name]
        rows=[dict(event='metadata',task='payoff161',owned=True,mode='observe',sha256=DLL,source_sha256=scene['sources'])]
        for r in copy.deepcopy(scene['normalized']['events']):
            if r['event']=='roster':
                r['unit']='u'+str(r['unit']);r['captain']=dict(counts=r.pop('counts'))
            elif r['event']=='prepared':
                r['event']='prepared-member';r['unit']='u'+str(r['unit'])
                r['shared']=[r['shared'],0];r['identity']=[r.pop('request'),0]
                r.update(policy=1,bindShared=1,target='0x0')
            else:
                r['event']='captain-call';r['name']='shared-point-publication'
                r['before']=dict(counts=r['before']);r['after']=dict(counts=r['after'])
            rows.append(r)
        for r in copy.deepcopy(scene['normalized']['speeds']):
            if r['event']=='speed':r['event']='captain-speed'
            rows.append(r)
        rows.extend(dict(event='marker',value=m) for m in scene['markers'])
        rows.extend([dict(event='trace-end',installed=True),dict(event='preload-file',complete=True,markers=len(scene['markers']))])
        return rows,scene

    def test_both_complete_rosters_are_accepted(self):
        for scene in ('twentyfour','twentyfive'):
            rows,expected=self.fixture(scene);check_rows(rows,'observe',expected)

    def test_thirteenth_member_is_not_the_last_batch(self):
        rows,expected=self.fixture()
        rows.remove(next(r for r in rows if r.get('event')=='prepared-member' and r['index']==2))
        with self.assertRaisesRegex(ValueError,'preparation timeline'):
            check_rows(rows,'observe',expected)

    def test_physical_batch_size_cannot_be_enlarged(self):
        rows,expected=self.fixture()
        r=next(r for r in rows if r.get('event')=='prepared-member' and r['count']==11)
        r['afterCount']=12;r['afterIndex']=r['index']
        with self.assertRaisesRegex(ValueError,'twelve-row'):
            check_rows(rows,'observe',expected)

    def test_request_sharing_must_be_retained(self):
        rows,expected=self.fixture()
        r=next(r for r in rows if r.get('event')=='prepared-member' and r['index']==2)
        r['shared']=[99999,1]
        with self.assertRaisesRegex(ValueError,'preparation timeline'):
            check_rows(rows,'observe',expected)

    def test_withdrawal_count_is_observable(self):
        rows,expected=self.fixture()
        r=next(r for r in rows if r.get('event')=='captain-call' and r['before']['counts'][1]==24)
        r['before']['counts'][1]=25
        with self.assertRaisesRegex(ValueError,'preparation timeline'):
            check_rows(rows,'observe',expected)

    def test_above_minimum_speed_cannot_be_clamped(self):
        rows,expected=self.fixture()
        next(r for r in rows if r.get('event')=='captain-speed' and r['word']==0x438877a6)['word']=0x43870000
        with self.assertRaisesRegex(ValueError,'decoded policy'):
            check_rows(rows,'observe',expected)

    def test_incomplete_capture_is_rejected(self):
        rows,expected=self.fixture();rows[-1]['complete']=False
        with self.assertRaisesRegex(ValueError,'incomplete public'):
            check_rows(rows,'observe',expected)

    def test_instrumented_control_is_rejected(self):
        rows,expected=self.fixture();rows[0]['mode']='control'
        with self.assertRaisesRegex(ValueError,'instrumented control'):
            check_rows(rows,'control',expected)
