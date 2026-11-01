#!/usr/bin/env python3
"""Strict retained-separation projection for native257; no inferred RNG state."""
import pathlib,json,hashlib,copy
import coupling_instant251_cantpath_projection as base
from coupling_instant243_preparation_projection import require
ROOT=pathlib.Path(__file__).resolve().parents[3]
FIXTURE=ROOT/'games/warcraft-3/game/tests/fixtures/retail_couple_instant257_separation.json'
sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
KINDS={'sep-update':0,'sep-apply':1,'sep-eligibility':2}
def unit(u):
 return None if u is None else {k:v for k,v in u.items() if k not in ['pointer','potential_cc']}
def state(s):
 return dict(moverIdentity=s['moverIdentity'],mover={k:v for k,v in s['mover'].items() if k!='mover'},unit=unit(s['unit']),vector=s['prefix'][6:8],policy=s['prefix'][8])
def core(a):
 out=[]
 for x in a:
  e=x['event']
  if e not in KINDS:continue
  y={k:v for k,v in x.items() if k in ['event','tick','scene','clock','leaveClock','result']}
  if e=='sep-eligibility':y['before']=unit(x['before'])
  else:y.update(before=state(x['before']),after=state(x['after']))
  out.append(y)
 return out
UNIT_FIELDS=['identity','rawcode','flags','flags60','head','task','widget20','suspend54','depth198','moverIdentity']
MOVER_FIELDS=['pose','heading','radius','range','flags','group','request']
def flatten_fields(d,fields):
 out=[]
 for k in fields:
  v=d[k];out.extend(v if isinstance(v,list) else [v])
 return [int(v)&0xffffffff for v in out]
def unit_words(u):return [int(u is not None)]+(flatten_fields(u,UNIT_FIELDS) if u else [0]*14)
def state_words(s):return s['moverIdentity']+flatten_fields(s['mover'],MOVER_FIELDS)+unit_words(s['unit'])+s['vector']+[s['policy']]
def packed(d):
 out=[]
 for x in d['separation']:
  words=[x['clock']['counter']]+x['clock']['primary']
  if x['event']=='sep-eligibility':words+=unit_words(x['before'])+[x['result']]
  else:words+=state_words(x['before'])+state_words(x['after'])+[x['leaveClock']['counter']]+x['leaveClock']['primary']
  require(len(words)<=96,'header word bound');out.append([KINDS[x['event']],x['scene'],x['tick'],words])
 return out
def semantics(d):
 a=d['separation'];require(len(a)==1397,'complete core');dead=[]
 for x in a:
  if x['event']!='sep-apply':continue
  b=x['before'];u=b['unit']
  if u and u['rawcode']==0x65686970 and u['flags']&0x100 and b['mover']['pose'][2:4]!=x['after']['mover']['pose'][2:4]:
   require(b['mover']['group']==[0xffffffff]*2,'dead mover no physical group')
   require(b['mover']['pose'][4:6]==[0,0],'dead mover no residual velocity');dead.append(x)
 require(len(dead)==87 and {x['scene'] for x in dead}=={0,1,2},'complete dead displacement witnesses')
def project(r):
 r=pathlib.Path(r);b=base.project(r);original=json.loads(base.FIXTURE.read_text())
 for k in ['public','hook_prefix','scoped_native','typed_events','probe_jass','map_metadata','profile']:require(b[k]==original[k],'unchanged251 '+k)
 a=[core(base.rows(r/(n+'.jsonl'))) for n in ['observe1','observe2']];require(a[0]==a[1],'exact core repeat')
 d=dict(schema=1,baseline251_sha256=sha(base.FIXTURE),public=b['public'],scoped_native=b['scoped_native'],separation=a[0],static_abi=json.loads((r/'abi-static.json').read_text()),limits=['1397 ordered core rows only; supplementary058900 queries include renderer/internal reads and differ5138/5121, retained raw but excluded.','Typed pointer keys and opaque potential_cc words excluded; native canonical identities and remaining numeric fields unchanged.','Opaque separation prefix0..4 retained raw only; prefix5 is mover pointer; only documented vector6/7 and policy8 projected.','No RNG state captured by257; do not infer seed from displacement.','Header preserves native identity/flag encodings; engine comparison must explicitly map roles rather than assume numeric identity equality.'],header_schema=dict(kinds=KINDS,unit_fields=UNIT_FIELDS,mover_fields=MOVER_FIELDS,format='clock counter+primary4; eligibility:unitpresent+unitfields+result; update/apply:beforestate+afterstate+leaveclock5; state:moverIdentity2+moverfields+unitpresent+unitfields+vector2+policy'))
 semantics(d);return d
def header_text(d):
 out='/* Generated native257 complete ordered core stream; schema in companion JSON. */\n#ifndef RETAIL_COUPLE_INSTANT257_SEPARATION_H\n#define RETAIL_COUPLE_INSTANT257_SEPARATION_H\ntypedef struct { unsigned kind,scene,tick,count; uint32_t words[96]; } coupleNativeSeparation257_t;\nstatic coupleNativeSeparation257_t const couple_native_separation257[] = {\n'
 for kind,scene,tick,w in packed(d):out+='{%d,%d,%d,%d,{%s}},\n'%(kind,scene,tick,len(w),','.join('0x%08xu'%v for v in w))
 return out+'};\n#endif\n'
def compare(a,b):require(a==b,'whole ordered projection')
def verify_header(text,d):require(text==header_text(d),'semantic generated header')
def mutation_checks(d):
 done=[]
 for key in ['public','scoped_native','separation']:
  for mode in ['missing','extra','reordered','field']:
   x=copy.deepcopy(d);a=x[key]
   if mode=='missing':a.pop()
   elif mode=='extra':a.append(copy.deepcopy(a[-1]))
   elif mode=='reordered':a[0],a[1]=a[1],a[0]
   elif key=='public':a[0]+=' MUTATION'
   else:a[0]['tick']+=1
   try:compare(x,d)
   except ValueError:done.append(key+':'+mode)
   else:raise ValueError('accepted mutation')
 text=header_text(d);lines=text.splitlines(keepends=True);idx=next(i for i,s in enumerate(lines) if s.startswith('{'))
 for mode in ['missing','extra','reordered','field']:
  a=lines[:]
  if mode=='missing':a.pop(idx)
  elif mode=='extra':a.insert(idx,a[idx])
  elif mode=='reordered':a[idx],a[idx+1]=a[idx+1],a[idx]
  else:a[idx]=a[idx].replace('0x','0y',1)
  try:verify_header(''.join(a),d)
  except ValueError:done.append('header:'+mode)
  else:raise ValueError('accepted header mutation')
 return done
def verify(r,fixture=FIXTURE):
 p=json.loads(fixture.with_suffix('.provenance.json').read_text())
 for n,h in p['capture_files_sha256'].items():require(sha(pathlib.Path(r)/n)==h,'capture pin '+n)
 require(sha(fixture)==p['projection_sha256'],'projection pin');require(sha(fixture.with_suffix('.h'))==p['header_sha256'],'header pin')
 d=project(r);compare(d,json.loads(fixture.read_text()));verify_header(fixture.with_suffix('.h').read_text(),d);require(len(mutation_checks(d))==16,'negative controls');return d
if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture-dir',type=pathlib.Path,required=True);p.add_argument('--fixture',type=pathlib.Path,default=FIXTURE);p.add_argument('--check',action='store_true');a=p.parse_args();verify(a.capture_dir,a.fixture);print('PASS257:478public/727baseline/1397separation/87dead displacement commits;16negative controls')
