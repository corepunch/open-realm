"""Original fine caller outcomes use the production route builder at O0/O2."""
import copy
import ctypes
import gzip
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_fine_result_trace import verify,same_cell_motion

class Objects(ctypes.Structure):
    _fields_=[('cells',ctypes.POINTER(ctypes.c_uint8)),('objects',ctypes.POINTER(ctypes.c_uint32))]

class FineResultTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.frozen=json.loads((ROOT/'tools/ghidra/fixtures/retail-fine-public-results-1.27.json').read_text())
        cls.live=json.loads((ROOT/'tools/ghidra/fixtures/retail-fine-public-results-live-1.27.json').read_text())
        cls.cache=json.loads((ROOT/'tools/ghidra/fixtures/retail-cached-fine-route-1.27.json').read_text())
        cls.cache_live=json.loads((ROOT/'tools/ghidra/fixtures/retail-cached-fine-route-1.27-live.json').read_text())
        cls.same_cell=json.loads((ROOT/'tools/ghidra/fixtures/retail-same-cell-motion-1.27.json').read_text())

    def test_full_caller_routes_and_retained_same_cell_latch_at_both_optimizations(self):
        with tempfile.TemporaryDirectory(prefix='wc3-fine-results-')as tmp:
            for opt in ('-O0','-O2'):
                lib=Path(tmp)/(opt+'.so')
                subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',opt,'-shared','-fPIC','-I',str(ROOT),str(ROOT/'tools/ghidra/wc3_pathing_engine_probe.c'),'-o',str(lib)],check=True)
                e=ctypes.CDLL(str(lib));e.pathing_fine_result_words.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(Objects),ctypes.POINTER(ctypes.c_uint32)]
                def request(name,cls,source,goal,budget):
                    blocked={(4,4)}if name=='blocked_start'else {(19,19)}if name=='blocked_goal'else {(12,y)for y in range(24)}if name=='disconnected'else set()
                    cells=(ctypes.c_uint8*576)(*(2 if (x,y)in blocked else 0 for y in range(24)for x in range(24)))
                    objects=(ctypes.c_uint32*7)(12,0,13,24,0x01000001,1,1)if name=='special_target'else None
                    words=[struct.unpack('<I',struct.pack('<f',v))[0]for v in (*source,*goal)]
                    q=(ctypes.c_uint32*16)(24,24,*map(int,source),*map(int,goal),budget,cls,1 if objects else 0x02000000,0,bool(objects),0 if objects else 0xffffffff,*words)
                    out=(ctypes.c_uint32*(7+2*32768))();e.pathing_fine_result_words(q,ctypes.byref(Objects(cells,objects)),out);return list(out[:7+2*out[3]])
                for c in self.frozen['cases']:
                    with self.subTest(opt=opt,name=c['name'],cls=c['cls']):
                        e.pathing_fine_result_reset();out=request(c['name'],c['cls'],c['source'],c['target'],c['budget']);r=c['refill']
                        self.assertEqual(out[1:7],[r['work'],r['nodes'],r['count'],r['index'],r['obstruction'],bool(r['flags']&0x10000000)])
                        self.assertEqual(out[7:],r['words'])
                        self.assertEqual(out[0],int(c['name']in ('same_cell','blocked_start','special_target')))
                for c in self.frozen['same_cell_reuse']:
                    e.pathing_fine_result_reset();request('blocked_goal',c['cls'],[4.25,4.75],[19.25,19.75],700)
                    out=request('blocked_goal',(c['cls']+1)%4,[4.25,4.75],[4.625,4.875],0)
                    self.assertEqual(out[:6],[1,0,0,1,0,1]);self.assertEqual(out[7:],c['words'])
                rows=[dict(event='metadata',**self.live['captures'][0]['metadata']),{'event':'marker','value':'PATHTRACE tick=960 label=complete case=12 '}]+[self.live['catalog'][i]for i in self.live['sequence']]+[{'event':'trace-end','installed':True}]
                self.assertEqual(verify(rows,self.live,self.live['captures'][0],e),292)
                for tag,cap in zip(('b','c'),self.cache_live['captures']):
                    raw=gzip.decompress((ROOT/f'tools/ghidra/fixtures/retail-cached-fine-route-1.27-live-{tag}.jsonl.gz').read_bytes())
                    self.assertEqual(len(raw),cap['bytes'])
                    self.assertEqual(hashlib.sha256(raw).hexdigest(),cap['sha256'])
                    self.assertEqual(verify([json.loads(line)for line in raw.splitlines()],self.cache_live,cap,e),292)
                    self.assertEqual(same_cell_motion([json.loads(line)for line in raw.splitlines()],self.same_cell),self.same_cell)

    def test_single_point_public_motion_words_and_script_match_engine_fixture(self):
        source=(ROOT/'games/warcraft-3/game/tests/retail_same_cell_motion_114.h').read_text()
        table=source.split('same_cell_motion_114[][7]={',1)[1].split('};',1)[0]
        self.assertEqual([int(w,16)for w in re.findall(r'0x([0-9a-f]+)u',table)],
            [w for c in self.same_cell['cases']for row in c['motion']for w in row])
        body=source.split('same_cell_script_114[]=',1)[1]
        script=''.join(json.loads(line.strip())for line in body.splitlines()if line.strip().startswith('"'))
        stock='function ModuloInteger takes integer dividend, integer divisor returns integer\nreturn dividend-(dividend/divisor)*divisor\nendfunction\n'
        original=(ROOT/'tools/frida/wc3_fine_results_probe.j').read_text()
        expected=original.replace('udg_PathProbeCase==12','udg_PathProbeCase==4').replace('endglobals\n','endglobals\n'+stock,1)
        self.assertEqual(script,expected+'function main takes nothing returns nothing\ncall PathProbeInit()\nendfunction\n')

    def test_single_point_lifetime_rejects_extra_refill_missing_commit_and_changed_words(self):
        raw=gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-cached-fine-route-1.27-live-b.jsonl.gz').read_bytes())
        rows=[json.loads(line)for line in raw.splitlines()]
        for event,key in [('route','count'),('route','indices'),('velocity-commit','after'),('arrival-evaluation','source')]:
            bad=copy.deepcopy(rows);r=next(r for r in bad if r['event']==event)
            if key=='count':r[key]+=1
            else:r[key][2 if key=='after' else 0]^=1
            with self.assertRaisesRegex(ValueError,'single-point'):same_cell_motion(bad,self.same_cell)
        i=next(i for i,r in enumerate(rows)if r['event']=='velocity-commit')
        with self.assertRaisesRegex(ValueError,'count differs'):same_cell_motion(rows[:i]+rows[i+1:],self.same_cell)
        i=next(i for i,r in enumerate(rows)if r['event']=='route'and r['kind']=='fine')
        with self.assertRaisesRegex(ValueError,'count differs'):same_cell_motion(rows[:i]+[rows[i]]+rows[i:],self.same_cell)

    def test_complete_cached_advance_matrix_keeps_unsigned_cache_and_gate_precedence(self):
        cases=self.cache['cases']
        self.assertEqual(len(cases),576)
        self.assertEqual(len({(c['cls'],c['count'],c['index'],c['adaptive_enabled'],c['disabled'],c['delay'])for c in cases}),576)
        self.assertEqual({(c['count'],c['index'])for c in cases},
                         {(1,0),(2,0),(2,1),(5,0),(5,3),(5,4),(0,-1),(0,0),(1,-1),(1,1),(2,-1),(2,2)})
        for c in cases:
            with self.subTest(cls=c['cls'],count=c['count'],index=c['index'],delay=c['delay']):
                self.assertEqual(c['after']['fine_work'],1101)
                self.assertEqual(c['after']['count'],c['count'])
                self.assertEqual(c['after']['index'],c['index']&0xffffffff)
                if c['disabled']:
                    self.assertEqual(c['result'],0x100000)
                    self.assertEqual(c['after']['delay'],c['delay'])
                elif c['delay']:
                    self.assertEqual(c['result'],1)
                    self.assertEqual(c['after']['delay'],c['delay']-1)
                elif 0<=c['index']<c['count']:
                    self.assertEqual(c['result'],0)
                    self.assertEqual(c['output'],c['points'][2*c['index']:2*c['index']+2])
                else:self.assertEqual(c['result'],2)
        # The public scene establishes same-cell creation, not the supplied
        # far-source one-point lifetime. Keep these evidence domains distinct.
        self.assertIn('controlled',self.cache['scope'])
        self.assertIn('No far-source',self.cache_live['scope'])

    def test_engine_literals_preserve_original_caller_route_words(self):
        source=(ROOT/'games/warcraft-3/game/tests/retail_fine_results.h').read_text()
        lines=[line for line in source.splitlines()if line.startswith(' {')]
        self.assertEqual(len(lines),24)
        for line,c in zip(lines,self.frozen['cases']):
            self.assertEqual([int(x,16)for x in re.findall(r'0x([0-9a-f]+)',line)][1:],c['refill']['words'])

    def test_live_contract_rejects_changed_history_source_footer_and_provenance(self):
        cap=self.live['captures'][0]
        rows=[dict(event='metadata',**cap['metadata']),{'event':'marker','value':'PATHTRACE tick=960 label=complete case=12 '},*[self.live['catalog'][i]for i in self.live['sequence']],{'event':'trace-end','installed':True}]
        self.assertEqual(verify(rows,self.live,cap),292)
        bad=copy.deepcopy(rows);next(r for r in bad if r.get('event')=='fine-result')['source'][0]^=1
        with self.assertRaisesRegex(ValueError,'words/history'):verify(bad,self.live,cap)
        bad=copy.deepcopy(rows);bad[0]['source_sha256']['wc3_pathfinding.js']='0'*64
        with self.assertRaisesRegex(ValueError,'provenance'):verify(bad,self.live,cap)
        with self.assertRaisesRegex(ValueError,'incomplete'):verify(rows[:-1],self.live,cap)
        with self.assertRaisesRegex(ValueError,'complete'):verify([r for r in rows if r.get('event')!='marker'],self.live,cap)

if __name__=='__main__':unittest.main()
