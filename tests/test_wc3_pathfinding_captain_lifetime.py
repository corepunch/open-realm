"""Final-binding controls retain logical identity, signed withdrawal and fresh owners."""
import copy
import hashlib
import gzip
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_captain_lifetime_trace import lifetime_state,render_header,verify_contract


def observer_rows(variant):
    """Reconstruct portable observer records from frozen native words, including address reuse."""
    s=variant['state'];rows=[]
    def births(start):
        for i in range(start,start+13):
            row=dict(event='movement-mask-publication',identity=s['births'][i],mover='unit'+str(i%13),
                     rawcode=1749240903 if i%13==0 else 1751543663,category=202)
            rows.extend([row.copy(),row.copy()])
    def footprint(r):
        _,owner,mask,radius,stored=r
        return dict(event='group-footprint-state',identity=[9,9],sharedIdentity=s['owners'][owner],
                    counter=r[0],sharedRadius=radius,footprint=stored,
                    members=[dict(owner=[9,9],resolved='unit'+str(i))for i in range(13)if mask&(1<<i)])
    def motion(r):
        i,time,x,y,vx,vy,heading=r
        return dict(event='velocity-commit',mover='unit'+str(i%13),after=[time,0,x,y,vx,vy,0,heading])
    births(0)
    if variant['name']=='remove':
        rows.extend(footprint(r)for r in s['footprints']if r[1]==0)
        rows.extend(motion(r)for r in s['motion']if r[0]<13)
        births(13)
        rows.extend(footprint(r)for r in s['footprints']if r[1]>0)
        rows.extend(motion(r)for r in s['motion']if r[0]>=13)
    else:
        rows.extend(footprint(r)for r in s['footprints']);rows.extend(motion(r)for r in s['motion'])
    rows.extend(dict(event='captain-shared-publish',counter=r[0],identityBefore=r[4],identity=r[5],before=r[2],after=r[3])for r in s['publication'])
    rows.extend(dict(event='metadata-row',parent=p,child=c,kind=k,word=w)for p,c,k,w in s['public_records'])
    rows.extend(dict(event='captain-marker',value=v)for v in s['markers'])
    rows.extend(dict(event=e,counter=c,counts=counts,**({'delta':d}if d is not None else {}))for e,c,counts,d in s['roster'])
    return rows


class CaptainLifetimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-captain-lifetime-1.27.json').read_text())

    def rejects(self,modify):
        f=copy.deepcopy(self.fixture);modify(f)
        with self.assertRaises(ValueError):verify_contract(f)

    def test_three_complete_repeated_controls_and_literal_header(self):
        verify_contract(self.fixture)
        header=ROOT/'games/warcraft-3/game/tests/retail_captain_lifetime_120.h'
        self.assertEqual(header.read_text(),render_header(self.fixture['variants']))
        self.assertEqual(hashlib.sha256(header.read_bytes()).hexdigest(),self.fixture['engine_header_sha256'])
        for v in self.fixture['variants']:
            self.assertEqual(lifetime_state(observer_rows(v),v['name']),v['state'])

    def test_no_repeated_capture_or_motion_prefix_can_replace_complete_journey(self):
        self.rejects(lambda f:f['variants'][0]['cases'].pop())
        self.rejects(lambda f:f['variants'][0]['cases'].__setitem__(1,f['variants'][0]['cases'][0]))
        self.rejects(lambda f:f['variants'][0]['state']['motion'].pop())
        self.rejects(lambda f:f.__setitem__('whole_retail_pathfinder',True))

    def test_collection_requires_zero_references_and_invalidates_old_identity(self):
        for field,value in [(2,[1,0,0,0]),(5,[1472,1942])]:
            self.rejects(lambda f:next(r for r in reversed(f['variants'][0]['state']['publication'])if r[1]==0).__setitem__(field,value))
        self.rejects(lambda f:next(r for r in reversed(f['variants'][0]['state']['publication'])if r[1]==0).__setitem__(0,1331))

    def test_refill_must_have_new_canonical_shared_generation(self):
        self.rejects(lambda f:f['variants'][0]['state']['owners'].__setitem__(1,f['variants'][0]['state']['owners'][0]))
        self.rejects(lambda f:f['variants'][0]['state']['births'].pop())

    def test_remove_is_signed_withdrawal_and_stop_retarget_retain_roster(self):
        self.rejects(lambda f:next(r for r in f['variants'][0]['state']['roster']if r[3]==-1).__setitem__(3,1))
        self.rejects(lambda f:f['variants'][1]['state']['roster'].pop())

    def test_duplicate_or_missing_public_records_and_uint32_types_are_rejected(self):
        v=self.fixture['variants'][0];rows=observer_rows(v)
        at=next(i for i,r in enumerate(rows)if r.get('event')=='metadata-row')
        for bad in (rows[:at]+rows[at+1:],rows[:at]+[rows[at]]+rows[at:]):
            with self.assertRaises(ValueError):lifetime_state(bad,'remove')
        for word in (False,1.0,-1,0x100000000):
            bad=copy.deepcopy(rows);bad[at]['word']=word
            with self.assertRaisesRegex(ValueError,'uint32'):lifetime_state(bad,'remove')

    def test_reused_mover_address_requires_fresh_canonical_birth(self):
        rows=observer_rows(self.fixture['variants'][0]);pubs=[r for r in rows if r.get('event')=='movement-mask-publication']
        pubs[26]['identity']=pubs[0]['identity']
        with self.assertRaises(ValueError):lifetime_state(rows,'remove')

    def test_signed_delta_and_membership_abis_are_persisted_in_ghidra(self):
        schema=json.loads((ROOT/'tools/ghidra/fixtures/retail-pathfinding-types-1.27.json').read_text())
        methods={m['address']:m for m in schema['methods']}
        count=methods['6f9d0650'];self.assertEqual(count['returns'],'void')
        self.assertEqual(count['parameters'][-1],dict(name='delta',type='i32',storage=8))
        for a in ('6f9d5610','6f9cf680','6f9d34b0'):
            self.assertEqual(methods[a]['parameters'][-1]['storage'],4)
        self.assertEqual(methods['6f9d34b0']['returns'],'u32')
        self.assertEqual(methods['6f9d16c0']['returns'],'void')
        self.assertEqual(methods['6f9d16c0']['parameters'],[dict(name='captain',type='ptr:WC3CaptainAIPrefix',storage='ECX')])
        self.assertFalse(self.fixture['ghidra']['unsaved_changes'])

    def test_saved_ghidra_and_actual_capture_source_preimages_are_readable(self):
        evidence=ROOT/'tools/ghidra/fixtures'/self.fixture['ghidra']['readback']
        self.assertEqual(hashlib.sha256(evidence.read_bytes()).hexdigest(),self.fixture['ghidra']['readback_sha256'])
        saved=json.loads(evidence.read_text());self.assertEqual(saved['program'],'game.dll')
        self.assertFalse(saved['unsaved_changes']);self.assertEqual(len(saved['functions']),8)
        for f in saved['functions']:
            self.assertTrue(f['exits']);self.assertGreater(f['instructions'],0)
            if f['address']!='6f166060':self.assertTrue(f['callers']) # Path activation is virtual.
            self.assertIn('Recovered descriptive name',f['comment'])
        for v in self.fixture['variants']:
            for c in v['cases']:
                for sha in c['metadata']['source_sha256'].values():
                    blob=ROOT/'tools/ghidra/fixtures/sources'/(sha+'.gz')
                    self.assertEqual(hashlib.sha256(gzip.decompress(blob.read_bytes())).hexdigest(),sha)

    def test_ai_refill_uses_existing_captain_and_does_not_replay_main(self):
        src=(ROOT/'tools/frida/wc3_captain_lifetime_probe.ai').read_text()
        self.assertEqual(src.count('call CreateCaptains()'),1)
        self.assertEqual(src.count('call PathCaptainLifetimeRecruit()'),2)
        self.assertNotIn('StartCampaignAI',src)
        fixture=(ROOT/'games/warcraft-3/tests/resources-src/Scripts/test_captain_lifetime.ai').read_text()
        self.assertEqual(fixture,src.replace("'hCLG'","'hBGL'").replace("'hfoo'","'hBGM'"))


if __name__=='__main__':unittest.main()
