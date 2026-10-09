"""Pin completed non-unit scheduling evidence and reject damaged captures."""
import copy
import gzip
import hashlib
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_scheduler_nonunit_trace import verify, SOURCE_HASHES


class SchedulerNonunitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=ROOT/'tools/ghidra/fixtures'
        cls.raw=gzip.decompress((cls.fixture/'retail-scheduler-nonunit-1.27.jsonl.gz').read_bytes())
        cls.rows=[json.loads(l) for l in cls.raw.splitlines()]
        cls.cert=json.loads((cls.fixture/'retail-scheduler-nonunit-1.27.json').read_text())

    def test_complete_repeated_spider_projectile_searches(self):
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(),self.cert['filtered_sha256'])
        result=verify(self.rows)
        for field in ('events','transactions','route','counts','digest'):
            self.assertEqual(result[field],self.cert[field])
        self.assertTrue(self.cert['repeat_verified'])
        self.assertEqual(result['route']['work'],4)
        self.assertEqual(result['route']['words'],[[0x41280000,0x41e40000],
            [0x41180000,0x41e40000],[0x41080000,0x41e40000],[0x40e3c000,0x41e00000]])

    def test_frozen_original_producer_and_observer(self):
        for name,digest in SOURCE_HASHES.items():
            if name=='map':continue
            self.assertEqual(hashlib.sha256(gzip.decompress(
                (self.fixture/'sources'/(digest+'.gz')).read_bytes())).hexdigest(),digest)

    def test_rejects_incomplete_or_changed_producer_and_search(self):
        for change in ('footer','sample','stop','source','identity','bridge','class','quota',
                       'footprint','charge','timestamp','route','bucket','fifo','cadence'):
            rows=list(self.rows)
            if change=='footer':rows.pop()
            elif change in ('sample','stop'):
                needle='tick=29 label=sample ' if change=='sample' else 'tick=100 label=complete '
                rows=[r for r in rows if r.get('event')!='marker' or needle not in r['value']]
            else:
                event={'source':'metadata','identity':'scheduler-nonunit-producer',
                       'bridge':'scheduler-class15-producer','class':'scheduler-class','quota':'search',
                       'footprint':'search','charge':'scheduler-fine-request','timestamp':'scheduler-fine-request',
                       'route':'fine-result','bucket':'scheduler-fine-request',
                       'fifo':'scheduler-admission','cadence':'scheduler-update'}[change]
                index=next(i for i,r in enumerate(rows) if r.get('event')==event and
                    (change!='identity' or r['entry']==0x6d3190))
                rows[index]=r=copy.deepcopy(rows[index])
                if change=='source':r['source_sha256']['map']='0'*64
                elif change=='identity':r['vtable']+=4
                elif change=='bridge':r['caller']+=1
                elif change=='class':r['after']^=0x04000000
                elif change=='quota':r['budget']=5000
                elif change=='footprint':r['footprint']=1
                elif change=='charge':r['after']['work']+=1
                elif change=='timestamp':r['after']['times'][0]+=1
                elif change=='route':r['words'][0][0]+=1
                elif change=='bucket':r['bucketOffset']-=112
                elif change=='fifo':r['stateAfter']['tail']='0xffffffff'
                else:r['after']['countdown']+=1
            with self.subTest(change=change),self.assertRaises(ValueError):verify(rows)


if __name__=='__main__':unittest.main()
