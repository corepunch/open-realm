#!/usr/bin/env python3
"""Native243 scoped preparation evidence; no numeric movement parity claim.
Only explicitly selected actor/ability fields are projected. Raw identity pairs,
clocks, numeric flags and order words remain unchanged; no pointer normalization.
The control's unused observer differs, while gameplay probe/map bytes agree.
"""
import json,re,hashlib,pathlib,copy
ROOT=pathlib.Path(__file__).resolve().parents[3]
FIXTURE=ROOT/'games/warcraft-3/game/tests/fixtures/retail_couple_instant243_preparation.json'
MAP='be64fb424519a81e004ac8f9590f611928e63f58142365792de9c17dbb47b8ea'
def rows(p):return [json.loads(x) for x in p.read_text().splitlines()]
def preload(p):return re.findall(r'call Preload\( "(CPF .*?)" \)',p.read_text())
def actor(u):return {k:u[k] for k in ['identity','rawcode','flags','flags60','head','task']}|{'potential_word':u['potential_cc'][0]}
def projection(a):
 out=[]
 for x in a:
  e=x['event'];y={k:x[k] for k in ['event','tick','scene','clock'] if k in x}
  if e=='marker':y['value']=x['value']
  elif e in ['instant-producer-enter','instant-producer-leave']:
   b=x['ability'];y.update(actor=actor(b['actor']),alias=b['alias'],counters=b['counters'],flags=b['flags'],rank=b['rank'],retained=b['retained'])
  elif e in ['prep-enter','prep-leave','prep-order-dispatch']:
   y['actor']=actor(x['actor']);y.update({k:x[k] for k in ['alias','caller','result'] if k in x})
  elif e=='target-task':y.update(actor=actor(x['unit']),code=x['code'],range=x['range'],caller=x['caller'])
  elif e in ['outer-availability-return','availability-gate-return']:y['result']=x['result']
  else:continue
  out.append(y)
 return out

def audit(a):
 assert a[0]['event']=='metadata' and a[0]['source_sha256']['map']==MAP
 assert any(x['event']=='trace-end' for x in a) and not any(x['event']=='trace-failed' for x in a)
 prod=[x for x in a if x['event']=='instant-producer-enter'];assert len(prod)==2
 ordinary,nested=prod;assert ordinary['scene']==0 and nested['scene']==1
 u=nested['ability']['actor'];assert u['flags60']&8 and u['flags']&4 and u['potential_cc'][0]==0x43160000
 assert ordinary['ability']['actor']['flags60']&8==0
 assert nested['ability']['alias']==0x41636f33
 assert any(x['event']=='target-task' and x['scene']==0 and x['range']==0 for x in a)
 assert any(x['event']=='arrival' and x['scene']==0 for x in a)
 assert not any(x['event'] in ['target-task','point-task','arrival','velocity'] and x['scene']==1 for x in a)
 dispatch=[x for x in a if x['event']=='prep-order-dispatch'];assert len(dispatch)==1
 assert dispatch[0]['actor']['identity']==u['identity'] and dispatch[0]['actor']['flags60']&8 and dispatch[0]['caller']==0x60919a
 enter=next(x for x in a if x['event']=='prep-enter' and x['alias']==0x58696e66)
 assert enter['actor']['identity']==u['identity'] and not enter['actor']['flags60']&8
 after=[x for x in a if x['event']=='prep-leave' and x['scene']==1][-1]
 assert after['actor']['identity']==u['identity'] and after['result']==1 and after['actor']['flags60']==0
 assert a.index(enter)<a.index(dispatch[0])<a.index(nested)<a.index(after)
 markers=[x['value'] for x in a if x['event']=='marker']
 phases=[x for x in markers if 'tick=500 ' in x and 'label=spell ' in x and 'ability=1097035571' in x]
 assert [int(re.search(r'event=(\d+)',x)[1]) for x in phases]==[272,275,276]
 assert any('label=nested-return accepted=1' in x for x in markers)
 assert any('label=outer-return accepted=1' in x for x in markers)
 assert markers[-2].find('label=complete')>=0 and markers[-1].endswith('mana=0.000 max=200.000 charges=1')
 return projection(a)


def require(ok,message):
 if not ok:raise ValueError(message)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def project(capture_dir):
 r=pathlib.Path(capture_dir);frozen=json.loads((r/'frozen-sources.json').read_text())
 for n,h in frozen.items():require(sha(r/n)==h,'source freeze '+n)
 public=[];native=[];hooks=[];counts=[]
 for name in ['observe1','observe2','../control-diagnostic']:
  a=rows(r/(name+'.jsonl'));meta=a[0];env=meta['env']
  require(meta['owned'] and env in ['B','C'],'owned')
  require(meta['display']=={'B':':98','C':':99'}[env] and meta['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[env],'environment')
  require(meta['sha256']=='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236','DLL')
  for key in ['itemdrop_capture.py','itemdrop_make_map.py','itemdrop_probe.j']:
   require(meta['source_sha256'][key]==frozen[key],'source '+key)
  require(meta['source_sha256']['map']==MAP,'map')
  raw=r/(name+'-preload.txt');markers=preload(raw);public.append(markers)
  require(len(markers)==92 and max(map(len,markers))<=160,'bounded public markers')
  require(a[-1]=={'event':'preload-file','sha256':sha(raw),'markers':92,'complete':True},'footer')
  require(not any(x.get('event') in ['trace-failed','error','script-error'] or x.get('type')=='error' for x in a),'capture errors')
  if name.startswith('../'):
   require(meta['mode']=='control' and all(x['event'] in {'metadata','owned-loading-input','loading-key','preload-file','control-start-file'} for x in a),'unhooked control')
   require('exit=0' in (r/(name+'.log')).read_text(),'control exit');continue
  require(meta['source_sha256']['itemdrop_observer.js']==frozen['itemdrop_observer.js'],'observer')
  ends=[x for x in a if x['event']=='trace-end'];require(len(ends)==1 and ends[0]['installed'],'trace end')
  import collections
  c=ends[0]['counts'];require(collections.Counter(x['event'] for x in a if x['event'] in c)==collections.Counter(c),'trace counts')
  require(set(x['event'] for x in a)-set(c)=={'metadata','owned-loading-input','loading-key','trace-end','preload-file'},'event inventory')
  h=[x['value'] for x in a if x['event']=='marker'];require(len(h)==95 and h[3:]==markers,'hook/public stream');hooks.append(h)
  ordinary=next(x for x in a if x['event']=='range-publication' and x['tick']==100)['before']['mover']
  arrivals=[x for x in a if x['event']=='arrival'];owned=[x for x in arrivals if x['before']['mover']==ordinary];background=[x for x in arrivals if x['before']['mover']!=ordinary]
  require(len(owned)==21 and len(background)==1 and background[0]['tick']==1 and background[0]['source']==[1111490560,1107296256],'poll actor attribution')
  native.append(audit(a));counts.append(c)
 require(public[0]==public[1]==public[2] and hooks[0]==hooks[1],'complete public repeat/control')
 require(native[0]==native[1] and len(native[0])==131,'scoped native repeat')
 return {'schema':1,'poll_attribution':{'raw_arrival_count':22,'ordinary_acoi_arrivals':21,'background_town_ai_arrivals':1,'scope':'Attribution only, not a per-poll numerical oracle'},'public':public[0],'hook_prefix':hooks[0][:3],'scoped_native':native[0],'capture_event_counts':counts[0],'probe_jass':(r/'itemdrop_probe.j').read_text(),'ai_source':(r/'probe.ai').read_text(),'typed_spell_events':{'272':'CHANNEL','273':'CAST','274':'EFFECT','275':'FINISH','276':'ENDCAST'},'limitations':['Only 131 selected native records are an equality oracle; no full numeric poll oracle.','The failed inline observer is excluded; no isolated cause is assigned.','Mana remains zero and potion charges remain one; no successful restoration/consumption claim.','Native spell event IDs 272..276 require typed role mapping; FourCC fields require typed conversion only.','This capture does not record GetSpellTargetUnit or spell target X/Y.']}

def header_text(d):
 def array(name,lines):return 'static char const *const '+name+'[] = {\n'+''.join('    '+json.dumps(s)+',\n' for s in lines)+'};\n'
 source=d['probe_jass'].replace('call Preload(', 'call CouplePreparationObserve(')
 return ('/* Generated from native243. Only Preload is replaced by the observation adapter. */\n'
 '#ifndef RETAIL_COUPLE_INSTANT243_PREPARATION_H\n#define RETAIL_COUPLE_INSTANT243_PREPARATION_H\n'
 '/* Typed native IDs: 272 CHANNEL, 273 CAST, 274 EFFECT, 275 FINISH, 276 ENDCAST. */\n'
 'static char const couple_native_probe243[] =\n'+''.join('    '+json.dumps(s)+'\n' for s in source.splitlines(keepends=True))+';\n'
 +array('couple_native_prefix243',d['hook_prefix'])+array('couple_native_public243',d['public'])+'#endif\n')

def verify_header(text,d):require(text==header_text(d),'semantic generated header including source and all markers')
def compare_projection(actual,expected):require(actual==expected,'complete ordered projection')

def mutations(d):
 verify_header(header_text(d),d);checked=[]
 for key in ['public','scoped_native']:
  for kind in ['missing','extra','reordered','field']:
   x=copy.deepcopy(d);a=x[key]
   if kind=='missing':a.pop()
   elif kind=='extra':a.append(copy.deepcopy(a[-1]))
   elif kind=='reordered':a[0],a[1]=a[1],a[0]
   elif key=='public':a[0]+=' MUTATED'
   else:a[0]['tick']+=1
   try:compare_projection(x,d)
   except ValueError:checked.append(key+':'+kind)
   else:raise ValueError('accepted projection mutation '+key+':'+kind)
 return checked

def header_mutations(d):
 text=header_text(d);marker=json.dumps(d['public'][0])+',';second=json.dumps(d['public'][1])+','
 variants=[text.replace(marker,'',1),text.replace(marker,marker+marker,1),text.replace(marker,'SWAP',1).replace(second,marker,1).replace('SWAP',second,1),text.replace('852507','852508',1)]
 for mutated in variants:
  try:verify_header(mutated,d)
  except ValueError:pass
  else:raise ValueError('accepted header mutation')
 return 4

def semantic_mutations(capture_dir):
 a=rows(pathlib.Path(capture_dir)/'observe1.jsonl');checked=[]
 for name in ['bit8','dispatch_identity','task','poll','restore','caller','town','ordinary']:
  b=copy.deepcopy(a);nested=next(x for x in b if x['event']=='instant-producer-enter' and x['scene']==1);dispatch=next(x for x in b if x['event']=='prep-order-dispatch')
  if name=='bit8':nested['ability']['actor']['flags60']&=~8
  elif name=='dispatch_identity':dispatch['actor']['identity'][0]+=1
  elif name in ['task','poll']:b.insert(-2,{'event':'target-task' if name=='task' else 'arrival','scene':1})
  elif name=='restore':[x for x in b if x['event']=='prep-leave' and x['scene']==1][-1]['actor']['flags60']=8
  elif name=='caller':dispatch['caller']+=1
  elif name=='town':nested['ability']['actor']['flags']&=~4
  else:next(x for x in b if x['event']=='instant-producer-enter')['ability']['actor']['flags60']|=8
  try:audit(b)
  except (AssertionError,ValueError):checked.append(name)
  else:raise ValueError('accepted semantic mutation '+name)
 return checked

def verify(capture_dir,fixture=FIXTURE):
 p=json.loads(fixture.with_suffix('.provenance.json').read_text());require(sha(fixture)==p['projection_sha256'],'projection pin')
 for n,h in p['capture_files_sha256'].items():require(sha(pathlib.Path(capture_dir)/n)==h,'capture pin '+n)
 d=project(capture_dir);compare_projection(d,json.loads(fixture.read_text()))
 h=fixture.with_suffix('.h');require(sha(h)==p['header_sha256'],'header pin');verify_header(h.read_text(),d)
 require(len(mutations(d))==8 and len(semantic_mutations(capture_dir))==8 and header_mutations(d)==4,'negative controls')
 return d
if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture-dir',type=pathlib.Path,required=True);p.add_argument('--fixture',type=pathlib.Path,default=FIXTURE);p.add_argument('--check',action='store_true');a=p.parse_args();verify(a.capture_dir,a.fixture)
 print('PASS: 92 exact completed markers + 3 setup markers, 131 scoped native rows, repeat/control, generated probe/header, 8 semantic controls.')
