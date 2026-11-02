#!/usr/bin/env python3
"""Verify public Slow admission, facing, cast phases and complete Captain motion."""
import argparse,gzip,json
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_captain320 import project,markers,require,digest,ROOT
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
SPEC=ROOT/'tools/ghidra/fixtures/retail-cast321-1.27.json'
PARENT=ROOT/'tools/ghidra/fixtures/retail-captain320-1.27.json'
TESTS=['wc3_movement.cast321*','wc3_movement.captain320*','wc3_movement.spell185*','wc3_movement.spell269*','wc3_movement.completion196*','wc3_movement.structure279*','wc3_movement.flight280*','wc3_spell.*','wc3_ability_dispatch.*','wc3_save.rejects_prior_save_versions']
SOURCES=['tools/frida/research/cast321'+x for x in ('_capture.py','_make_map.py','_observer.js')]+['tools/ghidra/research/Cast321Evidence.java','games/warcraft-3/game/tests/fixtures/retail_cast321.h','games/warcraft-3/tests/resources-src/Scripts/cast321.j']
PHASES={'spell-request':0x405fff5c,'spell-channel':0x406665b8,'spell-cast':0x406665b8,'spell-effect':0x40819926,'spell-concrete':0x40819926}
def caster_rows(rows):
 caster=next(x['m']['id']for x in rows if x.get('event')=='physical-radius'and x['m']['pos']==[0x41700000,0x41600000])
 result=[]
 for x in rows:
  if x.get('event')=='commit-leave':
   for member in x['g']['members']:
    if member['id']==caster and member['m']:
     m=member['m'];result.append([x['c'],*m['pos'],*m['vel'],m['heading'],m['clock'][0]])
 return result

def header(rows):
 return '/* Frozen read-only spell-facing commits; Captain320 scene, Cast321 repeats. */\nstatic uint32_t const cast321_caster_rows[][7]={\n'+''.join('    {'+','.join(f'0x{x:08x}u'for x in row)+'},\n'for row in rows)+'};\n'

def validate(spec,bundle,check_sources=True):
 require(spec['version']==1 and spec['task']=='ORDER-01.13'and spec['binary_sha256']==SHA,'identity differs')
 require(spec['engine_tests']==TESTS and set(spec['pins'])==set(SOURCES),'scope differs')
 require(digest(PARENT)==spec['parent_sha256'],'frozen parent differs')
 parent=json.loads(PARENT.read_text())
 require(len(bundle)==3 and len(spec['capture_sources'])==3,'two repeats and control required')
 require(len(spec['caster_rows'])==4,'caster scope differs')
 if check_sources:
  for path,h in spec['pins'].items():require(digest(ROOT/path)==h,'source differs: '+path)
  require((ROOT/SOURCES[4]).read_text()==header(spec['caster_rows']),'literal caster header differs')
 events=0
 for i,cap in enumerate(bundle):
  rows=cap['rows'];meta,end=rows[0],rows[-1];control=i==2
  require(meta['event']=='metadata'and meta['task']=='ORDER-01.13/cast321'and meta['sha256']==SHA and meta['owned']is True and meta['mode']==('control'if control else'observe'),'capture identity differs')
  require(meta['env']in('B','C')and meta['display']=={'B':':98','C':':99'}[meta['env']]and meta['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[meta['env']],'unowned capture')
  require(meta['source_sha256']==spec['capture_sources'][i]and meta['source_sha256']['map']==spec['map_sha256'],'capture provenance differs')
  for path,h in spec['pins'].items():
   if Path(path).name in meta['source_sha256']:require(meta['source_sha256'][Path(path).name]==h,'captured source differs')
  require(end['event']=='preload-file'and end.get('complete')is True and end['markers']==368,'incomplete capture')
  import hashlib
  require(hashlib.sha256(cap['preload'].encode()).hexdigest()==end['sha256'],'preload hash differs')
  require(markers(cap['preload'])==parent['markers'],'public control differs')
  require(not any(r.get('type')=='error'or r.get('event')in('native-exception','trace-failed')for r in rows),'faulting trace')
  live=[r for r in rows if 'seq'in r and r['event']!='trace-end'];ends=[r for r in rows if r['event']=='trace-end']
  if control:require(not live and not ends,'hooked control');continue
  require([r['seq']for r in live]==list(range(len(live)))and len(ends)==1,'lost events')
  require(ends[0]['seq']==len(live)and ends[0]['counts']==dict(Counter(r['event']for r in live))and ends[0]['readOnly']is True and ends[0]['installed']is True,'trace accounting differs')
  require([r['value']for r in live if r['event']=='marker'and r['tick']!=1]==parent['markers'],'observer/public differs')
  require(project(rows)==parent['projection'],'frozen Captain trajectory differs')
  require(caster_rows(rows)==spec['caster_rows'],'frozen facing trajectory differs')
  for phase,clock in PHASES.items():
   found=[r for r in rows if r.get('event')==phase]
   require(len(found)==1 and found[0]['primary']==[clock,0,0x43960000]and found[0]['delays']==[0,0x3ee66667,0x3f8a3d71],'cast phase differs: '+phase)
   require(found[0]['vtable']=='0xb0c410','cast class differs')
  facing=[r for r in rows if r.get('event')=='spell-facing-enter'];results=[r for r in rows if r.get('event')=='spell-facing-leave'];tasks=[r for r in rows if r.get('event')=='spell-target-task']
  require(len(facing)==len(results)==len(tasks)==1 and facing[0]['angle']==0x3e4ccccd and results[0]['result']==0 and tasks[0]['range']==0x7f7fffff,'turn admission differs')
  commands=[];pending=None
  for row in rows:
   event=row.get('event')
   if event in('target-order','immediate-order'):
    require(pending is None,'nested command');pending=[row['order']]
   elif event in('target-order-result','immediate-order-result'):
    require(pending is not None,'unpaired command result');commands.append(pending+[row['result']]);pending=None
  require(pending is None and commands==[[852076,0],[852077,1],[852075,1],[851972,1],[851972,1],[851972,1]],'public command acceptance differs')
  events+=len(live)
 return dict(captures=2,controls=1,public_markers=1104,native_events=events,caster_commits=4,phases=5,motion_rows=861,actor_rows=293,shared_rows=215,public_rows=270)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 require(not a.report.exists(),'new report required');s=json.loads(SPEC.read_text());b=json.loads(gzip.decompress(SPEC.with_suffix('.json.gz').read_bytes()))
 require(digest(SPEC.with_suffix('.json.gz'))==s['bundle_sha256'],'bundle differs');evidence=validate(s,b);original_bytes(a.binary,s['instructions'])
 import verify_wc3_pathing_work242 as engine
 engine.TESTS=TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
 report=dict(passed=True,status='verified',task=s['task'],**evidence,instructions=len(s['instructions']),engine=engine.run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(SPEC),binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=s['limits'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
