"""Public moving-cohort boundary, provenance, and fresh production C layouts."""
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
from verify_wc3_formation_boundary_trace import verify, SOURCE_HASHES

class FormationBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=ROOT/'tools/ghidra/fixtures'
        cls.cert=json.loads((cls.directory/'retail-formation-boundary-1.27.json').read_text())
        cls.rows=[]
        for capture in cls.cert['captures']:
            raw=gzip.decompress((cls.directory/capture['file']).read_bytes())
            if hashlib.sha256(raw).hexdigest()!=capture['raw_sha256']:raise AssertionError('capture changed')
            cls.rows.append([json.loads(l)for l in raw.splitlines()])
        cls.temp=tempfile.TemporaryDirectory(prefix='wc3-formation-boundary-')
        library=Path(cls.temp.name)/'probe.so'
        subprocess.run(['cc','-O2','-shared','-fPIC','-I',str(ROOT),str(ROOT/'tools/ghidra/wc3_pathing_engine_probe.c'),'-o',str(library),'-lm'],check=True)
        cls.engine=ctypes.CDLL(str(library))

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def test_completed_moving_public_boundaries_and_native_layout_words(self):
        for rows,certificate in zip(self.rows,self.cert['captures']):
            result=verify(rows,self.engine)
            for name in ('sizes','admitted','moving','layout_words','fixtures','counts','fixture_sha256'):
                self.assertEqual(result[name],certificate[name],name)
            self.assertEqual(result['admitted'],[11,12,12,12])
            self.assertEqual(result['moving'],[11,4,3,3])
            self.assertEqual(result['layout_words'],94)
        self.assertEqual(verify(self.rows[0])['fixtures'],verify(self.rows[1])['fixtures'])

    def test_source_and_saved_ghidra_evidence(self):
        for name,digest in SOURCE_HASHES.items():
            if name=='map':continue
            raw=gzip.decompress((self.directory/'sources'/(digest+'.gz')).read_bytes())
            self.assertEqual(hashlib.sha256(raw).hexdigest(),digest)
        raw=(self.directory/'retail-formation-boundary-1.27-static.json').read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),self.cert['static_sha256'])
        static=json.loads(raw);self.assertFalse(static['unsaved'])
        functions={f['address']:f for f in static['functions']}
        self.assertEqual(functions['6f16c060']['name'],'PathGroup_AppendMemberRows')
        self.assertEqual(functions['6f16da80']['name'],'PathGroup_FillMemberRows')
        assembly=functions['6f23acd0']['instructions']
        self.assertIn('6f23adc1 PUSH 0xc',assembly)
        self.assertIn('6f23aded PUSH 0xc',assembly)
        self.assertIn('6f23adb4 MOV dword ptr [EBP + -0x24],EAX',assembly)
        self.assertIn('6f23adc3 MOV dword ptr [EBP + -0x20],ECX',assembly)

    def test_engine_fixture_is_the_certified_native_input_boundary(self):
        header=(ROOT/'games/warcraft-3/game/tests/retail_formation_boundary_111.h').read_text()
        expected=[]
        for fixture in self.cert['captures'][0]['fixtures']:
            expected.extend([*fixture['point'],fixture['heading']])
            for row in fixture['rows']:expected.extend(row)
        self.assertEqual([int(v,16)for v in re.findall(r'0x[0-9a-f]{8}',header)],expected)
        self.assertEqual([int(v)for v in re.findall(r'^    \{(\d+),',header,re.M)],[11,12,13,25])

    def test_rejects_missing_boundary_or_changed_membership(self):
        for change in ('footer','map','sample','rank','member','point','layout','pose','truncation','moving','member_end'):
            rows=copy.deepcopy(self.rows[0])
            if change=='footer':rows.pop()
            elif change=='map':rows[0]['source_sha256']['map']='0'*64
            elif change=='sample':rows=[r for r in rows if r['event']!='marker' or 'tick=30 label=sample 'not in r['value']]
            elif change=='member_end':rows.pop(next(i for i,r in enumerate(rows)if r['event']=='group-point-member-end'))
            elif change=='rank':next(r for r in rows if r['event']=='formation-rank-set')['input']=15
            elif change in ('member','point'):
                r=next(r for r in rows if r['event']=='group-point-member-begin')
                if change=='member':r['unit']='0x123'
                else:r['point'][0]^=1
            elif change in ('layout','pose'):
                r=next(r for r in rows if r['event']=='formation-rank-layout' and len(r['before']['members'])>1)
                if change=='layout':r['after']['members'][0]['row'][3]^=1
                else:r['before']['members'][0]['pose'][4]=1
            elif change=='truncation':rows[-1]['counts']['formation-rank-layout']+=1
            else:
                for r in rows:
                    if r['event']=='velocity-commit':r['after'][4:6]=[0,0]
            with self.subTest(change=change),self.assertRaises(ValueError):verify(rows,self.engine)

if __name__=='__main__':unittest.main()
