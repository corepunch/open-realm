"""Verify public task retirement separately from deferred owner destruction."""
import copy
import gzip
import hashlib
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_mover_retirement_trace import verify, SOURCE_HASHES


class MoverRetirementTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=ROOT/'tools/ghidra/fixtures'
        cls.raw=gzip.decompress((cls.fixture/'retail-mover-retirement-1.27.jsonl.gz').read_bytes())
        cls.rows=[json.loads(l) for l in cls.raw.splitlines()]
        cls.cert=json.loads((cls.fixture/'retail-mover-retirement-1.27.json').read_text())

    def test_complete_pending_and_traveling_kill_remove(self):
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(),self.cert['filtered_sha256'])
        result=verify(self.rows)
        for field in ('events','boundaries','counts','digest'):
            self.assertEqual(result[field],self.cert[field])
        self.assertEqual(result['boundaries'],14)
        self.assertTrue(self.cert['repeat_verified'])

    def test_full_ordered_repeat(self):
        raw=gzip.decompress((self.fixture/'retail-mover-retirement-1.27-repeat.jsonl.gz').read_bytes())
        self.assertEqual(hashlib.sha256(raw).hexdigest(),self.cert['repeat_filtered_sha256'])
        repeat=verify([json.loads(l) for l in raw.splitlines()])
        self.assertEqual(verify(self.rows)['stream'],repeat['stream'])

    def test_source_provenance(self):
        for name,digest in SOURCE_HASHES.items():
            if name=='map':continue
            raw=gzip.decompress((self.fixture/'sources'/(digest+'.gz')).read_bytes())
            self.assertEqual(hashlib.sha256(raw).hexdigest(),digest,name)

    def test_rejects_missing_or_changed_lifecycle(self):
        for change in ('footer','sample','source','orders','velocity','group','counts',
                       'indices','destination','pose','premature','leak','corpse','fifo','row','task'):
            rows=list(self.rows)
            if change=='footer':rows.pop()
            elif change=='sample':rows=[r for r in rows if r.get('event')!='marker' or 'tick=29 label=sample ' not in r['value']]
            else:
                event='metadata' if change=='source' else 'scheduler-mutation-marker' if change=='fifo' else 'mover-retirement-marker'
                label='pending_removed_next' if change=='leak' else 'dead_retained' if change=='corpse' else 'pending_killed'
                index=next(i for i,r in enumerate(rows) if r.get('event')==event and
                           (event=='metadata' or 'label='+label in r['value']))
                rows[index]=r=copy.deepcopy(rows[index])
                if change=='source':r['source_sha256']['map']='0'*64
                elif change=='orders':r['orderCount']=1
                elif change=='velocity':r['velocity']=[1,0]
                elif change=='group':r['groupIdentity']=[0,0]
                elif change=='counts':r['routeCounts']=[1,0]
                elif change=='indices':r['routeIndices']=[0,-1]
                elif change=='destination':r['destination']=[0,0]
                elif change=='pose':r['pose'][0]+=1
                elif change=='premature':r['pathLive']=False
                elif change=='leak':r['moverLive']=True
                elif change=='corpse':r['originalGroupPathLive']=True
                elif change=='row':r['originalGroupMembers']=0
                elif change=='task':r['taskHead']=[-1,-1]
                elif change=='fifo':
                    r['buckets'][3]['queue'].pop()
                    r['buckets'][3]['tail']=r['buckets'][3]['queue'][-1]
            with self.subTest(change=change),self.assertRaises(ValueError):verify(rows)


if __name__=='__main__':unittest.main()
