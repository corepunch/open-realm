"""Keep public queue cancellation, owner change and next admission evidence exact."""
import copy
import gzip
import hashlib
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_scheduler_mutation_trace import verify, SOURCE_HASHES


class SchedulerMutationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=ROOT/'tools/ghidra/fixtures'
        cls.raw=gzip.decompress((cls.fixture/'retail-scheduler-mutation-1.27.jsonl.gz').read_bytes())
        cls.rows=[json.loads(l) for l in cls.raw.splitlines()]
        cls.cert=json.loads((cls.fixture/'retail-scheduler-mutation-1.27.json').read_text())

    def test_complete_public_mutations_and_search_charges(self):
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(),self.cert['filtered_sha256'])
        result=verify(self.rows)
        for field in ('events','mutations','requeued','counts','digest'):
            self.assertEqual(result[field],self.cert[field])
        self.assertEqual(result['mutations'],[['after_stop',52,20],['after_owner',50,19],['after_remove',48,18]])
        self.assertTrue(self.cert['repeat_verified'])

    def test_full_ordered_repeat_matches_without_process_addresses(self):
        raw=gzip.decompress((self.fixture/'retail-scheduler-mutation-1.27-repeat.jsonl.gz').read_bytes())
        self.assertEqual(hashlib.sha256(raw).hexdigest(),self.cert['repeat_filtered_sha256'])
        repeat=verify([json.loads(l) for l in raw.splitlines()])
        self.assertEqual(verify(self.rows)['stream'],repeat['stream'])

    def test_frozen_read_only_observer_and_public_producer(self):
        for name,digest in SOURCE_HASHES.items():
            if name=='map':continue
            raw=gzip.decompress((self.fixture/'sources'/(digest+'.gz')).read_bytes())
            self.assertEqual(hashlib.sha256(raw).hexdigest(),digest,name)

    def test_rejects_missing_or_corrupt_boundary_evidence(self):
        for change in ('footer','sample','source','actor','survivors','count','unlink','next','class','quota','charge','fine','timestamp','cadence'):
            rows=list(self.rows)
            if change=='footer':rows.pop()
            elif change=='sample':rows=[r for r in rows if r.get('event')!='marker' or 'tick=29 label=sample ' not in r['value']]
            else:
                event={'source':'metadata','actor':'scheduler-mutation-actor','survivors':'scheduler-mutation-marker',
                       'count':'scheduler-admission','unlink':'scheduler-unlink','next':'scheduler-admission',
                       'class':'scheduler-class','quota':'search','charge':'scheduler-acc-request','fine':'fine-result',
                       'timestamp':'scheduler-acc-request','cadence':'scheduler-update'}[change]
                start=next(i for i,r in enumerate(rows) if r.get('event')=='scheduler-mutation-marker' and 'label=after_remove' in r['value']) if change=='next' else 0
                index=next(i for i,r in enumerate(rows) if i>=start and r.get('event')==event and
                           (change!='survivors' or 'label=after_stop' in r['value']) and
                           (change!='next' or r['bucketOffset']==84))
                rows[index]=r=copy.deepcopy(rows[index])
                if change=='source':r['source_sha256']['map']='0'*64
                elif change=='actor':r['path']='0x0'
                elif change=='survivors':
                    r['buckets'][3]['queue'].pop();r['buckets'][3]['tail']=r['buckets'][3]['queue'][-1]
                elif change=='count':r['after'][3]+=1
                elif change=='unlink':r['after']['links']=[1,0]
                elif change=='next':r['result']=0
                elif change=='class':r['after']^=0x04000000
                elif change=='quota':r['budget']=2000
                elif change=='charge':r['after']['work']+=1
                elif change=='fine':r['work']+=1
                elif change=='timestamp':r['after']['times'][1]+=1
                else:r['after']['countdown']+=1
            with self.subTest(change=change),self.assertRaises(ValueError):verify(rows)


if __name__=='__main__':unittest.main()
