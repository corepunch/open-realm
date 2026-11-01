#!/usr/bin/env python3
"""Retail249 preparation ownership: full public stream and scoped native evidence.
No game launch. Pointer roles alone are normalized in the Move physical projection;
raw numeric identities, clocks, flags and float words are retained exactly.
"""
import pathlib,json,re,hashlib,collections,copy,struct
from coupling_instant243_preparation_projection import require,projection,preload,rows,actor
ROOT=pathlib.Path(__file__).resolve().parents[3]
FIXTURE=ROOT/'games/warcraft-3/game/tests/fixtures/retail_couple_instant249_ownership.json'
MAP='RS-TARGET249-preparation-ownership-v1.w3m'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
parse=lambda s:dict(re.findall(r'(\w+)=([^ ]+)',s))
word=lambda s:struct.unpack('<I',struct.pack('<f',float(s)))[0]
def move_projection(a):
 selected=[copy.deepcopy(x) for x in a if x.get('scene')==1 and x['event'] in ['point-task','target-task','point-request','arrival','velocity','range-publication']]
 for x in selected:
  if "unit" in x:x["unit"]={"pointer":x["unit"]["pointer"],**actor(x["unit"])}
 pointers={}
 def norm(x,key=None):
  if isinstance(x,dict):return {k:norm(v,k) for k,v in x.items()}
  if isinstance(x,list):return [norm(v,key) for v in x]
  if isinstance(x,str) and x.startswith('0x'):
   require(key in ['pointer','target','association','bridge','receiver','mover'],'unexpected hex field')
   if int(x,16)==0:return 'null'
   if x not in pointers:pointers[x]='pointer-role-'+str(len(pointers))
   return pointers[x]
  return x
 out=norm(selected)
 # The interrupted point task resumes with identical receiver/destination.
 requests=[x for x in out if x['event']=='point-request'];require(len(requests)==2,'two Move requests')
 require(requests[0]['tick']==500 and requests[1]['tick']==543,'Move resume chronology')
 for k in ['bridge','point','arrival','cantpath','receiver','speed','flag','range','caller']:require(requests[0][k]==requests[1][k],'same retained Move '+k)
 velocities=[x for x in out if x['event']=='velocity'];require(len(velocities)==24,'all Move-scene velocity rows')
 require([x['clock']['counter'] for x in velocities[:3]]==[1358,1359,1387],'spell suspension interval')
 require(all(x['speed']==0 for x in velocities[:2]),'facing has zero speed')
 require(velocities[0]['before']['pose'][2:4]==velocities[1]['after']['pose'][2:4]==velocities[2]['before']['pose'][2:4],'fixed XY until resume')
 require(len({x['before']['mover'] for x in velocities})==1,'same mover')
 return out

def project(capture_dir):
 r=pathlib.Path(capture_dir);f=json.loads((r/'frozen-sources.json').read_text())
 for n,h in f.items():
  if n!='original_dll':require(sha(r/n)==h,'freeze '+n)
 require(all(x['exit']==0 for x in json.loads((r/'runs.json').read_text())),'terminal jobs')
 public=[];prefix=[];native=[];contexts=[];moves=[];counts=[]
 for n in ['observe1','observe2','control1']:
  a=rows(r/(n+'.jsonl'));m=a[0];env=m['env'];require(m['owned'] and env in ['B','C'],'ownership');require(m['display']=={'B':':98','C':':99'}[env] and m['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[env],'environment')
  require(m['sha256']==f['original_dll']=='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236','DLL')
  require(m['source_sha256']=={**{k:f[k] for k in ['itemdrop_capture.py','itemdrop_make_map.py','itemdrop_probe.j','itemdrop_observer.js']},'map':f[MAP]},'source provenance')
  raw=r/(n+'-preload.txt');ms=preload(raw);public.append(ms);require(len(ms)==598 and max(map(len,ms))<=160 and not any('OVERLONG' in x for x in ms),'markers');require(a[-1]=={'event':'preload-file','sha256':sha(raw),'markers':598,'complete':True},'footer')
  require(not any(x.get('event') in ['error','trace-failed','script-error'] or x.get('type')=='error' for x in a),'capture error')
  if n=='control1':require(m['mode']=='control' and all(x['event'] in ['metadata','owned-loading-input','loading-key','control-start-file','preload-file'] for x in a),'unhooked');continue
  ends=[x for x in a if x['event']=='trace-end'];require(len(ends)==1 and ends[0]['installed'],'trace end');c=ends[0]['counts'];require(collections.Counter(x['event'] for x in a if x['event'] in c)==collections.Counter(c),'counts');require(set(x['event'] for x in a)-set(c)=={'metadata','owned-loading-input','loading-key','trace-end','preload-file'},'inventory');counts.append(c)
  hs=[x for x in a if x['event']=='marker'];require([x['value'] for x in hs[-598:]]==ms,'hook/public');prefix.append([x['value'] for x in hs[:-598]])
  native.append(projection(a));moves.append(move_projection(a));require(len(native[-1])==737,'all scoped rows')
  dispatch=[x for x in a if x['event']=='prep-order-dispatch'];require(len(dispatch)==6 and len({tuple(x['actor']['identity']) for x in dispatch})==6,'six distinct sources')
  for scene,x in enumerate(dispatch):require(x['scene']==scene and x['tick']==400*scene+100 and x['actor']['flags']&4 and x['actor']['flags60']&8 and x['caller']==0x60919a,'Town/force/dispatch')
  ctx=[]
  for i,x in enumerate(hs):
   q=parse(x['value'])
   if q['label']!='context':continue
   prev=parse(hs[i-1]['value']);require(prev['label']=='spell','context predecessor')
   ctx.append(dict(scene=int(q['scene']),tick=int(q['tick']),event=int(prev['event']),actor=int(prev['actor']),ability=int(prev['ability']),target=int(q['target']),x_word=word(q['x']),y_word=word(q['y']),clock_word=x['clock']['primary'][0]))
  contexts.append(ctx)
 require(public[0]==public[1]==public[2] and prefix[0]==prefix[1],'whole public repeat/control')
 require(native[0]==native[1] and moves[0]==moves[1] and contexts[0]==contexts[1],'whole scoped repeat')
 require([len([x for x in contexts[0] if x['scene']==s]) for s in range(6)]==[10,10,0,0,10,5],'phase counts')
 require([x['tick'] for x in contexts[0] if x['scene']==1 and x['ability']==0x58696e66]==[502,502,517,543,543],'outer survives nested Move')
 return dict(schema=1,public=public[0],hook_prefix=prefix[0],scoped_native=native[0],move_physical=moves[0],contexts=contexts[0],capture_event_counts=counts[0],probe_jass=(r/'itemdrop_probe.j').read_text(),typed_spell_events={'272':'CHANNEL','273':'CAST','274':'EFFECT','275':'FINISH','276':'ENDCAST'},limitations=['Scene4 transfer attempt returnedfalse; successful transfer unmeasured.','Public source handle retained type/life after RemoveUnit does not prove worldpresence.','Targetrole0 alone is not a null measurement.','737 scoped rows select documented fields; raw captures preserve omitted fields.','Move pointer strings use first-appearance roles; numeric identity/float/clock words are unchanged. Unit+cc retains only measured potential word; adjacent unspecified words are excluded as in243.','No complete movement parity claim.','FourCC values are native integer encoding; engine comparisons must map typed identifiers.'])

def header_text(d):
 def strings(name,items):return 'static char const *const '+name+'[] = {\n'+''.join('    '+json.dumps(s)+',\n' for s in items)+'};\n'
 source=d['probe_jass'].replace('call Preload(','call CouplePreparationObserve(')
 result='/* Retail249; only Preload replaced by observation adapter. */\n#ifndef RETAIL_COUPLE_INSTANT249_OWNERSHIP_H\n#define RETAIL_COUPLE_INSTANT249_OWNERSHIP_H\nstatic char const couple_native_probe249[] =\n'+''.join('    '+json.dumps(s)+'\n' for s in source.splitlines(keepends=True))+';\n'
 result+=strings('couple_native_prefix249',d['hook_prefix'])+strings('couple_native_public249',d['public'])
 result+='/* Native272CHANNEL273CAST274EFFECT275FINISH276ENDCAST; native FourCC. */\ntypedef struct { unsigned scene,tick,event,actor,target; uint32_t ability,x_word,y_word,clock_word; } coupleNativeContext249_t;\nstatic coupleNativeContext249_t const couple_native_context249[] = {\n'
 for c in d['contexts']:result+='{%d,%d,%d,%d,%d,0x%08xu,0x%08xu,0x%08xu,0x%08xu},\n'%(c['scene'],c['tick'],c['event'],c['actor'],c['target'],c['ability'],c['x_word'],c['y_word'],c['clock_word'])
 return result+'};\n#endif\n'
def compare(a,b):require(a==b,'complete ordered projection')
def verify_header(t,d):require(t==header_text(d),'semantic generated header')
def mutation_checks(d):
 checks=[]
 for key in ['public','contexts','scoped_native','move_physical']:
  for kind in ['missing','extra','reordered','field']:
   x=copy.deepcopy(d);a=x[key]
   if kind=='missing':a.pop()
   elif kind=='extra':a.append(copy.deepcopy(a[-1]))
   elif kind=='reordered':a[0],a[1]=a[1],a[0]
   elif key=='public':a[0]+=' MUTATION'
   else:a[0]['tick']+=1
   try:compare(x,d)
   except ValueError:checks.append(key+':'+kind)
   else:raise ValueError('accepted mutation')
 t=header_text(d);one=json.dumps(d['public'][0])+',';two=json.dumps(d['public'][1])+','
 for i,v in enumerate([t.replace(one,'',1),t.replace(one,one+one,1),t.replace(one,'SWAP',1).replace(two,one,1).replace('SWAP',two,1),t.replace('0x41496d61u','0x41496d60u',1)]):
  # Native AIma alias in this map is AIm1, use a guaranteed typed word mutation.
  if i==3:v=t.replace('0x%08xu'%d['contexts'][0]['ability'],'0x00000000u',1)
  try:verify_header(v,d)
  except ValueError:checks.append('header:'+str(i))
  else:raise ValueError('accepted header mutation')
 return checks

def verify(capture_dir,fixture=FIXTURE):
 p=json.loads(fixture.with_suffix('.provenance.json').read_text());require(sha(fixture)==p['projection_sha256'],'fixture pin')
 for n,h in p['capture_files_sha256'].items():require(sha(pathlib.Path(capture_dir)/n)==h,'capture pin '+n)
 d=project(capture_dir);compare(d,json.loads(fixture.read_text()));h=fixture.with_suffix('.h');require(sha(h)==p['header_sha256'],'header pin');verify_header(h.read_text(),d);require(len(mutation_checks(d))==20,'negative controls');return d
if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture-dir',type=pathlib.Path,required=True);p.add_argument('--fixture',type=pathlib.Path,default=FIXTURE);p.add_argument('--check',action='store_true');a=p.parse_args();d=verify(a.capture_dir,a.fixture);print('PASS249:598public/737scoped/%d contexts/%d Move physical rows;20 negative controls'%(len(d['contexts']),len(d['move_physical'])))
