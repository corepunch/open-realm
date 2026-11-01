#!/usr/bin/env python3
"""Derive the full native299 C replay header from the pinned portable evidence.

No retail installation or original DLL is needed. Output is written only to the
explicit --out directory; the repository fixture is never overwritten implicitly.
"""
import argparse,importlib.util,json,re,hashlib,collections
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[3]);ap.add_argument('--out',type=Path,required=True);a=ap.parse_args()
s=importlib.util.spec_from_file_location('evidence',a.repo/'tools/ghidra/research/attack_recovery299_evidence.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
files,spec=m.load();audit=m.audit(files,spec)
def project(name):
 rows=[json.loads(x) for x in files[name+'.jsonl'].splitlines()]
 motion=[r for r in rows if r.get('event') in ('arrival','velocity')]
 physical={(r['scene'],r['before']['mover']) for r in motion}; roles={}
 for r in rows:
  if r.get('event')!='role-binding':continue
  matches=[v for v in r['resolutions'] if (r['scene'],v['payload']) in physical]
  for v in matches:
   k=(r['scene'],v['payload']);value=r['role']
   assert k not in roles or roles[k]==value,(k,roles.get(k),value)
   roles[k]=value
 assert physical==set(roles),(physical-set(roles))
 out=[]
 for r in motion:
  role=roles[r['scene'],r['before']['mover']]
  if r['event']=='arrival':
   phase=0;words=[r['clock']['primary'][0],*r['source'],*r['destination'],r['heading'],r['threshold'],r['angle'],r['inside'],r['result']]
  else:
   phase=2;words=[r['clock']['primary'][0],*r['after']['pose'][2:6],r['after']['heading']]
  out.append(dict(scene=r['scene'],phase=phase,role=role,count=len(words),words=words))
 assert len(out)==5342 and collections.Counter(r['phase'] for r in out)=={0:2671,2:2671}
 return out,motion,roles
polls,raw,roles=project('observe1');assert polls==project('observe2')[0]
public=[]
for suffix in ('-start-preload.txt','-preload.txt'):
 public+=re.findall(r'call Preload\( "(AR299 [^"\r\n]*)" \)',files['observe1'+suffix].decode())
assert len(public)==2066
probe=files['sources/attack_recovery299_probe.j'].decode()
a.out.mkdir(parents=True,exist_ok=True)
data={'schema':1,'engine_parity':False,'bundle_sha256':spec['bundle_sha256'],'original_sha256':spec['game_sha256'],'probe_sha256':m.sha(probe.encode()),'public':public,'polls':polls,'raw_motion_observe1':raw,'roles':[{'scene':k[0],'pointer':k[1],'role':v} for k,v in sorted(roles.items())],'limits':['Native ordered projection only; no C comparison yet.','10 arrival and 6 postcommit words do not represent every private callback field; all raw motion rows retained separately.','Role mapping intersects resolver payloads with observed mover pointers, scoped by scene. No motion rows sorted or omitted.','Source role1, target role2, caster role3. Raw event IDs are not remapped.']}
(a.out/'replay299.json').write_text(json.dumps(data,indent=2)+'\n')
h=['/* SCRATCH generated from accepted299; no C parity claim. */','#ifndef RETAIL_ATTACK299_REPLAY_DRAFT_H','#define RETAIL_ATTACK299_REPLAY_DRAFT_H','typedef struct { unsigned scene, phase, role, count; unsigned words[10]; } attack299Poll_t;','static const attack299Poll_t attack299_polls[] = {']
for p in polls:h.append(' {%d,%d,%d,%d,{%s}},'%(p['scene'],p['phase'],p['role'],p['count'],','.join('0x%08xu'%x for x in p['words'])))
h+=['};','static const char *const attack299_public[] = {']+[json.dumps(x)+',' for x in public]+['};','static const char attack299_probe[] =']+[json.dumps(x+'\n') for x in probe.splitlines()]+[';','#endif']
(a.out/'retail_attack299_replay_draft.h').write_text('\n'.join(h)+'\n')
(a.out/'attack_recovery299_probe.j').write_text(probe)
summary={'public':len(public),'arrival':2671,'velocity':2671,'ordered_motion':len(polls),'roles':len(roles),'scene_counts':dict(collections.Counter(p['scene'] for p in polls)),'native_repeat_control_checked':True,'engine_parity':False,'outputs':{n:hashlib.sha256((a.out/n).read_bytes()).hexdigest() for n in ['replay299.json','retail_attack299_replay_draft.h','attack_recovery299_probe.j']}}
(a.out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
