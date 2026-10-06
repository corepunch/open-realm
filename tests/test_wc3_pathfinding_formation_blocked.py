"""Retail blocked-slot ordering, cached route and strict evidence controls."""
import copy
import ctypes
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_formation_blocked_trace import verify,header,SOURCE_HASHES
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion

class FormationBlockedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=ROOT/'tools/ghidra/fixtures'
        cls.cert=json.loads((cls.fixture/'retail-formation-blocked-1.27.json').read_text())
        cls.rows=[]
        for record in cls.cert['records']:
            raw=gzip.decompress((cls.fixture/record['file']).read_bytes())
            if hashlib.sha256(raw).hexdigest()!=record['trace_sha256']:raise ValueError('blocked checksum differs')
            cls.rows.append([json.loads(l)for l in raw.splitlines()])

    def test_complete_producers_and_fresh_cached_ticks(self):
        results=[verify(rows)for rows in self.rows]
        for r,c in zip(results,self.cert['records']):self.assertEqual(r,c['verification'])
        self.assertEqual(results[0]['signature'],results[1]['signature'])
        for t in range(2):
            self.assertEqual([w[:7]+w[8:]for w in results[0]['replay'][t]['words']],
                             [w[:7]+w[8:]for w in results[1]['replay'][t]['words']])
        self.assertEqual([(r['owner_ticks'],r['commits'])for r in results],[(195,960)]*2)

    def test_engine_header_is_generated_from_complete_native_stages(self):
        self.assertEqual(header([verify(rows)for rows in self.rows]),
            (ROOT/'games/warcraft-3/game/tests/fixtures/retail_formation_blocked_110.h').read_text())

    def test_sources_and_saved_ghidra(self):
        for name,digest in SOURCE_HASHES.items():
            if name in ('map','wc3_ui_input.exe','wc3_ui_input.exe.so'):continue
            raw=gzip.decompress((self.fixture/'sources'/(digest+'.gz')).read_bytes())
            self.assertEqual(hashlib.sha256(raw).hexdigest(),digest,name)
        raw=(self.fixture/'retail-formation-blocked-ghidra-1.27.json').read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),self.cert['ghidra_sha256'])
        evidence=json.loads(raw);self.assertFalse(evidence['unsaved'])
        self.assertEqual(evidence['native_sha256'],self.cert['native_sha256'])
        functions={r['address']:r for r in evidence['functions']}
        for address in ('6f16a790','6f16e250','6f16ce10','6f169b00','6f16fbd0'):
            self.assertIn('Payoff110',functions[address]['comment'])
        fields={(r['type'],r['name']):r['offset']for r in evidence['fields']}
        self.assertEqual(fields['WC3PathGroupPrefix','path'],0x3c)
        self.assertEqual(fields['WC3PathPrefix','fine_count'],0x50)
        self.assertEqual(fields['WC3PathPrefix','adaptive_count'],0x70)

    def test_all_captured_scalar_and_commit_arithmetic(self):
        with tempfile.TemporaryDirectory(prefix='wc3-formation110-')as temp:
            library=Path(temp)/'engine.so'
            subprocess.run(['cc','-O2','-shared','-fPIC','-I',str(ROOT),str(ROOT/'tools/ghidra/wc3_pathing_engine_probe.c'),'-o',str(library),'-lm'],check=True)
            engine=ctypes.CDLL(str(library));configure(engine)
            for rows in self.rows:
                result=verify_motion(rows,engine,None)
                self.assertEqual(result['exact_decisions'],954)
                self.assertEqual(result['exact_velocity_commits'],960)

    def test_rejects_lost_or_substituted_boundaries(self):
        for mutation in ('footer','source','cached','adjust_order','fallback','decision','phase','cap','delta','late_commit'):
            rows=copy.deepcopy(self.rows[0])
            if mutation=='footer':rows.pop()
            elif mutation=='source':rows[0]['source_sha256']['wc3_formation_blocked_probe.j']='0'*64
            elif mutation=='cached':next(r for r in rows if r['event']=='formation-group-route' and r['before']['coarseCount'])['after']['coarseCount']=0
            elif mutation=='adjust_order':next(r for r in rows if r['event']=='formation-member-destination' and r['member'][10])['member'][10]=0
            elif mutation=='fallback':next(r for r in rows if r['event']=='formation-member-destination')['after'][6]^=1
            elif mutation=='decision':rows.remove(next(r for r in rows if r['event']=='motion-decision'))
            elif mutation=='phase':next(r for r in rows if r['event']=='pair-group-phase-begin')['phase']='commit'
            elif mutation=='cap':next(r for r in rows if r['event']=='velocity-commit')['speed']^=1
            elif mutation=='delta':next(r for r in rows if r['event']=='velocity-commit')['requested'][1]^=1
            else:[r for r in rows if r['event']=='velocity-commit'][-1]['after'][2]^=1
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):verify(rows)

if __name__=='__main__':unittest.main()
