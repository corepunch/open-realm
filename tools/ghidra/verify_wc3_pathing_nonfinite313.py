#!/usr/bin/env python3
"""Check original nonfinite public producers, including explicitly nonreturning visibility consumers."""
import argparse,gzip,hashlib,json,re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-nonfinite313-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
TESTS=['wc3_movement.nonfinite313*','wc3_movement.numeric312*','wc3_movement.place309*','wc3_movement.bridge311*','wc3_movement.goal310*','wc3_spawn.*','wc3_fine_spatial.*','wc3_movement.public_axis_position_retains_original_write_and_next_step_words']
SOURCES=['tools/frida/research/nonfinite313_'+x for x in('capture.py','observer.js','make_map.py','probe.j')]+['tools/frida/research/smart277_ui_input.c','tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c','tools/ghidra/research/Nonfinite313Evidence.java','games/warcraft-3/game/tests/fixtures/retail_nonfinite313.h']
LABELS={'radius':['created','axis-x','restore-x','axis-y','restore-y','placed','queryzero'],'all':['created','axis-x','restore-x','axis-y','restore-y','placed','queryzero'],'create':['created','created-source']}
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(s):return re.findall(r'call Preload\( "(N313 [^"\r\n]*)" \)',s)
def normalize(v):
 if isinstance(v,list):return[normalize(x)for x in v]
 if isinstance(v,dict):return{k:normalize(x)for k,x in v.items()}
 if isinstance(v,str)and re.fullmatch('0x[0-9a-f]+',v):return v!='0x0'
 return v

def states(rows,op,scene):
 rr=[x for x in rows if x.get('scene')==scene and 'seq'in x];last=None;raw=None;block=[];out=[]
 for x in rr:
  if x['event']=='authored-radius':raw=x['radius']
  if x['event']=='delta-leave':last=x['m']
  block.append(x)
  if x['event']=='marker':
   label=re.search(r'label=([^ ]+)',x['value'])[1]
   if label in LABELS[op]:
    getters=[r for r in block if r['event']=='public-coordinate']
    if last is None or raw is None or len(getters)!=2 or[x['axis']for x in getters]!=[0,1]:raise ValueError('missing observed state words')
    out.append(dict(label=label,fine=last['pos'],world=[r['value']for r in getters],physical=last['radius'],authored=raw,returned=True))
   block=[]
 fault=[x for x in rows if x['event']=='native-exception']
 if fault:
  if last is None or raw is None or len(out)>=len(LABELS[op]):raise ValueError('invalid failure boundary')
  out.append(dict(label=LABELS[op][len(out)],fine=last['pos'],world=[0,0],physical=last['radius'],authored=raw,returned=False))
 return out

def render_header(cases):
 s='/* Exact original public coordinate words; terminal rows stop before retail visibility faults. */\n#pragma once\n'
 for op,title in [('radius','radius'),('all','axes'),('create','create')]:
  rows=cases['radius']['states'] if op=='radius' else [cases[op+'-'+str(scene)]['states']for scene in range(8,16)]
  s+='static uint32_t const nonfinite313_'+title+'_counts[8]={'+','.join(str(len(x))for x in rows)+'};\n'
  s+='static uint32_t const nonfinite313_'+title+'[8]['+str(2 if op=='create'else 7)+'][7]={\n'
  for stage in rows:s+=' {'+','.join('{'+','.join('0x%08xu'%word for word in r['fine']+r['world']+[r['physical'],r['authored'],int(r['returned'])])+'}'for r in stage)+'},\n'
  s+='};\n'
 return s

def validate(spec,bundle,check_sources=True):
 if spec['version']!=1 or spec['task']!='NUM-03.2'or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS:raise ValueError('wrong contract')
 if len(spec['instructions'])!=spec['instruction_count']or spec['instruction_count']!=482:raise ValueError('missing instruction guards')
 keys={'radius'}|{op+'-'+str(scene)for op in('all','create')for scene in range(8,16)}
 if set(spec['cases'])!=keys or set(bundle)!=keys:raise ValueError('missing producer families')
 if check_sources:
  if digest(BUNDLE)!=spec['bundle_sha256']or set(spec['pins'])!=set(SOURCES):raise ValueError('frozen bundle differs')
  for p in SOURCES:
   if digest(ROOT/p)!=spec['pins'][p]:raise ValueError('source differs '+p)
 words=[0x7fc00001,0x7fa00001,0xffc00001,0x7f800000,0xff800000,0x7f7fffff,0xff7fffff,0]
 physical=[0x3d000000,0x3d000000,0x3d000000,0x7d000000,0x3d000000,0x7cffffff,0x3d000000,0x3d000000]
 native=public=failures=total_states=0
 for key in sorted(keys):
  case=spec['cases'][key];caps=bundle[key]
  if len(caps)!=4:raise ValueError('missing observed/control repeats')
  streams=[]
  for index,cap in enumerate(caps):
   mode='observe'if index<2 else'control';rows=cap['rows'];m=rows[0];f=rows[-1]
   mm=markers(cap['preload']);public+=len(mm)
   if m['task']!='NUM-03.2/nonfinite313'or m['mode']!=mode or m['sha256']!=SHA or not m['owned']or m['env']not in('B','C')or m['display']!={'B':':98','C':':99'}[m['env']]or m['remote']!={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[m['env']]or m['source_sha256']['map']!=case['map_sha256']or f['event']!='preload-file'or f.get('missing')or mm!=case['public']or f['markers']!=len(mm)or f['complete']!=case['complete']or hashlib.sha256(cap['preload'].encode()).hexdigest()!=f['sha256']or any(x.get('type')=='error'or x.get('event')=='trace-failed'for x in rows):raise ValueError('capture provenance/public control differs')
   for p in SOURCES[:7]:
    if m['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('captured source differs '+p)
   live=[x for x in rows if'seq'in x and x['event']!='trace-end'];ends=[x for x in rows if x['event']=='trace-end'];fault=[x for x in rows if x['event']=='native-exception']
   if mode=='control':
    if live or ends or fault:raise ValueError('hooked control')
    continue
   if [x['seq']for x in live]!=list(range(len(live))):raise ValueError('lost native events')
   if case['complete']:
    if len(ends)!=1 or not ends[0]['installed']or not ends[0]['readOnly']or ends[0]['seq']!=len(live)or dict(Counter(x['event']for x in live))!=ends[0]['counts']:raise ValueError('lost native events')
   elif ends or not any(x['event']=='owned-detached'and x.get('reason')=='process-terminated'for x in rows):raise ValueError('missing fault termination')
   if[x['value']for x in live if x['event']=='marker'and'label=start'not in x['value']]!=mm:raise ValueError('live/public outputs differ')
   if key=='radius':
    actual=[states(rows,'radius',sc)for sc in range(8)]
    if len(actual)!=8 or any(len(x)!=7 for x in actual):raise ValueError('missing radius state')
    if any(st['physical']!=physical[sc]or st['authored']!=words[sc]for sc,sts in enumerate(actual)for st in sts):raise ValueError('authored/physical radius changed')
   else:
    op,sc=key.split('-');sc=int(sc);actual=states(rows,op,sc)
    inputs=[x['value']for x in rows if x['event']=='public-default-height']
    if inputs!=[words[sc-8]]:raise ValueError('missing actual public coordinate producer')
   if actual!=case['states']:raise ValueError('literal retail states differ')
   total_states+=sum(map(len,actual))if key=='radius'else len(actual)
   if case['complete']:
    if fault:raise ValueError('unexpected fault')
   else:
    if len(fault)!=1 or fault[0]['rva']!=case['fault_rva']or fault[0]['type']!='access-violation'or fault[0]['memory']['operation']!='read'or actual[-1]['returned']:raise ValueError('failure scope differs')
    failures+=1
   native+=len(live);streams.append(normalize(live))
  if streams[0]!=streams[1]:raise ValueError('native repeat differs')
 if check_sources and(ROOT/SOURCES[-1]).read_text()!=render_header(spec['cases']):raise ValueError('engine golden differs from retail')
 return dict(captures=68,native_events=native,public_markers=public,states=total_states//2,fault_captures=failures)

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');ap.add_argument('--data',type=Path,default=ROOT/'build/tests');ap.add_argument('--report',type=Path,required=True);a=ap.parse_args()
 if a.report.exists():ap.error('new report required')
 spec=json.loads(SPEC.read_text());bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));evidence=validate(spec,bundle);original_bytes(a.binary,spec['instructions'])
 import verify_wc3_pathing_work242 as engine
 engine.TESTS=TESTS
 result=dict(passed=True,status='verified',task=spec['task'],instructions=len(spec['instructions']),**evidence,engine=engine.run_engine(a.test_binary,a.data,a.report),binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
