"""Pin complete retail contention evidence and reject incomplete scheduler captures."""
import copy
import gzip
import hashlib
import json
import re
import sys
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_scheduler_trace import verify,SOURCE_HASHES


class SchedulerContentionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw=gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-scheduler-contention-1.27.jsonl.gz').read_bytes())
        cls.rows=[json.loads(line) for line in cls.raw.splitlines()]
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-scheduler-contention-1.27.json').read_text())

    def test_complete_retail_fifo_work_and_waits(self):
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(),self.fixture['filtered_sha256'])
        result=verify(self.rows)
        for field in ('events','digest','first_window','waits','retired','pending'):
            self.assertEqual(result[field],self.fixture[field])
        self.assertTrue(self.fixture['repeat_verified'])
        self.assertEqual(result['pending_boundary'],self.fixture['pending_boundary'])

    def test_captured_window_matches_compiled_production_fixture(self):
        text=(ROOT/'games/warcraft-3/game/tests/retail_scheduler_contention.h').read_text()
        self.assertIn(self.fixture['capture_sha256'],text)
        rows=[]
        for values in re.findall(r'\{([^{}]+)\}',text):
            rows.append([int(v) if v!='UINT32_MAX' else '0x0' for v in values.split(',')])
        self.assertEqual(rows,self.fixture['first_window'])

    def test_producer_and_frozen_observer_sources(self):
        for name,digest in SOURCE_HASHES.items():
            path=ROOT/'tools/ghidra/fixtures/sources'/(digest+'.gz')
            raw=gzip.decompress(path.read_bytes()) if path.exists() else (ROOT/'tools/frida'/name).read_bytes()
            self.assertEqual(hashlib.sha256(raw).hexdigest(),digest)

    def test_rejects_missing_and_altered_evidence(self):
        for change in ('footer','source','sample','complete','search','charge','fifo','cadence','timestamp'):
            rows=list(self.rows)
            if change=='footer':rows.pop()
            elif change in ('sample','complete'):
                needle='tick=37 label=sample ' if change=='sample' else 'tick=300 label=complete '
                rows=[r for r in rows if needle not in r.get('value','')]
            elif change=='search':rows.pop(next(i for i,r in enumerate(rows) if r.get('event')=='search'))
            else:
                event={'source':'metadata','charge':'scheduler-acc-request','fifo':'scheduler-admission',
                       'cadence':'scheduler-update','timestamp':'scheduler-fine-request'}[change]
                index=next(i for i,r in enumerate(rows) if r.get('event')==event)
                rows[index]=r=copy.deepcopy(rows[index])
                if change=='source':r['source_sha256']['wc3_pathfinding.js']='0'*64
                elif change=='charge':r['after']['work']+=1
                elif change=='fifo':r['stateAfter']['head']='0xffffffff'
                elif change=='cadence':r['after']['countdown']+=1
                else:r['after']['times'][0]+=1
            with self.subTest(change=change),self.assertRaises(ValueError):verify(rows)


if __name__=='__main__':unittest.main()
