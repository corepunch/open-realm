#!/usr/bin/env python3
"""Verify public finite coordinate producers and distinct authored/physical radii."""
import argparse,gzip,hashlib,json,re
from collections import Counter,defaultdict
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-numeric312-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
TESTS=['wc3_movement.numeric312*','wc3_slk.*','wc3_unit.*','wc3_spawn.*','wc3_fine_spatial.*','wc3_movement.place309*','wc3_movement.bridge311*','wc3_movement.goal310*','wc3_movement.place308*','wc3_movement.foot307*','wc3_save.rejects_prior_save_versions','wc3_movement.attack_chase_progresses_while_shared_route_jobs_compete']
SOURCES=['tools/frida/research/numeric312_'+x for x in('capture.py','observer.js','make_map.py','probe.j')]+['tools/frida/research/smart277_ui_input.c','tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c','tools/ghidra/research/Numeric312Evidence.java','games/warcraft-3/game/tests/fixtures/retail_numeric312.h']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(s):return re.findall(r'call Preload\( "(N312 [^"\r\n]*)" \)',s)
def normalize(v):
 if isinstance(v,list):return[normalize(x)for x in v]
 if isinstance(v,dict):return{k:normalize(x)for k,x in v.items()}
 if isinstance(v,str)and re.fullmatch('0x[0-9a-f]+',v):return v!='0x0'
 return v

def states(rows):
 result=[];by_scene=defaultdict(list)
 for x in rows:
  if 'scene'in x:by_scene[x['scene']].append(x)
 for sc in range(320):
  rr=by_scene[sc];prior=-1;last=None;raw=None;steps=[]
  if sum(x['event']=='marker'and'label=end-cleanup'in x['value']for x in rr)!=1:raise ValueError('incomplete scene')
  for label in('created','placed','scale-negative','queryzero','axis-setters'):
   mm=[x for x in rr if x['event']=='marker'and'label='+label+' 'in x['value']]
   if len(mm)!=1:raise ValueError('missing/duplicate stage')
   block=[x for x in rr if prior<x.get('seq',-1)<mm[0]['seq']]
   for x in block:
    if x['event']=='delta-leave':last=x['m']
    if x['event']=='authored-radius':raw=x['radius']
   getter=[x for x in block if x['event']=='public-coordinate']
   if last is None or raw is None or len(getter)!=2 or[x['axis']for x in getter]!=[0,1]:raise ValueError('missing observed words')
   steps.append(dict(fine=last['pos'],world=[x['value']for x in getter],physical=last['radius'],authored=raw));prior=mm[0]['seq']
  result.append(steps)
 return result

def render_header(expected):
 s='/* Exact public retail numeric312 words; no engine-derived expectations. */\n#pragma once\nstatic uint32_t const numeric312_states[320][5][6]={\n'
 for steps in expected:s+=' {'+','.join('{'+','.join('0x%08xu'%x for x in st['fine']+st['world']+[st['physical'],st['authored']])+'}'for st in steps)+'},\n'
 return s+'};\n'

def validate(spec,bundle,check_sources=True):
 if spec['version']!=1 or spec['task']!='NUM-03.1'or spec['game_sha256']!=SHA or len(bundle)!=3 or spec['engine_tests']!=TESTS:raise ValueError('specification differs')
 if len(spec['public'])!=2241 or len(spec['states'])!=320:raise ValueError('incomplete frozen contract')
 if spec['instruction_count']!=327 or len(spec['instructions'])!=327:raise ValueError('missing instruction guards')
 if check_sources:
  if digest(BUNDLE)!=spec['bundle_sha256']or set(spec['pins'])!=set(SOURCES):raise ValueError('frozen bundle differs')
  for p in SOURCES:
   if digest(ROOT/p)!=spec['pins'][p]:raise ValueError('source differs '+p)
 streams=[]
 for index,cap in enumerate(bundle):
  rows=cap['rows'];m=rows[0];f=rows[-1];public=markers(cap['preload']);mode='control'if index==2 else'observe'
  if m['task']!='NUM-03.1/numeric312'or m['mode']!=mode or m['sha256']!=SHA or not m['owned']or m['env']not in('B','C')or m['display']!={'B':':98','C':':99'}[m['env']]or m['remote']!={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[m['env']]or m['source_sha256']['map']!=spec['map_sha256']or f['event']!='preload-file'or not f.get('complete')or public!=spec['public']or f['markers']!=2241 or hashlib.sha256(cap['preload'].encode()).hexdigest()!=f['sha256']or any(x.get('type')=='error'or x.get('event')=='trace-failed'for x in rows):raise ValueError('capture provenance/public control')
  for p in SOURCES[:7]:
   if m['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('captured source differs '+p)
  live=[x for x in rows if'seq'in x and x['event']!='trace-end'];ends=[x for x in rows if x['event']=='trace-end']
  if mode=='control':
   if live or ends:raise ValueError('hooked control')
   continue
  if len(ends)!=1 or not ends[0]['installed']or not ends[0]['readOnly']or ends[0]['seq']!=len(live)or len(live)!=25602 or[x['seq']for x in live]!=list(range(len(live)))or dict(Counter(x['event']for x in live))!=ends[0]['counts']or ends[0]['counts']!=spec['event_counts']:raise ValueError('lost native events')
  if[x['value']for x in live if x['event']=='marker'and'label=start'not in x['value']]!=public:raise ValueError('live/preload markers differ')
  if states(rows)!=spec['states']:raise ValueError('literal states differ')
  streams.append(normalize(live))
 if streams[0]!=streams[1]:raise ValueError('native repeat differs')
 if check_sources and(ROOT/SOURCES[-1]).read_text()!=render_header(spec['states']):raise ValueError('engine golden differs from retail')
 return dict(captures=3,native_events=25602,public_markers=2241,states=1600,scalar_words=9600)

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');ap.add_argument('--data',type=Path,default=ROOT/'build/tests');ap.add_argument('--report',type=Path,required=True);a=ap.parse_args()
 if a.report.exists():ap.error('new report required')
 spec=json.loads(SPEC.read_text());bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));evidence=validate(spec,bundle);original_bytes(a.binary,spec['instructions'])
 import verify_wc3_pathing_work242 as engine
 engine.TESTS=TESTS
 result=dict(passed=True,status='verified',task=spec['task'],instructions=len(spec['instructions']),**evidence,engine=engine.run_engine(a.test_binary,a.data,a.report),binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
