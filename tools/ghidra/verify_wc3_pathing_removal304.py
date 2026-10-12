#!/usr/bin/env python3
"""Check attached Move death ownership against repeated original retail captures."""
import argparse, gzip, hashlib, json, re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes, SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-removal304-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
TESTS=['wc3_movement.removal304*','wc3_movement.owner303*','wc3_movement.scope301*','wc3_movement.scope300*',
       'wc3_ability_dispatch.*','wc3_order_lifecycle.*','wc3_repulsion_policy.*',
       'wc3_unit_releases.*','wc3_save.*','wc3_building.*']
SOURCES=['tools/frida/research/removal304_'+x for x in ('capture.py','observer.js','make_map.py','probe.j')]+[
 'tools/ghidra/research/Removal304Evidence.java','tools/frida/research/group032_make_map.py',
 'tools/frida/research/sep_research_map.py','tools/frida/make_wc3_pathfinding_map.py',
 'tools/frida/research/smart277_ui_input.c','tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(R304 [^"\r\n]*)" \)',text)
def normalize(rows):
 out=[]
 for scene in range(6):
  stages=[]
  for row in rows:
   if row.get('scene')!=scene or 'seq' not in row:continue
   state=row.get('state');value={k:row[k]for k in ('event','caller','task_event')if k in row}
   if state:
    value['state']={k:state[k]for k in ('code','flags','suppression')}
    value['state']['abilities']=[{k:a[k]for k in ('classGetter','disabled')}for a in state['abilities']]
   stages.append(value)
  out.append(stages)
 return out

def validate_runtime(bundle,spec):
 captures=bundle['captures'];streams=[]
 if len(captures)!=3 or [c['rows'][0]['mode']for c in captures]!=['observe','observe','control']:
  raise ValueError('two repeats and observer-free control required')
 for cap in captures:
  rows=cap['rows'];meta=rows[0];footer=rows[-1];preload=cap['preload']
  if(meta.get('event')!='metadata' or meta.get('task')!='MAP-04.2/removal304' or meta.get('sha256')!=SHA or
     not meta.get('owned') or meta.get('env') not in ('B','C') or meta['source_sha256']['map']!=spec['map_sha256'] or
     footer.get('event')!='preload-file' or not footer.get('complete') or footer.get('markers')!=36 or
     hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256'] or markers(preload)!=spec['markers'] or
     any(r.get('type')=='error' or r.get('event')=='trace-failed'for r in rows)):
   raise ValueError('unowned, failed, incomplete or substituted capture')
  for p in SOURCES[:4]+SOURCES[-3:]:
   if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('capture source differs')
  for p,h in spec['helper_sha256'].items():
   if meta['source_sha256'][p]!=h:raise ValueError('input helper differs')
  if any(r.get('rc')for r in rows if r.get('event')=='owned-loading-input'):raise ValueError('loading input failed')
  if meta['mode']=='control':
   if any('seq'in r or r.get('event')=='trace-end'for r in rows):raise ValueError('instrumented control')
   continue
  ends=[r for r in rows if r.get('event')=='trace-end']
  if(len(ends)!=1 or not ends[0].get('installed') or not ends[0].get('readOnly') or ends[0].get('depth') or
     dict(Counter(r['event']for r in rows if 'seq'in r))!=ends[0]['counts'] or
     [r['seq']for r in rows if 'seq'in r]!=list(range(sum(ends[0]['counts'].values()))) or
     [r['value']for r in rows if r.get('event')=='marker']!=spec['markers']):
   raise ValueError('observer stream incomplete')
  for scene in range(6):
   scoped=[r for r in rows if r.get('scene')==scene and 'seq'in r]
   events=[r['event']for r in scoped]
   begin=events.index('remove-enter');end=events.index('remove-exit');final=events.index('inactivity-enter')
   cancel=events.index('move-cancel');destroy=events.index('destroy')
   stops=[(i,r)for i,r in enumerate(scoped)if r['event']=='unit-stop-enter']
   expected=['0x600433']*(3 if scene>=4 else 2)+['0x5fc18a']
   if([r['caller']for i,r in stops]!=expected or not end<final<cancel<stops[-1][0]<destroy or
      sum(begin<i<end for i,r in stops)!=2 or sum(i<begin for i,r in stops)!=(1 if scene>=4 else 0)):
    raise ValueError('synchronous/deferred removal chronology differs')
   last=stops[-1][1]['state']
   owners=[a for a in last['abilities']if a['classGetter']=='0x5fcd40']
   if not last['flags']&0x100 or len(owners)!=1 or owners[0]['disabled']!=[1,0,0]:
    raise ValueError('dead flag/count store order differs')
  streams.append(normalize(rows))
 if streams!=[spec['stages']]*2:raise ValueError('normalized native stages differ')
 return dict(retail_observations=2,retail_controls=1,public_markers=108,retail_scenes=12)

def validate(spec):
 if(spec['version']!=1 or spec['task']!='MAP-04.2' or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS or
    set(spec['pins'])!=set(SOURCES) or spec['function_count']!=4 or
    len(spec['instructions'])!=spec['instruction_count'] or not spec['instructions'] or len(spec['stages'])!=6):
  raise ValueError('owner contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('source differs '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle substituted')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));validate_runtime(bundle,spec);return bundle

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
 p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests')
 p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=json.loads(SPEC.read_text());bundle=validate(spec);original_bytes(a.binary,spec['instructions'])
 import verify_wc3_pathing_work242 as runner
 old=runner.TESTS
 try:
  runner.TESTS=TESTS;a.report.parent.mkdir(parents=True,exist_ok=True);engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=old
 result=dict(passed=True,status='verified',task=spec['task'],**validate_runtime(bundle,spec),instructions=len(spec['instructions']),
             engine=engine,binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
