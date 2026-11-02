#!/usr/bin/env python3
"""Measured native258 owner RNG startup and all separation RNG/phase inputs."""
import pathlib,json,hashlib,copy
import coupling_instant257_separation_projection as sep
from coupling_instant243_preparation_projection import require
ROOT=pathlib.Path(__file__).resolve().parents[3]
FIXTURE=ROOT/'games/warcraft-3/game/tests/fixtures/retail_couple_instant258_rng.json'
sha=sep.sha
KINDS={'rng-seed':0,'rng-owner-draw':1,'rng-direction':2,'sep-update':3,'sep-apply':4}
def rng_rows(a):
 out=[]
 for x in a:
  if x['event'] in ['rng-owner-draw','rng-direction']:out.append(x)
  elif x['event']=='rng-seed' and x['caller']==0x29ec20:out.append({k:v for k,v in x.items() if k!='pointer'})
  elif x['event'] in ['sep-update','sep-apply']:out.append({k:x[k] for k in ['event','tick','scene','clock']}|{'before':x['before']['rng'],'after':x['after']['rng']})
 return out
def semantics(d):
 a=d['rng'];require(len(a)==1390,'complete RNG projection');seed=[x for x in a if x['event']=='rng-seed'];require(len(seed)==1 and seed[0]['seed']==0x77617233,'actual owner seed')
 draws=[x for x in a if x['event']=='rng-owner-draw' and x['tick']==-1];require(len(draws)==12,'complete startup draws');state=seed[0]['after']
 for x in draws:require(x['before']==state and x['result']==x['after'][0],'startup chain');state=x['after']
 require(state==[3057455189,3092019416],'actual post-startup state')
 first=next(x for x in a if x['event']=='sep-update');require(first['before']=={'state':state,'phase':0} and first['clock']['counter']==1026,'first post-toggle visit')
 directions=[x for x in a if x['event']=='rng-direction'];require(len(directions)==2 and directions[0]['before']==state,'actual overlap draw inputs')
 require([x['clock']['counter'] for x in directions]==[1539,2871],'overlap clock order')
 require(d['initial']==dict(seed=seed[0]['seed'],startup_draws=len(draws),state=state,constructor_phase=0,first_visit_counter=1026,first_visit_posttoggle_phase=0),'derived initial facts')
def project(r):
 r=pathlib.Path(r);b=sep.project(r);original=json.loads(sep.FIXTURE.read_text())
 for k in ['public','scoped_native','separation']:require(b[k]==original[k],'unchanged257 '+k)
 a=[rng_rows(sep.base.rows(r/(n+'.jsonl'))) for n in ['observe1','observe2']];require(a[0]==a[1],'complete RNG repeat')
 ctor=json.loads((r/'owner-constructor-static.json').read_text());phase=json.loads((r/'owner-phase-static.json').read_text())
 require('separation_phase = 0' in json.dumps(ctor),'original constructor phase assignment')
 d=dict(schema=1,baseline257_sha256=sha(sep.FIXTURE),public=b['public'],scoped_native=b['scoped_native'],separation=b['separation'],rng=a[0],initial=dict(seed=0x77617233,startup_draws=12,state=[3057455189,3092019416],constructor_phase=0,first_visit_counter=1026,first_visit_posttoggle_phase=0),static_abi=json.loads((r/'abi-static.json').read_text()),owner_constructor=ctor,owner_phase_update=phase,limits=['Only actual owner seed caller29ec20 selected; other per-unit seeds remain raw.','All seed/draw/direction/state fields read during actual native execution; no guessed poses or injected state.','First visit phase is AFTER15aa80 toggles; constructor phase0 is independently established by original157610 assignment.','Retains257 supplemental query and opaque-prefix limits; no full renderer query oracle.'],header_schema=dict(kinds=KINDS,words='seed:seed,after2,caller; draw:before2,after2,result,caller,counter,primary4; direction:before2,after2,direction2,caller,counter,primary4; separation:counter,primary4,beforestate2,beforephase,afterstate2,afterphase'))
 semantics(d);return d
def packed(d):
 out=[]
 for x in d['rng']:
  e=x['event']
  if e=='rng-seed':w=[x['seed']]+x['after']+[x['caller']]
  elif e=='rng-owner-draw':w=x['before']+x['after']+[x['result'],x['caller'],x['clock']['counter']]+x['clock']['primary']
  elif e=='rng-direction':w=x['before']+x['after']+x['direction']+[x['caller'],x['clock']['counter']]+x['clock']['primary']
  else:w=[x['clock']['counter']]+x['clock']['primary']+x['before']['state']+[x['before']['phase']]+x['after']['state']+[x['after']['phase']]
  require(len(w)<=16,'word bound');out.append((KINDS[e],x['scene'],x['tick'],w))
 return out
def header_text(d):
 i=d['initial'];out='/* Native258 measured owner inputs; generated, no pose injection. */\n#ifndef RETAIL_COUPLE_INSTANT258_RNG_H\n#define RETAIL_COUPLE_INSTANT258_RNG_H\n'
 out+='static uint32_t const couple_native_owner_seed258=0x%08xu;\n'%i['seed']
 out+='enum { couple_native_startup_draws258=%d, couple_native_constructor_phase258=%d, couple_native_first_visit_counter258=%d, couple_native_first_visit_phase258=%d };\n'%(i['startup_draws'],i['constructor_phase'],i['first_visit_counter'],i['first_visit_posttoggle_phase'])
 out+='static uint32_t const couple_native_initial_rng258[2]={%uu,%uu};\n'%tuple(i['state'])
 out+='typedef struct { unsigned kind; int scene,tick; unsigned count; uint32_t words[16]; } coupleNativeRng258_t;\nstatic coupleNativeRng258_t const couple_native_rng258[] = {\n'
 for kind,scene,tick,w in packed(d):out+='{%d,%d,%d,%d,{%s}},\n'%(kind,scene,tick,len(w),','.join('0x%08xu'%v for v in w))
 return out+'};\n#endif\n'
def compare(a,b):require(a==b,'whole ordered projection')
def verify_header(text,d):require(text==header_text(d),'semantic generated header')
def mutation_checks(d):
 done=[]
 for key in ['public','scoped_native','separation','rng']:
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
 for key in ['seed','startup_draws','constructor_phase','first_visit_counter']:
  x=copy.deepcopy(d);x['initial'][key]+=1
  try:semantics(x)
  except ValueError:done.append('semantic:'+key)
  else:raise ValueError('accepted semantic mutation')
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
def verify(r,fixture=FIXTURE,header=None):
 p=json.loads(fixture.with_suffix('.provenance.json').read_text())
 for n,h in p['capture_files_sha256'].items():require(sha(pathlib.Path(r)/n)==h,'capture pin '+n)
 header=header or fixture.with_suffix('.h');require(sha(fixture)==p['projection_sha256'],'fixture pin');require(sha(header)==p['header_sha256'],'header pin')
 d=project(r);compare(d,json.loads(fixture.read_text()));verify_header(header.read_text(),d);require(len(mutation_checks(d))==24,'negative controls');return d
if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture-dir',type=pathlib.Path,required=True);p.add_argument('--fixture',type=pathlib.Path,default=FIXTURE);p.add_argument('--header',type=pathlib.Path);p.add_argument('--check',action='store_true');a=p.parse_args();verify(a.capture_dir,a.fixture,a.header);print('PASS258:478public/727baseline/1397separation/1390RNG;24negative controls')
