#!/usr/bin/env python3
"""Verify Captain Slow notification, retained virtual-actor requests and full motion."""
import argparse,gzip,hashlib,json,re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-captain320-1.27.json'
TESTS=['wc3_movement.captain320*','wc3_movement.slow318*','wc3_movement.target319*','wc3_save.rejects_prior_save_versions']
SOURCES=['tools/frida/research/captain320'+x for x in ('_capture.py','_make_map.py','_probe.j','_probe.ai','_observer.js','_alert_observer.js','_turn_observer.js')]+['tools/ghidra/research/Captain320Evidence.java','games/warcraft-3/game/tests/fixtures/retail_captain320.h','games/warcraft-3/tests/resources-src/Scripts/captain320.j','games/warcraft-3/tests/resources-src/Scripts/captain320.ai']
COUNTS=dict(motion=861,public=270,shared=215,actor=293)
def require(value,message):
 if not value:raise ValueError(message)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(C320 [^"\r\n]*)" \)',text)
def project(rows):
 actors=[next(x['m']['id']for x in rows if x.get('event')=='physical-radius'and x['m']['pos']==p)for p in ([0x41800000,0x41800000],[0x41980000,0x41800000],[0x41b00000,0x41800000])]
 captain=next(x['m']['id']for x in rows if x.get('event')=='bind'and x['m']['radius']==0)
 result={k:[]for k in COUNTS};coords=[];speeds=[];created=False
 for x in rows:
  e=x.get('event')
  if e=='commit-leave':
   for member in x['g']['members']:
    if not member['m']:continue
    m=member['m']
    if member['id']in actors:result['motion'].append([actors.index(member['id']),*m['clock'],*m['pos'],*m['vel'],m['heading']])
    elif member['id']==captain:result['actor'].append([x['c'],*m['pos'],*m['vel'],m['heading'],m['clock'][0]])
  if e=='public-coordinate':coords.append(x['value'])
  if e=='public-speed':speeds.append(x['value'])
  if e=='marker':
   if 'label=created'in x['value']:created=True
   unit=re.search(r'label=unit u=(\d+)',x['value'])
   if unit and coords and created:
    require(len(coords)==2 and len(speeds)==1,'incomplete public getter')
    result['public'].append([x['tick'],int(unit[1]),x['clock'][0],*coords,*speeds,int(re.search(r'order=(\d+)',x['value'])[1])])
   coords=[];speeds=[]
  if e=='shared-leave'and x['s']:
   p=x['s'];result['shared'].append([x['c'],p['refs'],p['published'],p['next'],p['radius']])
 return result
def header(p):
 return '/* Frozen public Captain Slow shared-owner capture. */\n'+''.join('static uint32_t const captain320_'+k+'_rows[]['+str(len(p[k][0]))+']={\n'+''.join(' {'+','.join(f'0x{x:08x}u'for x in row)+'},\n'for row in p[k])+'};\n'for k in COUNTS)
def validate(spec,bundle,check_sources=True):
 require(spec['version']==1 and spec['task']=='NUM-03.2'and spec['binary_sha256']==SHA,'identity differs')
 require(spec['engine_tests']==TESTS and set(spec['pins'])==set(SOURCES),'scope differs')
 require(len(spec['instructions'])==spec['instruction_count']and spec['instruction_count']==931,'instruction guards differ')
 require(len(bundle)==7 and len(spec['capture_sources'])==7,'repeat/control count differs')
 p=spec['projection'];require({k:len(v)for k,v in p.items()}==COUNTS,'projection scope differs')
 if check_sources:
  for path,h in spec['pins'].items():require(digest(ROOT/path)==h,'source differs: '+path)
  require((ROOT/SOURCES[8]).read_text()==header(p),'literal header differs')
 events=0
 for i,cap in enumerate(bundle):
  rows=cap['rows'];meta,end=rows[0],rows[-1];control=i==6
  require(meta['event']=='metadata'and meta['task']=='NUM-03.2/captain320'and meta['sha256']==SHA and meta['owned']is True and meta['mode']==('control'if control else'observe'),'capture identity differs')
  require(meta['env']in ('B','C')and meta['display']=={'B':':98','C':':99'}[meta['env']]and meta['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[meta['env']],'unowned capture')
  require(meta['source_sha256']==spec['capture_sources'][i],'capture source differs')
  for path,h in spec['pins'].items():
   if Path(path).name in meta['source_sha256']:require(meta['source_sha256'][Path(path).name]==h,'capture/source pin differs')
  require(end['event']=='preload-file'and end.get('complete')is True and end['markers']==368,'incomplete capture')
  require(hashlib.sha256(cap['preload'].encode()).hexdigest()==end['sha256'],'preload hash differs')
  require(markers(cap['preload'])==spec['markers']and len(spec['markers'])==368,'public control differs')
  require(not any(r.get('type')=='error'or r.get('event')in ('native-exception','trace-failed')for r in rows),'faulting trace')
  live=[r for r in rows if 'seq'in r and r['event']!='trace-end'];ends=[r for r in rows if r['event']=='trace-end']
  if control:require(not live and not ends,'hooked control');continue
  require([r['seq']for r in live]==list(range(len(live)))and len(ends)==1,'lost events')
  require(ends[0]['seq']==len(live)and ends[0]['counts']==dict(Counter(r['event']for r in live))and ends[0]['readOnly']is True and ends[0]['installed']is True,'trace accounting differs')
  require([r['value']for r in live if r['event']=='marker'and r['tick']!=1]==spec['markers'],'observer/public differs')
  require(project(rows)==p,'frozen movement differs')
  if i>=2:
   notify=[r for r in rows if r.get('event')=='attack-notify'];begin=[r for r in rows if r.get('event')=='attack-cap-begin']
   require(len(notify)==len(begin)==1,'notification count differs')
   require(notify[0]['packet'][1:]==[0,1,16]and notify[0]['trace'][:4]==['0x69b51a','0x4935e0','0x42819e','0x40a37f'],'notification owner differs')
   require(begin[0]['caller']=='0x493663'and begin[0]['primary'][:2]==[0x40819926,0],'completion input differs')
  if i>=4:
   captains=[r['m']for r in rows if r.get('event')=='bind'and r['m']['radius']==0]
   require(captains and all(m['turn']==0x3eccccce and m['window']==0x3dcccccd for m in captains),'virtual actor angular defaults differ')
  events+=len(live)
 return dict(captures=7,public_markers=2576,native_events=events,motion_rows=861,public_rows=270,shared_rows=215,actor_rows=293)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 require(not a.report.exists(),'new report required');s=json.loads(SPEC.read_text());b=json.loads(gzip.decompress(SPEC.with_suffix('.json.gz').read_bytes()))
 require(digest(SPEC.with_suffix('.json.gz'))==s['bundle_sha256'],'bundle differs');evidence=validate(s,b);original_bytes(a.binary,s['instructions'])
 import verify_wc3_pathing_work242 as engine
 engine.TESTS=TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
 report=dict(passed=True,status='verified',task=s['task'],**evidence,instructions=len(s['instructions']),engine=engine.run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(SPEC),binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=s['limits'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
