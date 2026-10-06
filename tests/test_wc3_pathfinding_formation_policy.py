"""Original UI-policy provenance and repeated full first owner stages."""
import copy
import gzip
import hashlib
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_formation_policy_trace import verify,header,SOURCE_HASHES

class FormationPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=ROOT/'tools/ghidra/fixtures'
        cls.cert=json.loads((cls.fixture/'retail-formation-policy-1.27.json').read_text())
        cls.rows=[]
        for record in cls.cert['records']:
            raw=gzip.decompress((cls.fixture/record['file']).read_bytes())
            if hashlib.sha256(raw).hexdigest()!=record['filtered_sha256']:
                raise ValueError('frozen policy checksum differs')
            cls.rows.append([json.loads(l)for l in raw.splitlines()])

    def test_complete_owned_producers_and_first_tick_repeat(self):
        results=[verify(rows)for rows in self.rows]
        for r,c in zip(results,self.cert['records']):self.assertEqual(r,c['verification'])
        self.assertEqual(results[0]['signature'],results[2]['signature'])
        self.assertEqual(results[1]['signature'],results[3]['signature'])
        self.assertEqual([r['policy_requests']for r in results],[2,3,2,3,6])
        self.assertEqual([r['first_tick']['flags']&14 for r in results],[0,14,0,14,14])

    def test_engine_oracle_is_generated_from_validated_native_stages(self):
        results=[verify(rows)for rows in self.rows[:4]]
        self.assertEqual(header(results),(ROOT/'games/warcraft-3/game/tests/fixtures/retail_formation_policy_109.h').read_text())

    def test_reviewed_observer_producer_and_saved_ghidra(self):
        for name,digest in SOURCE_HASHES.items():
            if name in ('map','wc3_ui_input.exe','wc3_ui_input.exe.so'):continue
            raw=gzip.decompress((self.fixture/'sources'/(digest+'.gz')).read_bytes())
            self.assertEqual(hashlib.sha256(raw).hexdigest(),digest,name)
        raw=(self.fixture/'retail-formation-policy-ghidra-1.27.json').read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),self.cert['ghidra_sha256'])
        evidence=json.loads(raw)
        self.assertEqual(evidence['native_sha256'],self.cert['native_sha256'])
        functions={r['address']:r for r in evidence['functions']}
        self.assertEqual(functions['6f16d7e0']['xrefs'],[])
        self.assertEqual(functions['6f16dc90']['xrefs'],[
            '6f05a65a:UNCONDITIONAL_CALL','6f05bb6a:UNCONDITIONAL_CALL','6f89cd99:UNCONDITIONAL_CALL'])
        for address in ('6f89caf0','6f16b2f0','6f16fd90'):
            self.assertIn('Payoff109',functions[address]['comment'])

    def test_queued_diagnostic_does_not_assume_policy_survives_reconstruction(self):
        rows=self.rows[4];verify(rows)
        layouts=[r for r in rows if r['event']=='formation-rank-layout']
        self.assertEqual([r['before']['flags']&14 for r in layouts],[14,14,0,0,0,0])
        self.assertEqual([len(r['before']['members'])for r in layouts],[6,1,1,1,1,1])

    def test_rejects_missing_or_changed_producer_and_stage_observations(self):
        for mutation in ('footer','sample','source','helper','input','option','caller','owner','member','offset','pose','commit'):
            rows=copy.deepcopy(self.rows[1])
            if mutation=='footer':rows.pop()
            elif mutation=='sample':rows=[r for r in rows if r['event']!='marker' or 'tick=29 label=sample ' not in r['value']]
            elif mutation=='source':rows[0]['source_sha256']['wc3_ui_input.c']='0'*64
            elif mutation=='helper':next(r for r in rows if r['event']=='player-input-helper')['output']='input rejected'
            elif mutation=='input':next(r for r in rows if r['event']=='player-point-action-begin')['flags']=8
            elif mutation in ('option','caller'):
                r=next(r for r in rows if r['event']=='formation-policy-set');r['after'if mutation=='option'else'caller']=0
            elif mutation in ('owner','member','offset','pose'):
                r=next(r for r in rows if r['event']=='pair-group-phase-end' and r['phase']=='decide')
                if mutation=='owner':r['identity']=[-1,-1]
                elif mutation=='member':r['members'].reverse()
                elif mutation=='offset':r['members'][0]['row'][3]^=1
                else:r['members'][0]['row'][8]=0
            else:next(r for r in rows if r['event']=='velocity-commit')['after'][2]^=1
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):verify(rows)

if __name__=='__main__':unittest.main()
