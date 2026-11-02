#!/usr/bin/env python3
"""Verify all-class goal routes and target-perimeter completion against exact retail captures."""
import argparse,gzip,hashlib,json,re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-goal310-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
TESTS=['wc3_movement.goal310*','wc3_fine_spatial.*','wc3_movement.place309*','wc3_movement.goal308*','wc3_movement.place308*','wc3_movement.foot307*','wc3_movement.stop247*','wc3_movement.stop258*','wc3_movement.target*','wc3_movement.follow*','wc3_movement.smart*','wc3_movement.retail_gate_blocked_and_outside*','wc3_movement.public_move_matches_original*','wc3_order_lifecycle.*','wc3_interrupt.*','wc3_save.rejects_prior_save_versions']
SOURCES=['tools/frida/research/goal310_'+s for s in('capture.py','observer.js','make_map.py','probe.j')]+['tools/frida/research/smart277_ui_input.c','tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c','tools/ghidra/research/Goal310Evidence.java','games/warcraft-3/game/skills/fixtures/retail_goal310_motion.h']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(G310 [^"\r\n]*)" \)',text)
def normalize(value):
 if isinstance(value,list):return [normalize(x)for x in value]
 if isinstance(value,dict):return {k:normalize(v)for k,v in value.items()}
 if isinstance(value,str)and re.fullmatch('0x[0-9a-f]+',value):return value!='0x0'
 return value

def goal_motion(rows):
 scenes=[]
 for scene in range(96):
  rr=[x for x in rows if x.get('scene')==scene]
  if sum(x['event']=='marker'and'label=end-cleanup'in x['value']for x in rr)!=1:raise ValueError('incomplete scene')
  bind=next(x for x in rr if x['event']=='bind');mid=bind['m']['id']
  before=[x for x in rr if x['event']=='commit-enter'and x['g']['count']==1 and x['g']['members'][0]['id']==mid]
  after=[x for x in rr if x['event']=='commit-leave'and x['g']['count']==1 and x['g']['members'][0]['id']==mid]
  if len(before)!=len(after)or not before:raise ValueError('unpaired commits')
  data=[]
  for a,b in zip(before,after):
   if a['c']!=b['c']:raise ValueError('unpaired counter')
   m=a['g']['members'][0]['m'];out=b['g']['members'][0]['m']
   data.append([a['c'],*out['clock'],*m['pos'],*m['vel'],m['range'],m['heading'],*out['pos'],*out['vel'],out['heading'],a['tick'],a['g']['flags']])
  fine=next((x for x in rr if x['event']=='fine-leave'),None)
  removal=next((x for x in rr if x['event']=='marker'and'label=before-removal'in x['value']),None)
  final=next(x for x in rr if x['event']=='marker'and'label=begin-cleanup'in x['value'])
  final_order=int(re.search(r'order=(\d+)',final['value'])[1])
  poses=[]
  for x in rr:
   if x['seq']>=final['seq']:break
   if x['event']=='recover-leave' and x['m']['id']==mid:poses.append((x['seq'],x['m']['pos']))
   if x['event']=='commit-leave' and x['g']['count']==1 and x['g']['members'][0]['id']==mid:poses.append((x['seq'],x['g']['members'][0]['m']['pos']))
  finalpose=max(poses)[1]
  scenes.append(dict(clock=bind['m']['clock'],counter=bind['c'],rows=data,fine={k:fine[k]for k in('result','points','index','flags')}if fine else None,removal=removal,removal_index=sum(x['seq']<removal['seq']for x in before)if removal else 0,final_order=final_order,final=final,finalpose=finalpose))
 return scenes

def render_header(scenes):
 header=['/* Exact retail goal310 physical states; literal raw capture words. */','#pragma once']
 for i,s in enumerate(scenes):
  header+=['static uint32_t const goal310_motion'+str(i)+'[][16]={']+[' {'+','.join('0x%08xu'%x for x in row)+'},'for row in s['rows']]+['};']
  if s['fine']:header+=['static uint32_t const goal310_route'+str(i)+'[][2]={']+[' {'+','.join('0x%08xu'%x for x in row)+'},'for row in s['fine']['points']]+['};']
 header+=['static struct {uint32_t clock[2],counter;uint32_t const (*rows)[16];unsigned count;uint32_t const (*route)[2];unsigned route_count,index;uint32_t removal_clock[2],removal_counter;unsigned removal_index;uint32_t final_order,final_clock[2],final_counter,final_pose[2];} const goal310_motion[]={']
 for i,s in enumerate(scenes):
  fine=s['fine'];rem=s['removal'];header+=[' {{'+','.join('0x%08xu'%x for x in s['clock'])+'},'+str(s['counter'])+',goal310_motion'+str(i)+','+str(len(s['rows']))+','+('goal310_route'+str(i)+','+str(len(fine['points']))+','+str(fine['index'])if fine else'NULL,0,0')+(',{'+','.join('0x%08xu'%x for x in rem['clock'][:2])+'},'+str(rem['c'])if rem else',{0,0},0')+','+str(s['removal_index'])+','+str(s['final_order'])+',{'+','.join('0x%08xu'%x for x in s['final']['clock'][:2])+'},'+str(s['final']['c'])+',{'+','.join('0x%08xu'%x for x in s['finalpose'])+'}},']
 header+=['};']
 return '\n'.join(header)+'\n'

def validate(spec,bundle=None):
 if spec['version']!=1 or spec['task']!='FOOT-04.2'or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS or len(spec['instructions'])!=spec['instruction_count']or spec['instruction_count']<500 or set(spec['pins'])!=set(SOURCES):raise ValueError('goal contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('source pin '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('frozen captures changed')
 caps=bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
 if len(caps)!=3 or [c['rows'][0]['mode']for c in caps]!=['observe','observe','control']:raise ValueError('two repeats and unhooked control required')
 streams=[]
 for cap in caps:
  rows=cap['rows'];meta=rows[0];footer=rows[-1];public=markers(cap['preload'])
  if meta['task']!='FOOT-04.2/goal310'or meta['sha256']!=SHA or not meta['owned']or meta['env']not in('B','C')or meta['source_sha256']['map']!=spec['map_sha256']or footer['event']!='preload-file'or not footer.get('complete')or public!=spec['public']or len(public)!=7009 or footer['markers']!=7009 or hashlib.sha256(cap['preload'].encode()).hexdigest()!=footer['sha256']or any(x.get('type')=='error'or x.get('event')=='trace-failed'for x in rows):raise ValueError('capture provenance or public control')
  for p in SOURCES[:7]:
   if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('captured source differs')
  live=[x for x in rows if 'seq'in x and x['event']!='trace-end']
  if meta['mode']=='control':
   if live or any(x['event']=='trace-end'for x in rows):raise ValueError('hooked control')
   continue
  ends=[x for x in rows if x['event']=='trace-end']
  if len(ends)!=1 or not ends[0]['installed']or not ends[0]['readOnly']or ends[0]['seq']!=len(live)or len(live)!=67760 or [x['seq']for x in live]!=list(range(len(live)))or dict(Counter(x['event']for x in live))!=ends[0]['counts']:raise ValueError('lost native observer events')
  if [x['value']for x in live if x['event']=='marker'and'label=start'not in x['value']]!=public:raise ValueError('live/preload marker mismatch')
  if goal_motion(rows)!=spec['motion']:raise ValueError('frozen motion differs')
  streams.append(normalize(live))
 if streams[0]!=streams[1]:raise ValueError('repeated native streams differ')
 if (ROOT/SOURCES[-1]).read_text()!=render_header(spec['motion']):raise ValueError('literal engine states differ from retail')
 terminals=[x for x in caps[0]['rows']if x['event']=='advance-leave'and x['result']==3]
 if [(x['scene'],x['c'])for x in terminals]!=[(6,2643),(14,4443),(22,6242),(38,9843),(46,11643),(54,13442),(70,17043),(78,18843),(86,20641)]:raise ValueError('target-perimeter transition differs')
 return dict(captures=2,controls=1,scenes=96,motion_states=sum(len(x['rows'])for x in spec['motion']),first_route_points=sum(len(x['fine']['points'])for x in spec['motion']),public_markers=7009,native_records=67760,perimeter_completions=len(terminals))

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());evidence=validate(spec);original_bytes(a.binary,spec['instructions'])
 import verify_wc3_pathing_work242 as engine
 engine.TESTS=TESTS
 result=dict(passed=True,status='verified',task=spec['task'],instructions=len(spec['instructions']),**evidence,engine=engine.run_engine(a.test_binary,a.data,a.report),binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
