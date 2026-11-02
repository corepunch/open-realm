"""Frozen public mixed-rank producer and complete ordered repeat."""
import copy
import gzip
import hashlib
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_formation_rank_trace import verify, SOURCE_HASHES

class FormationRankTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=ROOT/'tools/ghidra/fixtures'
        cls.raw=gzip.decompress((cls.fixture/'retail-formation-ranks-1.27.jsonl.gz').read_bytes())
        cls.rows=[json.loads(l)for l in cls.raw.splitlines()]
        cls.cert=json.loads((cls.fixture/'retail-formation-ranks-1.27.json').read_text())

    def test_complete_public_creation_and_chaos(self):
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(),self.cert['filtered_sha256'])
        result=verify(self.rows)
        for name in ('events','digest','counts','authored_ranks','layouts','mixed_buckets'):
            self.assertEqual(result[name],self.cert[name])
        self.assertEqual(result['authored_ranks'],[0,1,2,3,0,1,3,3])

    def test_entire_ordered_repeat(self):
        raw=gzip.decompress((self.fixture/'retail-formation-ranks-1.27-repeat.jsonl.gz').read_bytes())
        self.assertEqual(hashlib.sha256(raw).hexdigest(),self.cert['repeat_filtered_sha256'])
        self.assertEqual(verify(self.rows)['stream'],verify([json.loads(l)for l in raw.splitlines()])['stream'])

    def test_source_provenance(self):
        for name,digest in SOURCE_HASHES.items():
            if name=='map':continue
            raw=gzip.decompress((self.fixture/'sources'/(digest+'.gz')).read_bytes())
            self.assertEqual(hashlib.sha256(raw).hexdigest(),digest,name)

    def test_readonly_spacing_global_provenance(self):
        raw=(self.fixture/'retail-formation-spacing-1.27.json').read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),self.cert['spacing_sha256'])
        witness=json.loads(raw)
        self.assertEqual(hashlib.sha256(witness['source'].encode()).hexdigest(),witness['source_sha256'])
        self.assertEqual(witness['native_sha256'],self.rows[0]['sha256'])
        self.assertEqual(witness['records'][0]['payload']['words'],[0x40b00001,0x40000000,0x40200000]*2)

    def test_rejects_incomplete_or_changed_ranks(self):
        for change in ('footer','sample','source','setter','rebind','rank','bucket','projection','owner','pose','type'):
            rows=copy.deepcopy(self.rows)
            if change=='footer':rows.pop()
            elif change=='sample':rows=[r for r in rows if r.get('event')!='marker' or 'tick=29 label=sample ' not in r['value']]
            elif change=='source':rows[0]['source_sha256']['map']='0'*64
            elif change in ('setter','rebind'):
                r=[r for r in rows if r.get('event')=='formation-rank-set'][6]
                if change=='setter':r['after']=0
                else:r['mover']='0x123'
            elif change in ('rank','bucket','projection'):
                r=next(r for r in rows if r.get('event')=='formation-rank-buckets')
                if change=='rank':r['members'][0]['moverFlags']=4096
                elif change=='bucket':r['buckets'][0]['members'].reverse()
                else:r['buckets'][0]['projected'].pop()
            elif change in ('owner','pose'):
                r=next(r for r in rows if r.get('event')=='formation-rank-layout')['after']
                if change=='owner':r['identity'][0]+=1
                else:r['members'][0]['pose'][2]+=1
            else:next(r for r in rows if r.get('event')=='formation-rank-marker')['value']='PATHRANK label=rebound type=0'
            with self.subTest(change=change),self.assertRaises(ValueError):verify(rows)
if __name__=='__main__':unittest.main()
