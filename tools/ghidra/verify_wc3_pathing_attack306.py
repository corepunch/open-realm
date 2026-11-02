#!/usr/bin/env python3
"""Check public Attack profiles, exact physical words and owned saved execution.

Frozen retail expectations are derived only from two read-only repeats, checked
against the unhooked public control. Tree orders remain a point-task control.
"""
import argparse,gzip,hashlib,json,re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-attack306-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
TESTS=['wc3_movement.attack306*','wc3_movement.target222*','wc3_movement.target223*',
       'wc3_movement.policy245*','wc3_movement.structure279*','wc3_movement.flight280*',
       'wc3_combat.*','wc3_order_lifecycle.*','wc3_interrupt.*',
       'wc3_save.rejects_invalid_physical_group_payloads','wc3_save.rejects_prior_save_versions']
SOURCES=['tools/frida/research/attack306_'+s for s in('capture.py','observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/smart277_ui_input.c','tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c',
 'tools/ghidra/research/Attack306Evidence.java','games/warcraft-3/game/skills/fixtures/retail_attack306_motion.h']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(A306 [^"\r\n]*)" \)',text)
def scalar_commits(rows):
 out=[]
 for x in rows:
  if x['event']in('commit-enter','commit-leave'):
   g=x['g']
   if g['count']!=1 or len(g['members'])!=1 or not g['members'][0]['m']:raise ValueError('missing singleton physical state')
   m=g['members'][0]['m']
   if m['group']!=g['id'] or m['id']!=g['members'][0]['id']:raise ValueError('physical identity ownership')
   out.append([x['scene'],x['c'],x['event'],{k:m[k]for k in('pos','vel','clock','maximum','heading','radius','range','flags')}])
 return out

def motion(rows):
 out=[]
 for scene in(0,1):
  rr=[x for x in rows if x.get('scene')==scene];bind=next(x for x in rr if x['event']=='bind');gid=bind['g']['id']
  a=[x for x in rr if x['event']=='commit-enter'and x['g']['id']==gid]
  b=[x for x in rr if x['event']=='commit-leave'and x['g']['id']==gid]
  if len(a)!=len(b) or len(a)!=(30 if scene==0 else 32):raise ValueError('missing uninterrupted chase visits')
  result=[]
  for before,after in zip(a,b):
   if before['c']!=after['c']:raise ValueError('unpaired commit')
   m=before['g']['members'][0]['m'];end=after['g']['members'][0]['m']
   result.append([before['c'],*end['clock'],*m['pos'],*m['vel'],m['range'],m['heading']])
  out.append(dict(clock=bind['m']['clock'],counter=bind['c'],rows=result))
 return out

def validate_runtime(bundle,spec):
 caps=bundle['captures']
 if len(caps)!=3 or [c['rows'][0]['mode']for c in caps]!=['observe','observe','control']:raise ValueError('two repeats and unhooked control required')
 commits=[]
 for cap in caps:
  rows=cap['rows'];meta=rows[0];footer=rows[-1];public=markers(cap['preload'])
  if(meta['task']!='ATTACK-PROFILES/attack306' or meta['sha256']!=SHA or not meta['owned'] or
     meta['env']not in('B','C') or meta['source_sha256']['map']!=spec['map_sha256'] or
     footer['event']!='preload-file' or not footer.get('complete') or footer['markers']!=349 or
     len(public)!=349 or public!=spec['public'] or hashlib.sha256(cap['preload'].encode()).hexdigest()!=footer['sha256'] or
     any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('capture provenance/completion/public control')
  for p in SOURCES[:7]:
   if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('captured source differs '+p)
  if meta['mode']=='control':
   if any(r['event']in('trace-end','bind','move-task','commit-enter')for r in rows):raise ValueError('hooked control')
   continue
  ends=[r for r in rows if r['event']=='trace-end'];live=[r for r in rows if 'seq'in r and r['event']!='trace-end']
  if len(ends)!=1 or not ends[0]['installed'] or not ends[0]['readOnly'] or ends[0]['seq']!=len(live) or [r['seq']for r in live]!=list(range(len(live))):raise ValueError('observer completeness')
  if dict(Counter(r['event']for r in live))!=ends[0]['counts'] or len(live)!=1220:raise ValueError('lost observer events')
  if [r['value']for r in rows if r['event']=='marker'and ' label=start 'not in r['value']]!=public:raise ValueError('live/preload mismatch')
  for scene in range(6):
   rr=[x for x in rows if x.get('scene')==scene];tasks=[x for x in rr if x['event']=='move-task'];requests=[x for x in rr if x['event']=='target-request'];binds=[x for x in rr if x['event']=='bind']
   if len(tasks)!=[3,4,0,6,4,0][scene] or len(requests)!=len(tasks) or not binds:raise ValueError('profile producer inventory')
   for task,request in zip(tasks,requests):
    if task['range']!=request['range'] or task['persistent'] or task['subscribe'] or request['persistent']:raise ValueError('captured target request')
    if task['unit']['code']!=(0x68677279 if scene%3==1 else 0x68666f6f):raise ValueError('profile type differs')
    if (request['targetRegion']=='0x0')!=(scene%3==1):raise ValueError('structure/flyer captured region')
   if scene in(2,5):
    if any(x['g']['flags']!=0 or x['g']['target']!=[0xffffffff]*2 for x in binds):raise ValueError('tree point control differs')
   else:
    expected=(0x42b40000 if scene==0 else 0x43e10000 if scene==1 else 0x7f7fffff)
    if tasks[0]['range']!=expected or binds[0]['g']['flags']!=(0x1000 if scene<3 else 0x1200):raise ValueError('range/turn-only policy differs')
   for label in('accepted','paused','resumed','stopped','end-cleanup'):
    if sum(x['event']=='marker'and f'label={label} 'in x['value']for x in rr)!=1:raise ValueError('public lifecycle incomplete')
  if motion(rows)!=spec['motion']:raise ValueError('captured motion differs')
  commits.append(scalar_commits(rows))
 if commits[0]!=commits[1] or len(commits[0])!=760:raise ValueError('scalar commits do not repeat')
 return dict(captures=2,controls=1,public_markers=349,profile_cases=6,physical_visits=380,exact_chase_states=62)

def validate(spec,bundle=None):
 if(spec['version']!=1 or spec['task']!='TARGET-01.2/ORDER-01.2' or spec['game_sha256']!=SHA or
    spec['engine_tests']!=TESTS or len(spec['instructions'])!=338 or set(spec['pins'])!=set(SOURCES)):
  raise ValueError('Attack profile contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('source pin '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('frozen capture changed')
 header=(ROOT/SOURCES[-1]).read_text()
 literal=[[int(x,16)for x in re.findall(r'0x([0-9a-f]{8})u',row)]for row in re.findall(r'\{([^{}]+)\}',header) if len(re.findall(r'0x[0-9a-f]{8}u',row))==9]
 if literal!=[r for s in spec['motion']for r in s['rows']]:raise ValueError('header differs from original capture')
 return validate_runtime(bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes())),spec)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());evidence=validate(spec);original_bytes(a.binary,spec['instructions'])
 import verify_wc3_pathing_work242 as engine
 engine.TESTS=TESTS
 result=dict(passed=True,status='verified',task=spec['task'],instructions=len(spec['instructions']),**evidence,
             engine=engine.run_engine(a.test_binary,a.data,a.report),binary_sha256=SHA,
             game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
