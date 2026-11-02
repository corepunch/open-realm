#!/usr/bin/env python3
"""Verify captured effect-completion inputs and physical stepping through Stop."""
import argparse,gzip,hashlib,json,re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
from verify_wc3_pathing_modifiers316 import projection as factor_projection,markers,require,digest
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-modifiers317-1.27.json'
TESTS=['wc3_movement.modifiers317*','wc3_movement.modifiers316*','wc3_movement.temporary_modifiers_match_original_velocity_and_saved_restoration']
SOURCES=['tools/frida/research/modifiers317_'+x for x in ('capture.py','make_map.py','observer.js')]+['tools/frida/research/modifiers316_probe.j','tools/ghidra/research/Modifiers317Evidence.java']+['games/warcraft-3/game/tests/fixtures/retail_modifiers317_'+x+'.h'for x in ('qnan','finite')]
def project(r):
 actor=next(x['m']['id']for x in r if x.get('event')=='physical-radius'and x['m']['pos']==[0x41c00000,0x41800000])
 factors={};pending=None
 for x in r:
  if x.get("event")=="factor-enter":pending=x["seq"]
  if x.get("event")=="factor-leave":
   require(pending is not None,"unpaired factor leave");factors[pending]=x["owner"]["factor"];pending=None
 require(pending is None,"missing factor leave")
 rows=[];coords=[];speed=0;factor_index=0
 for x in r:
  e=x.get('event');tick=x.get('tick',0)
  if not 20<=tick<=65:continue
  head=[0,x.get('counter',0),*x.get('primary',[0,0])[:2]]
  if e=='factor-enter':
   head[0]=7;ability=0 if factor_index in (0,3)else 1;remove=factor_index>=2;factor_index+=1
   rows.append(head+[x['delta'],ability,int(remove),factors[x['seq']]]+[0]*4)
  if e=='commit-leave':
   for member in x['g']['members']:
    if member['id']==actor and member['m']:
     m=member['m'];head[0]=6;rows.append(head+[*m['pos'],*m['vel'],m['heading'],m['maximum'],m['range'],member['requested']])
  if e=='public-coordinate':coords.append(x['value'])
  if e=='public-speed':speed=x['value']
  if e=='marker':
   label=re.search(r'label=(\S+)',x['value'])[1]
   if label=='begin-setup':coords=[];continue
   if len(coords)!=2:raise ValueError((label,coords))
   head[0]={'created':0,'set-speed':1,'move':2,'restore-stop':3}.get(label,5)
   rows.append(head+coords+[speed,0,int(re.search(r'order=(\d+)',x['value'])[1])]+[0]*3);coords=[]
 require(factor_index==4,"completion inputs differ")
 return rows

def normalized(rows):
 offset=rows[0][1]
 return [[r[0],r[1]-offset,*r[2:]]for r in rows]
def header(name,rows):
 return '/* Literal retail motion and public queries through Stop; captured spell completion inputs. */\nstatic uint32_t const modifiers317_'+name+'_rows[][12]={\n'+''.join(' {'+','.join(f'0x{v:08x}u'for v in row)+'},\n'for row in rows)+'};\n'
def validate(spec,bundle,check_sources=True):
 require(spec['version']==1 and spec['task']=='NUM-03.2'and spec['binary_sha256']==SHA,'identity differs')
 require(set(spec['pins'])==set(SOURCES),'source inventory differs')
 require(set(spec['cases'])==set(bundle)=={'qnan','finite'},'scenes differ')
 require(spec['engine_tests']==TESTS,'engine scope differs')
 require(len(spec['instructions'])==172,'instruction guards differ')
 if check_sources:
  for path,h in spec['pins'].items():require(digest(ROOT/path)==h,'source differs: '+path)
 total=0
 for name,captures in bundle.items():
  case=spec['cases'][name];require(len(captures)==3,'missing repeat/control')
  require(len(case['states'])==133 and sum(r[0]==6 for r in case['states'])==83,'motion scope differs')
  if check_sources:require((ROOT/case['header']).read_text()==header(name,case['states']),'literal header differs')
  for i,cap in enumerate(captures):
   rows=cap['rows'];meta,end=rows[0],rows[-1];mode='observe'if i<2 else'control'
   require(meta['event']=='metadata'and meta['task']=='NUM-03.2/modifiers317'and meta['sha256']==SHA and meta['mode']==mode and meta['owned']is True,'capture identity differs')
   require(meta['env']in ('B','C')and meta['display']=={'B':':98','C':':99'}[meta['env']]and meta['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[meta['env']],'unowned capture')
   require(meta['source_sha256']==case['capture_sources'],'capture sources differ')
   require(end['event']=='preload-file'and end.get('complete')is True and end['markers']==102,'incomplete capture')
   require(hashlib.sha256(cap['preload'].encode()).hexdigest()==end['sha256'],'preload hash differs')
   require(markers(cap['preload'])==case['markers'],'control markers differ')
   require(not any(r.get('type')=='error'or r.get('event')in ('native-exception','trace-failed')for r in rows),'faulting capture')
   live=[r for r in rows if 'seq'in r and r['event']!='trace-end'];ends=[r for r in rows if r['event']=='trace-end']
   if mode=='control':require(not live and not ends,'hooked control');continue
   require([r['seq']for r in live]==list(range(len(live)))and len(ends)==1,'lost events')
   require(ends[0]['seq']==len(live)and ends[0]['counts']==dict(Counter(r['event']for r in live))and ends[0]['readOnly']is True and ends[0]['installed']is True,'trace accounting differs')
   require([r['value']for r in live if r['event']=='marker'and r['tick']!=1]==case['markers'],'observer/public markers differ')
   require(factor_projection(rows)==case['factors'],'factor stream differs')
   require(normalized(project(rows))==normalized(case['states']),'motion/public stream differs')
   total+=len(live)
 return dict(captures=6,public_markers=612,native_events=total,motion_rows=166,states=266)
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
