#!/usr/bin/env python3
"""Verify retained Slow/Bloodlust factors from completed public retail captures."""
import argparse,gzip,hashlib,json,re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-modifiers316-1.27.json'
TESTS=['wc3_movement.modifiers316*','wc3_movement.temporary_modifiers_match_original_velocity_and_saved_restoration','wc3_spell.*','wc3_save.*','wc3_movement.shared315*']
SOURCES=['tools/frida/research/modifiers316_'+s for s in ('capture.py','make_map.py','observer.js','probe.j','reverse_make_map.py','custom_make_map.py')]+['tools/ghidra/research/Modifiers316Evidence.java','games/warcraft-3/game/tests/fixtures/retail_modifiers316.h']
def require(ok,message):
 if not ok:raise ValueError(message)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(M316 [^"\r\n]*)" \)',text)
def projection(rows):
 factors=[];pending=None;speed=None;public=[]
 for r in rows:
  e=r.get('event')
  if e=='factor-enter':
   require(pending is None,'nested factor event');pending=r
   require(r['caller'] in ('0x52d4dc','0x532511'),'unknown applying owner')
  elif e=='factor-leave':
   require(pending is not None and pending['owner']['unit']==r['owner']['unit'],'unpaired factor event')
   factors.append([pending['delta'],pending['owner']['factor'],r['owner']['factor']]);pending=None
  elif e=='public-speed':speed=r['value']
  elif e=='marker':
   if speed is not None:public.append([r['tick'],re.search(r'label=(\S+)',r['value'])[1],speed])
   speed=None
 require(pending is None,'missing factor return')
 return {'factors':factors,'public':public}
def render_header(cases):
 lines=['/* Literal public GetUnitMoveSpeed words: first buff, overlap, haste removed, both removed. */','static uint32_t const modifiers316_speed[][4]={']
 for key in ('finite','reverse','custom'):
  rows=cases[key]['projection']['public'];values=[next(r[2]for r in rows if r[0]==tick)for tick in (35,40,50,60)]
  lines.append('    {'+','.join(f'0x{x:08x}u'for x in values)+'}'+(','if key!='custom'else''))
 lines+=['};','static uint32_t const modifiers316_factor[]={'+','.join(f"0x{cases[k]['projection']['factors'][1][2]:08x}u"for k in ('finite','reverse','custom'))+'};']
 return '\n'.join(lines)+'\n'
def validate(spec,bundle,check_sources=True):
 require(spec['task']=='NUM-03.2' and spec['version']==1 and spec['binary_sha256']==SHA,'wrong identity')
 require(set(bundle)==set(spec['cases'])=={'finite','reverse','custom'},'missing scene')
 require(spec['engine_tests']==TESTS,'engine scope changed')
 require(len(spec['instructions'])==spec['instruction_count']==172,'instruction guards missing')
 require(set(spec['pins'])==set(SOURCES),'source inventory differs')
 if check_sources:
  for path,sha in spec['pins'].items():require(digest(ROOT/path)==sha,'source differs: '+path)
  require((ROOT/SOURCES[-1]).read_text()==render_header(spec['cases']),'engine literal fixture differs')
 events=0
 for key,captures in bundle.items():
  require(len(captures)==3,'missing repeat/control')
  case=spec['cases'][key];require(len(case['markers'])==102,'marker count differs')
  for i,cap in enumerate(captures):
   rows=cap['rows'];require(len(rows)>1,'empty capture');meta,end=rows[0],rows[-1]
   mode='observe'if i<2 else'control'
   require(meta['event']=='metadata' and meta['task']=='NUM-03.2/modifiers316' and meta['sha256']==SHA and meta['mode']==mode and meta['owned'] is True,'capture identity differs')
   require(meta['env'] in ('B','C') and meta['display']=={'B':':98','C':':99'}[meta['env']] and meta['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[meta['env']],'unowned capture')
   require(meta['source_sha256']==case['capture_sources'],'captured source/map differs')
   require(end['event']=='preload-file' and end.get('complete') is True and end.get('markers')==102,'incomplete capture')
   require(hashlib.sha256(cap['preload'].encode()).hexdigest()==end['sha256'],'preload hash differs')
   require(markers(cap['preload'])==case['markers'],'control marker mismatch')
   require(not any(r.get('type')=='error' or r.get('event') in ('native-exception','trace-failed')for r in rows),'faulting capture')
   live=[r for r in rows if 'seq'in r and r['event']!='trace-end'];ends=[r for r in rows if r['event']=='trace-end']
   if mode=='control':require(not live and not ends,'hooked control');continue
   require([r['seq']for r in live]==list(range(len(live))) and len(ends)==1,'lost native events')
   require(ends[0]['seq']==len(live) and ends[0]['counts']==dict(Counter(r['event']for r in live)) and ends[0].get('installed') is True and ends[0].get('readOnly') is True,'trace accounting differs')
   require([r['value']for r in live if r['event']=='marker' and r['tick']!=1]==case['markers'],'observer/preload differs')
   require(projection(rows)==case['projection'],'factor/getter stream differs')
   events+=len(live)
 return {'captures':9,'public_markers':918,'native_events':events}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
 p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 require(not a.report.exists(),'new report required');spec=json.loads(SPEC.read_text());bundle=json.loads(gzip.decompress(SPEC.with_suffix('.json.gz').read_bytes()))
 evidence=validate(spec,bundle);original_bytes(a.binary,spec['instructions'])
 import verify_wc3_pathing_work242 as engine
 engine.TESTS=TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
 report=dict(passed=True,status='verified',task=spec['task'],**evidence,instructions=len(spec['instructions']),engine=engine.run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(SPEC),binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
