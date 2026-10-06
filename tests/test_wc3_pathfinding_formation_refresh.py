"""Fixed-tick mutation captures, original regroup matrix and saved Ghidra map."""
import copy
import ctypes
import gzip
import hashlib
import json
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_formation_refresh_trace import verify,SOURCE_HASHES

class FormationRefreshTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=ROOT/'tools/ghidra/fixtures'
        cls.cert=json.loads((cls.directory/'retail-formation-refresh-1.27.json').read_text())
        cls.rows=[]
        for c in cls.cert['captures']:
            raw=gzip.decompress((cls.directory/c['file']).read_bytes())
            if hashlib.sha256(raw).hexdigest()!=c['raw_sha256']:raise AssertionError('capture changed')
            cls.rows.append([json.loads(l)for l in raw.splitlines()])
        cls.temp=tempfile.TemporaryDirectory(prefix='wc3-formation-refresh-')
        library=Path(cls.temp.name)/'probe.so'
        subprocess.run(['cc','-O2','-shared','-fPIC','-I',str(ROOT),str(ROOT/'tools/ghidra/wc3_pathing_engine_probe.c'),'-o',str(library),'-lm'],check=True)
        cls.engine=ctypes.CDLL(str(library))
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def test_two_completed_fixed_tick_mutation_captures_and_exact_layouts(self):
        for rows,c in zip(self.rows,self.cert['captures']):
            result=verify(rows,self.engine)
            for name in ('counts','membership','routes','layout_words','fixtures','reset_counter','advance_age','advance_clock','fixture_sha256'):
                self.assertEqual(result[name],c[name],name)
            self.assertEqual(result['layout_words'],34)
        self.assertEqual(verify(self.rows[0])['fixtures'],verify(self.rows[1])['fixtures'])

    def test_original_regroup_matrix_and_exact_engine_fixture(self):
        raw=(self.directory/'retail-formation-refresh-1.27-oracle.json').read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),self.cert['oracle_sha256'])
        oracle=json.loads(raw);self.assertTrue(oracle['passed']);self.assertEqual(oracle['binary_sha256'],self.cert['binary_sha256'])
        self.assertEqual(len(oracle['member_cases']),32);self.assertEqual(len(oracle['regroup_cases']),648)
        self.assertEqual(oracle['refresh_cases'],48);self.assertEqual(oracle['regroup_status_cases'],1792)
        # Identical native inputs must replace either prior arrival bit.
        cases=oracle['member_cases']
        for a,b in zip(cases[::2],cases[1::2]):
            self.assertEqual(a['prior'],0);self.assertEqual(b['prior'],0x10000)
            self.assertEqual(a['flags'],b['flags']);self.assertEqual(a['speed'],b['speed']);self.assertEqual(a['heading'],b['heading'])
        expected=[]
        for r in oracle['regroup_cases']:
            expected.extend(r['input']+r['output']+r['member_flags'])
            for p in r['paths']:expected.extend(p)
        header=(ROOT/'games/warcraft-3/game/tests/fixtures/retail_formation_regroup_112.h').read_text()
        self.assertEqual([int(x)for x in re.findall(r'\b(\d+)u\b',header)],expected)

    def test_engine_script_retains_the_captured_public_producer(self):
        raw=gzip.decompress((self.directory/'sources'/(SOURCE_HASHES['wc3_formation_refresh_probe.j']+'.gz')).read_bytes()).decode()
        header=(ROOT/'games/warcraft-3/game/tests/fixtures/retail_formation_refresh_script_112.h').read_text()
        script=''.join(json.loads(line)for line in header.splitlines()if line.startswith('"'))
        self.assertIn(raw,script)
        self.assertIn('call PathProbeInit()',script)
        self.assertIn('function ModuloInteger',script)

    def test_sources_and_saved_ghidra_contracts(self):
        for name,digest in {**SOURCE_HASHES,'oracle':self.cert['oracle_source_sha256']}.items():
            if name=='map':continue
            self.assertEqual(hashlib.sha256(gzip.decompress((self.directory/'sources'/(digest+'.gz')).read_bytes())).hexdigest(),digest)
        raw=(self.directory/'retail-formation-refresh-1.27-static.json').read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),self.cert['static_sha256'])
        static=json.loads(raw);self.assertFalse(static['unsaved']);self.assertEqual(static['sha256'],self.cert['binary_sha256'])
        functions={x['address']:x for x in static['functions']}
        self.assertEqual(functions['6f16d660']['name'],'PathGroup_InitializeMemberOffsets')
        self.assertEqual(functions['6f16d8e0']['name'],'PathGroup_SetHeadingFromPointDelta')
        self.assertTrue(functions['6f16d8e0']['xrefs'])
        self.assertTrue(any('0xfffeffff' in x for x in functions['6f16a790']['instructions']))

    def test_rejects_incomplete_or_changed_producer_contract(self):
        for change in ('footer','map','sample','rank','reset','clock','cached','layout','truncation','counter','destination','membership'):
            rows=copy.deepcopy(self.rows[0])
            if change=='footer':rows.pop()
            elif change=='map':rows[0]['source_sha256']['map']='0'*64
            elif change=='sample':rows=[r for r in rows if r['event']!='marker' or 'tick=20 label=sample 'not in r['value']]
            elif change=='rank':next(r for r in rows if r['event']=='formation-rank-set')['input']=15
            elif change in ('reset','destination'):
                r=next(r for r in rows if r['event']=='formation-reset')
                if change=='reset':r['after']['members'][0]['row'][10]=1
                else:r['after']['members'][0]['row'][6]^=1
            elif change in ('clock','counter'):
                r=next(r for r in rows if r['event']=='formation-refresh')
                if change=='clock':r['before']['clock'][0]^=0x100000
                else:r['before']['counter']+=1
            elif change=='cached':
                r=next(r for r in rows if r['event']=='formation-group-route' and r['before']['coarseCount'])
                r['after']['coarseIndex']^=1
            elif change=='layout':next(r for r in rows if r['event']=='formation-rank-layout')['after']['members'][0]['row'][3]^=1
            elif change=='truncation':rows[-1]['counts']['formation-refresh']+=1
            else:next(r for r in rows if r['event']=='formation-regroup')['before']['members'].pop()
            with self.subTest(change=change),self.assertRaises(ValueError):verify(rows,self.engine)

if __name__=='__main__':unittest.main()
