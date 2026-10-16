#!/usr/bin/env python3
"""Verify exceptional public speed motion, finite restoration and literal actor commits."""
import argparse,gzip,hashlib,json,re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-speed314-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
TESTS=['wc3_movement.speed314*','wc3_movement.public_speed*','wc3_movement.profile271*','wc3_hero_movement.move225*','wc3_movement.nonfinite313*','wc3_movement.group*','wc3_movement.persistent_group*','wc3_movement.temporary_modifiers*','wc3_save.*']
SOURCES=['tools/frida/research/speed314_'+x for x in('capture.py','observer.js','make_map.py','probe.j')]+['tools/frida/research/smart277_ui_input.c','tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c','tools/ghidra/research/Speed314Evidence.java','games/warcraft-3/game/tests/fixtures/retail_speed314.h']
MODES=['foot','fly','amph','hover','float']
WORDS=[0x7fc00001,0x7fa00001,0xffc00001,0x7f800000,0xff800000,0x7f7fffff,0xff7fffff,0]
ACTION={'created':0,'set-exceptional':1,'move':2,'restore':3,'stop':4,'sample':5}
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(s):return re.findall(r'call Preload\( "(S314 [^"\r\n]*)" \)',s)
def extract(rows,scene):
 rr=[x for x in rows if x.get('scene')==scene and 'seq'in x];actor=None;out=[];block=[];last=None
 for x in rr:
  block.append(x)
  if x['event']=='marker':
   label=re.search(r'label=(\S+)',x['value'])[1]
   if label=='created':
    actor=next(z['m']['id']for z in reversed(block)if z['event']=='physical-radius')
   if label in ACTION:
    get=[z['value']for z in block if z['event']=='public-coordinate'];sp=[z['value']for z in block if z['event']=='public-speed'];df=[z['value']for z in block if z['event']=='public-default-speed']

    if len(get)!=2 or len(sp)!=1 or len(df)!=1:raise ValueError('missing public getters')
    out.append([ACTION[label],x['c'],*x['clock'][:2],*get,*sp,*df,int(re.search(r'order=(\d+)',x['value'])[1]),0,0,0,0,0,0])
   block=[]
  elif x['event']=='commit-leave'and actor:
   members=[m for m in x['g']['members']if m['id']==actor]
   if not members:continue

   if len(members)!=1:raise ValueError('ambiguous actor')
   m=members[0]['m'];out.append([6,x['c'],*m['clock'],*m['pos'],*m['vel'],m['heading'],m['maximum'],m['range'],members[0]['requested'],members[0]['turn'],members[0]['flags'],x['g']['flags']])
 return out

def render_header(cases):
 s='/* Literal public actions and physical commits from read-only retail speed314 captures. */\n#pragma once\n';groups=[]
 for mi,mode in enumerate(MODES):
  for sc in range(8):
   rows=cases[mode]['states'][sc];name=f'speed314_{mode}_{sc}';s+=f'static uint32_t const {name}[][15]={{\n';s+=''.join(' {'+','.join('0x%08xu'%w for w in row)+'},\n'for row in rows)+'};\n';groups.append((mi,sc,name,len(rows)))
 s+='static struct {unsigned mode,scene;uint32_t const (*rows)[15];unsigned count;}const speed314_cases[]={\n'+''.join(f' {{{m},{sc},{name},{n}}},\n'for m,sc,name,n in groups)+'};\n'
 return s

def validate(spec,bundle,check_sources=True):
 if spec['version']!=1 or spec['task']!='NUM-03.2'or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS:raise ValueError('wrong contract')
 if len(spec['instructions'])!=spec['instruction_count']or spec['instruction_count']!=523:raise ValueError('missing instruction guards')
 if set(spec['cases'])!=set(MODES)or set(bundle)!=set(MODES):raise ValueError('missing movement family')
 if check_sources:
  if digest(BUNDLE)!=spec['bundle_sha256']or set(spec['pins'])!=set(SOURCES):raise ValueError('frozen bundle differs')
  for p in SOURCES:
   if digest(ROOT/p)!=spec['pins'][p]:raise ValueError('source differs '+p)
 native=public=commits=states=0
 for mode in MODES:
  case=spec['cases'][mode];caps=bundle[mode]
  if len(caps)!=3:raise ValueError('missing observed repeat/control')
  streams=[]
  for index,cap in enumerate(caps):
   method='observe'if index<2 else'control';rows=cap['rows'];m=rows[0];f=rows[-1]
   mm=markers(cap['preload']);public+=len(mm)
   if m['task']!='NUM-03.2/speed314'or m['mode']!=method or m['sha256']!=SHA or not m['owned']or m['env']not in('B','C')or m['display']!={'B':':98','C':':99'}[m['env']]or m['remote']!={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[m['env']]or m['source_sha256']['map']!=case['map_sha256']or f['event']!='preload-file'or f.get('missing')or mm!=case['public']or f['markers']!=257 or len(mm)!=257 or not f['complete']or hashlib.sha256(cap['preload'].encode()).hexdigest()!=f['sha256']or any(x.get('type')=='error'or x.get('event')in('trace-failed','native-exception')for x in rows):raise ValueError('capture provenance/public control differs')
   for p in SOURCES[:7]:
    if m['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('captured source differs '+p)
   live=[x for x in rows if'seq'in x and x['event']!='trace-end'];ends=[x for x in rows if x['event']=='trace-end']
   if method=='control':
    if live or ends:raise ValueError('hooked control')
    continue
   if [x['seq']for x in live]!=list(range(len(live)))or len(ends)!=1 or not ends[0]['installed']or not ends[0]['readOnly']or ends[0]['seq']!=len(live)or dict(Counter(x['event']for x in live))!=ends[0]['counts']:raise ValueError('lost native events')
   if[x['value']for x in live if x['event']=='marker'and'label=start'not in x['value']]!=mm:raise ValueError('live/public outputs differ')
   if[x['value']for x in live if x['event']=='public-default-height']!=WORDS:raise ValueError('changed public producer')
   actual=[extract(rows,sc)for sc in range(8)]
   if actual!=case['states']or any(sum(r[0]!=6 for r in stage)!=30 for stage in actual)or[sum(r[0]==6 for r in stage)for stage in actual]!=[77,76,77,51,76,51,77,76]:raise ValueError('actor timeline differs')
   for sc in range(8):
    setters=[x['value']for x in live if x['event']=='setter-enter'and x['scene']==sc]
    if setters!=[WORDS[sc],0x43870000]:raise ValueError('missing setter/restoration')
   native+=len(live);states+=sum(map(len,actual));commits+=sum(r[0]==6 for stage in actual for r in stage);streams.append(actual)
  if streams[0]!=streams[1]:raise ValueError('actor repeat differs')
 if check_sources and(ROOT/SOURCES[-1]).read_text()!=render_header(spec['cases']):raise ValueError('engine golden differs from retail')
 return dict(captures=15,native_events=native,public_markers=public,states=states//2,commits=commits//2)
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');ap.add_argument('--data',type=Path,default=ROOT/'build/tests');ap.add_argument('--report',type=Path,required=True);a=ap.parse_args()
 if a.report.exists():ap.error('new report required')
 spec=json.loads(SPEC.read_text());bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));evidence=validate(spec,bundle);original_bytes(a.binary,spec['instructions'])
 import verify_wc3_pathing_work242 as engine
 engine.TESTS=TESTS
 result=dict(passed=True,status='verified',task=spec['task'],instructions=len(spec['instructions']),**evidence,engine=engine.run_engine(a.test_binary,a.data,a.report),binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
