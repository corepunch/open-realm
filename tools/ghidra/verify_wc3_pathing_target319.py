#!/usr/bin/env python3
"""Verify public target Follow/Smart isolation and building footprint publication, motion, and captured completion inputs."""
import argparse,gzip,hashlib,json,re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-target319-1.27.json'
TESTS=['wc3_movement.target319*','wc3_movement.slow318*','wc3_movement.profile271*','wc3_save.rejects_prior_save_versions']
SOURCES=['tools/frida/research/target319'+x for x in ('_capture.py','_make_map.py','_probe.j','_observer.js','_smart_make_map.py','_smart_probe.j','_structure_make_map.py','_structure_probe.j')]+['tools/ghidra/research/Target319Evidence.java']+['games/warcraft-3/game/tests/fixtures/'+n for n in ('retail_target319.h','retail_target319_smart.h','retail_target319_structure.h','target319_script.h','target319_smart_script.h','target319_structure_script.h')]+['games/warcraft-3/tests/resources-src/PathTextures/16x16Simple.tga']
INSTRUCTION_COUNT=385
def require(value,message):
 if not value:raise ValueError(message)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(T319 [^"\r\n]*)" \)',text)
def project(rows):
 actors=[next(x['m']['id']for x in rows if x.get('event')=='physical-radius'and x['m']['pos']==p)for p in ([0x41c00000,0x41800000],[0x42400000,0x42000000])]
 motion=[];public=[];coords=[];speed=0;isolation=[];binds=[];regions=[];radii=[];owners=[]
 for x in rows:
  e=x.get('event')
  if e=='commit-leave':
   for member in x['g']['members']:
    if member['id']in actors and member['m']:
     g=x['g'];owners.append([actors.index(g['target'])if g['target']in actors else -1,bool(g['region']),g['flags']])
     m=member['m'];motion.append([actors.index(member['id']),*m['clock'],*m['pos'],*m['vel'],m['heading'],m['maximum'],m['range'],member['requested']])
  if e=='public-coordinate':coords.append(x['value'])
  if e=='public-speed':speed=x['value']
  if e=='marker':
   label=re.search(r'label=(\S+)',x['value'])[1]
   if label in ('start','begin-setup'):coords=[];continue
   require(len(coords)==2,'coordinate query pair differs')
   public.append([x['tick'],int(re.search(r'u=(\d+)',x['value'])[1]),*x['primary'][:2],*coords,speed,int(re.search(r'order=(\d+)',x['value'])[1])]);coords=[]
  if e in ('block-enter','block-leave','unblock-enter','unblock-leave'):
   o=x['owner'];isolation.append([e,*x['primary'][:2],o['base'],o['factor'],o['blocked'],x.get('caller','')])
  if e=='bind'and x['m']['id']in actors:
   g=x['g'];binds.append([actors.index(x['m']['id']),*x['primary'][:2],*g['goal'],g['flags'],g['count'],[[actors.index(m['id'])if m['id']in actors else -1,*m['offset']]for m in g['members']]])
  if e=='republish-enter':regions.append([*x['primary'][:2],bool(x['g']['region']),x['g']['flags'],x['g']['count']])
  if e=='physical-radius' and x['m']['id']in actors:radii.append([actors.index(x['m']['id']),x['radius']])
 return dict(motion=motion,public=public,isolation=isolation,binds=binds,regions=regions,radii=radii,owners=owners)
def header(name,rows):
 return '/* Frozen target Follow motion; captured Slow completion input. */\n'+''.join('static uint32_t const '+name+'_'+k+'[]['+str(len(v[0]))+']={\n'+''.join(' {'+','.join(f'0x{x:08x}u'for x in r)+'},\n'for r in v)+'};\n'for k,v in ((k,rows[k])for k in ('motion','public')))
def validate(spec,bundle,check_sources=True):
 require(spec['version']==1 and spec['task']=='NUM-03.2'and spec['binary_sha256']==SHA,'identity differs')
 require(spec['engine_tests']==TESTS,'engine scope differs')
 require(set(spec['pins'])==set(SOURCES),'source inventory differs')
 require(len(spec['instructions'])==INSTRUCTION_COUNT,'instruction guards differ')
 require(set(spec['cases'])==set(bundle)=={'follow','smart','structure'},'scenes differ')
 if check_sources:
  for p,h in spec['pins'].items():require(digest(ROOT/p)==h,'source differs: '+p)
 events=0
 for name,caps in bundle.items():
  case=spec['cases'][name];require(len(caps)==3,'repeat/control count differs')
  expected=case['projection'];require(len(expected['motion'])=={'follow':324,'smart':324,'structure':127}[name]and len(expected['public'])==184,'motion scope differs')
  if check_sources:require((ROOT/case['header']).read_text()==header(case['symbol'],expected),'literal motion header differs')
  for i,cap in enumerate(caps):
   rows=cap['rows'];meta,end=rows[0],rows[-1];mode='control'if i==2 else'observe'
   require(meta['event']=='metadata'and meta['task']=='NUM-03.2/target319'and meta['sha256']==SHA and meta['owned']is True and meta['mode']==mode,'capture identity differs')
   require(meta['env']in ('B','C')and meta['display']=={'B':':98','C':':99'}[meta['env']]and meta['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[meta['env']],'unowned capture')
   require(meta['source_sha256']==case['sources'],'capture source differs')
   require(end['event']=='preload-file'and end.get('complete')is True and end['markers']==186,'incomplete capture')
   require(hashlib.sha256(cap['preload'].encode()).hexdigest()==end['sha256'],'preload hash differs')
   require(markers(cap['preload'])==case['markers'],'public control differs')
   require(not any(r.get('type')=='error'or r.get('event')in ('native-exception','trace-failed')for r in rows),'faulting trace')
   live=[r for r in rows if 'seq'in r and r['event']!='trace-end'];ends=[r for r in rows if r['event']=='trace-end']
   if mode=='control':require(not live and not ends,'hooked control');continue
   require([r['seq']for r in live]==list(range(len(live)))and len(ends)==1,'lost events')
   require(ends[0]['seq']==len(live)and ends[0]['counts']==dict(Counter(r['event']for r in live))and ends[0]['readOnly']is True and ends[0]['installed']is True,'trace accounting differs')
   require([r['value']for r in live if r['event']=='marker'and r['tick']!=1]==case['markers'],'observer/public differs')
   require(project(rows)==expected,'frozen movement/isolation differs')
   events+=len(live)
 return dict(captures=9,public_markers=1674,native_events=events,motion_rows=775,public_rows=552)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 require(not a.report.exists(),'new report required');spec=json.loads(SPEC.read_text());bundle=json.loads(gzip.decompress(SPEC.with_suffix('.json.gz').read_bytes()))
 require(digest(SPEC.with_suffix('.json.gz'))==spec['bundle_sha256'],'bundle differs')
 evidence=validate(spec,bundle);original_bytes(a.binary,spec['instructions'])
 import verify_wc3_pathing_work242 as engine
 engine.TESTS=TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
 report=dict(passed=True,status='verified',task=spec['task'],**evidence,instructions=len(spec['instructions']),engine=engine.run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(SPEC),binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
