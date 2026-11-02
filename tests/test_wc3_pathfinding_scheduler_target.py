"""Keep target scheduling evidence reproducible and reject incomplete captures."""
import copy
import gzip
import hashlib
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_scheduler_target_trace import verify, PRODUCER_HASH
from verify_wc3_scheduler_trace import SOURCE_HASHES


class SchedulerTargetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixture=ROOT/'tools/ghidra/fixtures'
        cls.raw=gzip.decompress((fixture/'retail-scheduler-target-1.27.jsonl.gz').read_bytes())
        cls.rows=[json.loads(l) for l in cls.raw.splitlines()]
        cls.cert=json.loads((fixture/'retail-scheduler-target-1.27.json').read_text())

    def test_complete_repeated_target_and_point_routes(self):
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(),self.cert['filtered_sha256'])
        result=verify(self.rows)
        for field in ('events','groups','counts','digest'):
            self.assertEqual(result[field],self.cert[field])
        self.assertTrue(self.cert['repeat_verified'])

    def test_frozen_original_producer_and_observer(self):
        for name,digest in [('producer',PRODUCER_HASH)]+[(n,SOURCE_HASHES[n]) for n in ('trace_wc3_pathfinding.py','wc3_pathfinding.js')]:
            path=ROOT/'tools/ghidra/fixtures/sources'/(digest+'.gz')
            raw=gzip.decompress(path.read_bytes()) if path.exists() else (ROOT/'tools/frida'/name).read_bytes()
            self.assertEqual(hashlib.sha256(raw).hexdigest(),digest)

    def test_rejects_missing_or_altered_route_evidence(self):
        for change in ('footer','sample','stop','source','quota','target','class','charge','timestamp','policy','fifo','cadence'):
            rows=list(self.rows)
            if change=='footer':rows.pop()
            elif change in ('sample','stop'):
                needle='tick=29 label=sample ' if change=='sample' else 'tick=300 label=follow_stop_accepted '
                rows=[r for r in rows if r.get('event')!='marker' or needle not in r['value']]
            else:
                event={'source':'metadata','quota':'search','target':'scheduler-target','class':'scheduler-class',
                       'charge':'scheduler-acc-request','timestamp':'scheduler-acc-request','policy':'scheduler-acc-request',
                       'fifo':'scheduler-admission','cadence':'scheduler-update'}[change]
                index=next(i for i,r in enumerate(rows) if r.get('event')==event)
                rows[index]=r=copy.deepcopy(rows[index])
                if change=='source':r['source_sha256']['map']='0'*64
                elif change=='quota':r['budget']=2000
                elif change in ('target','class'):r['after']^=0x04000000
                elif change=='charge':r['after']['work']+=1
                elif change=='timestamp':r['after']['times'][1]+=1
                elif change=='policy':r['bucketOffset']=0
                elif change=='fifo':r['stateAfter']['tail']='0xffffffff'
                else:r['after']['countdown']+=1
            with self.subTest(change=change),self.assertRaises(ValueError):verify(rows)


if __name__=='__main__':unittest.main()
