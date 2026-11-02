#!/usr/bin/env python3
"""Verify retail point-candidate keys, sorted publication, and canonical member order."""
import argparse,gzip,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
from verify_wc3_pathing_work237 import normalize as affinity
from verify_wc3_pathing_work240 import run_engine as previous_engine
from research.work241_oracle import original,header as rank_header
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work241-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_point_rank241.h'
SOURCES=['tools/frida/research/work241_probe.j','tools/frida/research/work241_make_map.py',
 'tools/frida/research/work241b_probe.j','tools/frida/research/work241b_make_map.py',
 'tools/frida/research/work241_observer.js','tools/frida/research/work228_capture.py',
 'tools/ghidra/research/work241_oracle.py','tools/ghidra/research/Work241Evidence.java',
 'tools/ghidra/research/foot03_spatial_harness_copy.py','tools/ghidra/verify_wc3_pathing_work237.py']
TESTS=['wc3_movement.selected241_original_comparator_words','wc3_movement.selected241_public_candidate_order_matches_retail_rows',
 'wc3_movement.selected240_float_queue_history_and_save','wc3_movement.selected238_alt_callbacks_keep_candidate_order_and_replacement',
 'wc3_movement.selected_player_input_matches_both_original_journeys',
 'wc3_movement.selected_independent_shift_inputs_match_original_complete_journeys']
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def normalize(rows):
 starts=[i for i,r in enumerate(rows)if r.get('event')=='packet'];result=[]
 for i,start in enumerate(starts):
  scope=rows[start:starts[i+1]if i+1<len(starts)else len(rows)]
  binds=[r for r in scope if r.get('event')=='bind'];roots={tuple(r['request']):r['members']for r in binds if r['depth']==0}
  if any(r['members']!=roots.get(tuple(r['request']))for r in binds):raise ValueError('recursive physical member order differs')
  base=affinity(scope);attachments=[r for r in scope if r.get('event')=='attach'];units={r['unit']:i for i,r in enumerate(attachments)}
  candidates=[r for r in scope if r.get('event')=='candidate'];scores=[r for r in scope if r.get('event')=='score'];poses=[r for r in scope if r.get('event')=='score-pose'];published=[r for r in scope if r.get('event')=='publish']
  if len(candidates)!=4 or len(scores)!=4 or len(poses)!=4 or len(published)!=4:raise ValueError('missing candidate/score/pose/publication')
  keys=[]
  for n,(c,s,p)in enumerate(zip(candidates,scores,poses)):
   if units[c['unit']]!=n or c['unit']!=s['unit'] or s['order']!=base['order'] or c['row'][3]!=s['value'] or c['row'][2]!=c['unit1b4'] or p['words'][2]!=0:raise ValueError('candidate score provenance differs')
   if c['row'][7]!=int(next(r['wrapper']for r in scope if r.get('event')=='retain'and r['unit']==c['unit']),16):raise ValueError('canonical association differs')
   keys.append([c['unit198'],*c['row'][1:6],c['canonical']])
  if len({tuple(s['point'])for s in scores})!=1:raise ValueError('point inputs differ')
  base.update(keys=keys,point=scores[0]['point'],poses=[p['words'][:2]for p in poses],published=[units[r['unit']]for r in published]);result.append(base)
 return result

def header(spec):
 out=rank_header(spec['kernel']).removesuffix('#endif\n')
 out+='/* Actual retail query poses/point scores; pre_move selects the captured active user head. */\n'
 out+='static struct {uint32_t alt,point[2],pose[4][2],score[4],order[4],births[3],count,pre_move[4];} const public241_rows[]={\n'
 fmt=lambda a:'{'+','.join(fmt(v)if isinstance(v,list)else str(v)+'u'for v in a)+'}'
 for r in [spec['public'][0][0],spec['public'][1][0],spec['public'][2][1],spec['public'][3][1]]:
  births=[g['members'][0]for g in r['groups']]
  values=[int(bool(r['flags']&16)),r['point'],r['poses'],[k[3]for k in r['keys']],r['published'],births+[0]*(3-len(births)),len(births),[k[2]for k in r['keys']]]
  out+=fmt(values)+',\n'
 return out+'};\n#endif\n'

def validate(spec):
 if(spec['version']!=1 or spec['task']!='GROUP-04.6' or spec['game_sha256']!=SHA or set(spec['pins'])!=set(SOURCES) or spec['engine_tests']!=TESTS or len(spec['kernel']['cases'])!=198 or len(spec['instructions'])<1000):raise ValueError('candidate contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('changed source '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']or HEADER.read_text()!=header(spec):raise ValueError('frozen capture/header differs')
 return spec

def validate_runtime(bundle,spec):
 if len(bundle['captures'])!=4:raise ValueError('missing four public witnesses')
 for i,c in enumerate(bundle['captures']):
  rows=c['rows'];meta=rows[0];end=rows[-1];preload=c['preload'];ends=[r for r in rows if r.get('event')=='trace-end']
  markers=re.findall(r'call Preload\( "(P241 [^"\r\n]*)" \)',preload)
  if(meta.get('mode')!='observe'or meta.get('sha256')!=SHA or not meta.get('owned')or meta['env']not in('B','C')or
   meta['source_sha256']['map']!=spec['maps'][i]or len(ends)!=1 or not ends[0]['installed']or ends[0]['dispatch']or ends[0]['depth']or ends[0]['count']!=(4 if i<2 else 8)or
   end.get('event')!='preload-file'or not end.get('complete')or end['markers']!=82 or len(markers)!=82 or markers!=[r['value']for r in rows if r.get('event')=='marker']or hashlib.sha256(preload.encode()).hexdigest()!=end['sha256']or
   any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('incomplete candidate capture')
  for p in SOURCES[4:6]:
   if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('captured observer/runner changed')
  inputs=[r for r in rows if r.get('event')=='player-input']
  if len(inputs)!=(1 if i<2 else 2)or any(r['rc']for r in inputs)or [r['plan']for r in inputs]!=meta['input']:raise ValueError('missing accepted UI input')
  actual=normalize(rows)
  if actual!=spec['public'][i]:raise ValueError('candidate rows/publication changed')
  for n,r in enumerate(actual):
   if r['order']!=851986 or [k[4]for k in r['keys']]!=[2]*4 or r['published']!=([0,3,1,2]if n==0 else [0,3,2,1]):raise ValueError('retail admitted order differs')
   expected=[[1],[0,2,3]]if not(r['flags']&16)else [[0,3],[1],[2]]if n==0 else [[0,3],[2],[1]]
   if [g['members']for g in r['groups']]!=expected:raise ValueError('canonical membership changed')
 return dict(captures=4,public_markers=328,public_packets=6)

def verify_live_comparisons(binary,public):
 from research.foot03_spatial_harness_copy import Emu
 e=Emu(binary);units=[e.fixture(0x200)for _ in range(2)];rows=[e.fixture(36)for _ in range(2)];count=0
 for capture in public:
  for packet in capture:
   order=packet['published']
   for i in range(4):
    for j in range(i+1,4):
     for u,p,k in zip(units,rows,[packet['keys'][order[i]],packet['keys'][order[j]]]):e.w(u+0x198,k[0]);e.w(u+0xc,k[6]);e.w(p,u,*k[1:6],0,0,0)
     value=e.call(0x6f6bcc40,0,*rows)
     if value<0x80000000:raise ValueError('native comparator rejects published order')
     count+=1
 return count

def run_engine(binary,data,report):
 import verify_wc3_pathing_work240 as previous
 before=previous.TESTS
 try:previous.TESTS=TESTS;return previous_engine(binary,data,report)
 finally:previous.TESTS=before

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 s=validate(json.loads(FIXTURE.read_text()));live=validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),s);original_bytes(a.binary,s['instructions'])
 if original(a.binary)!=s['kernel']:raise ValueError('original comparator differs')
 count=verify_live_comparisons(a.binary,s['public']);a.report.parent.mkdir(parents=True,exist_ok=True)
 result=dict(passed=True,task=s['task'],**live,comparator_cases=198,public_comparisons=count,instructions=len(s['instructions']),engine=run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(FIXTURE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=s['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
