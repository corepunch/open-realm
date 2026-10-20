"""Native gate capacity/lifetime evidence and shared production allocator parity."""
import copy
import ctypes
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_waygate_pool_trace import verify


class WaygatePoolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original=json.loads((ROOT/'tools/ghidra/fixtures/retail-waygate-pool-1.27.json').read_text())
        cls.live=json.loads((ROOT/'tools/ghidra/fixtures/retail-waygate-pool-live-1.27.json').read_text())

    def test_complete_original_allocation_and_release_refill_at_o0_o2(self):
        with tempfile.TemporaryDirectory(prefix='wc3-waygate-')as tmp:
            for opt in ('-O0','-O2'):
                lib=Path(tmp)/(opt+'.so')
                subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',opt,'-fPIC','-shared','-I',str(ROOT),str(ROOT/'tools/ghidra/wc3_pathing_engine_probe.c'),'-o',str(lib)],check=True)
                engine=ctypes.CDLL(str(lib));engine.pathing_waygate_id_allocate.argtypes=[ctypes.POINTER(ctypes.c_uint8)]
                used=(ctypes.c_uint8*256)()
                self.assertEqual([engine.pathing_waygate_id_allocate(used)for _ in range(256)],self.original['ids'])
                self.assertEqual(list(used),self.original['full'])
                used[:]=self.original['freed']
                self.assertEqual([engine.pathing_waygate_id_allocate(used)for _ in range(3)],self.original['refill'])

    def test_original_active_transitions_and_zero_rejection(self):
        self.assertEqual(self.original['active_count'],253)
        self.assertEqual(self.original['active'][-1],dict(identity=0,first=[255,0,0],repeat=[255,0,0]))
        for i,row in enumerate(self.original['active'][:-1],1):
            self.assertEqual(row,dict(identity=i,first=[i,1,1],repeat=[i,1,1]))

    def test_live_full_history_rejects_truncation_bitmap_and_provenance(self):
        capture=self.live['captures'][0]
        rows=[dict(event='metadata',**capture['metadata'])]
        for observed in self.live['observations']:
            row=copy.deepcopy(observed)
            for key in ('before','after'):
                if key in row:
                    bits=int(row[key]['used'],16);row[key]['used']=[(bits>>i)&1 for i in range(256)]
            rows.append(row)
        rows.append(dict(event='trace-end',installed=True))
        self.assertEqual(verify(rows,self.live,capture),self.live['observations'])
        for change in ('end','allocation','bitmap','count','query','source','failed'):
            bad=copy.deepcopy(rows)
            if change=='end':bad.pop()
            elif change=='allocation':bad.pop(1)
            elif change=='bitmap':bad[1]['after']['used'][17]=1
            elif change=='count':bad[2]['after']['active']=2
            elif change=='query':next(r for r in bad if r['event']=='gate-pool-active-query')['result']=0
            elif change=='source':bad[0]['source_sha256']['map']='0'*64
            else:bad.append(dict(event='trace-failed'))
            with self.subTest(change=change),self.assertRaises(ValueError):verify(bad,self.live,capture)

    def test_committed_jass_is_the_archived_public_producer(self):
        raw=(ROOT/'tools/frida/wc3_waygate_capacity_probe.j').read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),self.live['producer_sha256'])
        self.assertEqual(self.live['captures'][0]['metadata']['source_sha256']['wc3_movement_bypasses_probe.j'],self.live['producer_sha256'])
        self.assertIn(b'udg_PathProbeTick==72',raw)


if __name__=='__main__':unittest.main()
