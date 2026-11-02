#!/usr/bin/env python3
"""Verify actual public power native lifetime, guarded VM dispatch and bounded engine workers."""
import argparse
import base64
from collections import Counter
import ctypes as C
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
from verify_wc3_pathing_target_normalize218 import SHA, original_bytes
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-power350-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
PATTERN=re.compile(r'P350 tick=(\d+) beat=(\d+) label=(\w+)$')
INPUTS=[(0x40000000,0x4f000000),(0x40000000,0xce6e6b28),(0x3f000000,0xce6e6b28),(0x40000000,0x4e800000)]
LOOPS=[(0x40000000,0x80000000),(0x3fa45af2,0xa5425800),(0x3fa45af2,0xa5425800)]
def digest(data):return hashlib.sha256(data).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(P350 [^"\r\n]*)" \)',text)
def records(trace):
 result=[]
 for row in trace:
  if row['event']!='marker':continue
  m=PATTERN.fullmatch(row['value'])
  if not m:raise ValueError('malformed public marker')
  tick,beat,label=m.groups();result.append(dict(counter=row['c'],tick=int(tick),beat=int(beat),label=label))
 return result

def validate(spec,files):
 if spec['task']!='NUM-01.14' or spec['game_sha256']!=SHA or len(spec['instructions'])!=1574 or len(spec['functions'])!=8:
  raise ValueError('wrong VM/math inventory')
 if set(files)!=set(spec['files']) or any(digest(v.encode())!=spec['files'][k]for k,v in files.items()):raise ValueError('archive differs')
 hashes={digest(v.encode())for v in files.values()};expected={};semantic={};samples=0
 for case in range(4):
  if digest(base64.b64decode(files[f'maps/{case}.w3m.b64']))!=spec['maps'][str(case)]:raise ValueError('map differs')
  for mode,suffix in [('observe','a'),('observe','b'),('control','a')]:
   name=f'{mode}-{case}-{suffix}';rows=[json.loads(s)for s in files[name+'.jsonl'].splitlines()];meta=rows[0]
   if (meta['event']!='metadata'or meta.get('task')!='NUM-01.14/power350'or not meta.get('owned')or meta['mode']!=mode or
       meta['sha256']!=SHA or (meta['env'],meta['display'],meta['remote'])!=('B',':98','127.0.0.1:27049')or
       meta['hold_seconds']!=12 or meta['source_sha256']['map']!=spec['maps'][str(case)]or
       any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):
    raise ValueError('unowned/failed capture')
   for source,value in meta['source_sha256'].items():
    if source.endswith(('.py','.j','.js','.c'))and value not in hashes:raise ValueError('captured source absent')
   before=[r for r in rows if r['event']=='before-file']
   if len(before)!=1 or digest(files[name+'-before.txt'].encode())!=before[0]['sha256']:raise ValueError('before boundary missing')
   if case<3:
    bound=[r for r in rows if r['event']==('bounded-power-deadline'if mode=='observe'else'bounded-control-deadline')]
    if len(bound)!=1 or bound[0]['elapsed']-before[0]['elapsed']<12:raise ValueError('insufficient native lifetime')
    if mode=='control'and bound[0]['hold']<12:raise ValueError('short observer-free lifetime')
    if rows[-1]!=dict(event='preload-file',missing=True):raise ValueError('unexpected after/completion file')
   else:
    end=rows[-1]
    if (end['event']!='preload-file'or not end.get('complete')or digest(files[name+'-preload.txt'].encode())!=end['sha256']):
     raise ValueError('terminating control incomplete')
   trace=[r for r in rows if 'seq'in r and r['event']!='trace-end']
   if mode=='control':
    if trace or any(r['event']=='trace-end'for r in rows):raise ValueError('instrumented control')
    continue
   ends=[r for r in rows if r['event']=='trace-end']
   if (len(ends)!=1 or not ends[0].get('installed')or not ends[0].get('readOnly')or
       ends[0]['counts']!=dict(Counter(r['event']for r in trace))or [r['seq']for r in trace]!=list(range(len(trace)))):
    raise ValueError('incomplete native observation')
   public=records(trace);want=15 if case<3 else 23
   if len(public)!=want or public[14]!=dict(counter=1090,tick=20,beat=13,label='before'):
    raise ValueError('scheduler admission differs')
   if markers(files[name+'-before.txt'])!=[r['value']for r in trace if r['event']=='marker'][1:15]:raise ValueError('preload/public prefix differs')
   entry=[r for r in trace if r['event']=='power-enter'];integer=[r for r in trace if r['event']=='integer-power-enter']
   reductions=[(r['name'],r['input'])for r in trace if r['event']=='reduction-enter']
   if (len(entry)!=1 or len(integer)!=1 or (entry[0]['base'],entry[0]['power'])!=INPUTS[case] or entry[0]['c']!=1090 or
       entry[0]['stack'][:3]!=['0x919123','0x20f990','0x91ba8a']):raise ValueError('public dispatch or operands differ')
   exponent=LOOPS[case][1] if case<3 else 0x40000000
   if integer[0]['exponent']&0xffffffff!=exponent or integer[0]['c']!=1090:raise ValueError('integer loop conversion differs')
   wanted=[]if case in (0,3)else [('exp',0xce254258 if case==1 else 0x4e254258),('exp-quarter',0x4e254258)]
   if reductions!=wanted:raise ValueError('reduction order/sign differs')
   visits=[r for r in trace if r['event']=='lifetime-sample']
   if case<3:
    if any(r['event']=='power-leave'for r in trace)or not 40<=len(visits)<=60:raise ValueError('native returned or insufficient samples')
    def in_loop(pc):
     return pc is not None and any(int(entry,16)<=0x6f000000+int(pc,16)<=int(spec['functions'][entry]['end'],16)for entry in ('6f071180','6f06f9c0'))
    if any(not in_loop(r['pc'])or r.get('state')!='running'  or r['c']!=1090 or r['tid']!=entry[0]['tid']or not r['pc']or format(0x6f000000+int(r['pc'],16),'08x')not in spec['instructions']for r in visits):
     raise ValueError('native counter advanced or thread left guarded code')
    samples+=len(visits)
   else:
    leave=[r for r in trace if r['event']=='power-leave']
    if len(leave)!=1 or leave[0]['result']!=0 or public[-1]['label']!='complete' or visits:raise ValueError('finite native control differs')
    if markers(files[name+'-preload.txt'])!=[r['value']for r in trace if r['event']=='marker'][15:]:raise ValueError('control suffix differs')
   stable=dict(public=public,inputs=INPUTS[case],exponent=exponent,reductions=reductions)
   if digest(json.dumps(stable,sort_keys=True,separators=(',',':')).encode())!=spec['semantic_sha256'][str(case)]:raise ValueError('frozen public/native sequence differs')
   if suffix=='a':expected[case]=public;semantic[case]=stable
   elif stable!=semantic[case]:raise ValueError('retail repeat differs')
  control=f'control-{case}-a'
  native_prefix=[f"P350 tick={r['tick']} beat={r['beat']} label={r['label']}"for r in expected[case]]
  if markers(files[control+'-before.txt'])!=native_prefix[1:15]:raise ValueError('observer-free prefix differs')
  if case==3 and markers(files[control+'-preload.txt'])!=native_prefix[15:]:raise ValueError('observer-free finite suffix differs')
 return dict(captures=8,controls=4,samples=samples,records=expected)

def main():
 from wc3_power_lifetime import run_worker
 from wc3_pathfinding_engine_tests import run_engine
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
 p.add_argument('--engine-library',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text())
 if digest(BUNDLE.read_bytes())!=spec['bundle_sha256']:raise ValueError('bundle differs')
 files=json.loads(gzip.decompress(BUNDLE.read_bytes()));result=validate(spec,files);original_bytes(a.binary,spec['instructions'])
 # Rebuild all authored inputs from the embedded base, preserving their exact hashes.
 import tempfile
 with tempfile.TemporaryDirectory(prefix='power350-maps-')as tmp:
  tmp=Path(tmp);base=tmp/'base.w3m';base.write_bytes(base64.b64decode(files['maps/base.w3m.b64']))
  for case in range(4):
   out=tmp/f'{case}.w3m'
   subprocess.run([__import__('sys').executable,str(ROOT/'tools/frida/research/power350_make_map.py'),'--base',str(base),'--case',str(case),'--output',str(out)],check=True,stdout=subprocess.DEVNULL)
   if digest(out.read_bytes())!=spec['maps'][str(case)]:raise ValueError('map producer differs')
 engine=C.CDLL(str(a.engine_library.resolve()))
 for case,(base,exponent)in enumerate(INPUTS):
  words=(C.c_uint32*4)();engine.pathing_power_loop(C.c_uint32(base),C.c_uint32(exponent),words)
  expected=[0,0xabcdef02,*LOOPS[case]]if case<3 else [1,0,0xabcdef03,0xabcdef04]
  if list(words)!=expected:raise ValueError('checked engine loop operands/result differ')
 from concurrent.futures import ThreadPoolExecutor
 def check(pair):
  edition,case=pair;row=run_worker(a.test_binary,a.data,case,edition,a.report.with_name(a.report.stem+f'-{edition}-{case}.log'))
  if row['records']!=result['records'][case]:raise ValueError('engine public scheduler sequence differs')
  return row
 with ThreadPoolExecutor(max_workers=4)as pool:workers=list(pool.map(check,[(e,k)for e in ('classic','tft')for k in range(4)]))
 ordinary=run_engine(a.test_binary,a.data,a.report,['wc3_api.pathfinding_public_power*','wc3_api.pathfinding_power_large*','wc3_spell.power349*','wc3_movement.power349*'],timeout=300)
 result.pop('records');result.update(passed=True,status='verified',binary_sha256=SHA,workers=workers,engine=ordinary,checked_domains=len(INPUTS),instructions=len(spec['instructions']))
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
