"""Public pool selection must retain distinct birth, transfer and no-op contracts."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_captain_pool_trace import physical_motion,render_header,verify_contract


class CaptainPoolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-captain-pool-1.27.json').read_text())

    def test_complete_contracts_and_literal_engine_reference(self):
        for name,spec in self.fixture['journeys'].items():verify_contract(spec,name)
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_captain_pool.h').read_text(),render_header(self.fixture))

    def test_transfer_cannot_recruit_by_edict_order(self):
        s=copy.deepcopy(self.fixture['journeys']['transfer']);s['selected']=1
        with self.assertRaises(ValueError):verify_contract(s,'transfer')

    def test_same_owner_cannot_refresh_pool_position(self):
        s=copy.deepcopy(self.fixture['journeys']['same_owner']);s['producer']['markers'][-1]='PATHCAPTAIN pool after recruit primary=851986 peer=0'
        with self.assertRaises(ValueError):verify_contract(s,'same_owner')

    def test_delayed_recreation_requires_a_new_birth(self):
        s=copy.deepcopy(self.fixture['journeys']['reuse']);s['physical_births']=2
        with self.assertRaises(ValueError):verify_contract(s,'reuse')

    def test_delayed_removal_marker_cannot_be_omitted(self):
        s=copy.deepcopy(self.fixture['journeys']['reuse']);s['producer']['markers'].pop(0)
        with self.assertRaises(ValueError):verify_contract(s,'reuse')

    def test_reused_native_address_is_a_new_primary_birth(self):
        pubs=[dict(event='movement-mask-publication',rawcode=1751543663,category=202,mover=m,identity=[1,g])
              for m,g in zip(('old','old','peer','peer','old','old'),(1,1,2,2,3,3))]
        words=list(range(8));rows=pubs+[dict(event='velocity-commit',mover='old',after=words)]
        self.assertEqual(physical_motion(rows,'reuse'),[[0,0,2,3,4,5,7]])
        with self.assertRaises(ValueError):physical_motion(rows[:-2]+rows[-1:],'reuse')
        pubs[-1]['identity']=pubs[-2]['identity']=[1,1]
        with self.assertRaises(ValueError):physical_motion(pubs+rows[-1:],'reuse')

    def test_partial_assault_counts_existing_recruit(self):
        s=copy.deepcopy(self.fixture['journeys']['partial']);s['producer']['recruits'][1][0]=1
        with self.assertRaises(ValueError):verify_contract(s,'partial')

    def test_partial_batch_waits_for_both_enter_callbacks(self):
        s=copy.deepcopy(self.fixture['journeys']['partial'])
        next(r for r in s['admission'] if r[0]=='captain-range-enter-end')[3][3]=2
        with self.assertRaises(ValueError):verify_contract(s,'partial')

    def test_singleton_retry_cannot_be_relabelled_as_pair(self):
        s=copy.deepcopy(self.fixture['journeys']['transfer'])
        next(r for r in s['lifecycle'] if r['event']=='retry-result')['members']=2
        with self.assertRaises(ValueError):verify_contract(s,'transfer')


if __name__=='__main__':unittest.main()
