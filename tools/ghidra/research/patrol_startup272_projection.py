#!/usr/bin/env python3
"""Check full controlled124c startup evidence; never relabel it old124b control.

Only pointer names are normalized. All ordered native numeric fields are retained.
Header rendering is available as render_header(rows); this tool writes no C/header.
"""
import argparse,collections,copy,gzip,hashlib,json,re,struct
from pathlib import Path
import patrol_lifetime_projection as lifetime
ROOT=Path(__file__).resolve().parents[3]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-patrol-startup272.json'
SPEC=FIXTURE.with_suffix('.provenance.json')
KINDS={'streams-reseed-enter','streams-reseed-leave','stream-real','attack-attach-enter','attack-attach-leave','attack-timer-request','attack-scan-enter','attack-scan-leave','arrival','velocity','marker'}
def need(ok,reason):
 if not ok: raise ValueError(reason)
def sha(b):return hashlib.sha256(b).hexdigest()
def normalize(a):
 pointers={}
 def walk(x,k=''):
  if isinstance(x,str) and x.startswith('0x'):
   assert k in {'pointer','ability','timer','receiver','mover','source','target','state'},k
   if x=='0x0':return 'null'
   pointers.setdefault(x,'p'+str(len(pointers)));return pointers[x]
  if isinstance(x,dict):return {k:walk(v,k) for k,v in x.items()}
  if isinstance(x,list):return [walk(v,k) for v in x]
  return x
 return [walk(x) for x in a if x.get('event') in KINDS]
def polls(a):
 roles={};out=[]
 for x in a:
  if x.get('event') not in ('arrival','velocity'):continue
  mover=x['before']['mover']
  if mover not in roles:
   xy=[struct.unpack('<f',struct.pack('<I',v))[0] for v in x['before']['pose'][2:4]];assert xy[0]==8.5 and xy[1] in [8,16,24,32,40,48,56];roles[mover]=int(xy[1]/8-1)
  if x['event']=='arrival':phase=0;words=[x['clock']['primary'][0],*x['source'],*x['destination'],x['heading'],x['threshold'],x['angle'],x['inside'],x['result']]
  else:phase=2;words=[x['clock']['primary'][0],*x['after']['pose'][2:6],x['after']['heading']]
  out.append({'phase':phase,'role':roles[mover],'words':words})
 return out

def load_capture(directory,name):
 rows=[json.loads(line) for line in (directory/(name+'.jsonl')).read_text().splitlines()]
 need(not any(x.get('type')=='error' for x in rows),'capture error')
 ends=[i for i,x in enumerate(rows) if x.get('event')=='marker' and x['value']=='PATHPUB tick=240 label=complete']
 need(len(ends)==1,'complete declared marker')
 footer=[x for x in rows if x.get('event')=='trace-end']
 need(len(footer)==1 and footer[0]['installed'],'trace footer')
 need(dict(collections.Counter(x['event'] for x in rows if x.get('event') in footer[0]['counts']))==footer[0]['counts'],'footer counts')
 return rows,rows[:ends[0]+1]

def check_rows(rows,expected):
 need(rows==expected,'complete ordered native projection differs')

def verify(directory):
 spec=json.loads(SPEC.read_text())
 for name,digest in spec['files'].items():need(sha((directory/name).read_bytes())==digest,'frozen bytes: '+name)
 raw=FIXTURE.read_bytes();need(sha(raw)==spec['projection_sha256'],'portable projection bytes')
 expected=json.loads(raw)
 aa,a=load_capture(directory,'observe1');bb,b=load_capture(directory,'observe2')
 x,y=normalize(a),normalize(b)
 check_rows(x,expected);check_rows(y,expected)
 need(len(x)==15356,'complete core count')
 counts=collections.Counter(z['event'] for z in x)
 need(counts==spec['expected']['counts'],'all core event counts')
 need(counts['attack-scan-enter']==counts['attack-scan-leave']==343,'scan pairs')
 stack=[]
 for z in x:
  if z['event']=='attack-scan-enter':stack.append(z['ability'])
  if z['event']=='attack-scan-leave':need(stack and stack.pop()==z['ability'],'scan owner pair')
 need(not stack,'unclosed scan')
 need(counts['attack-timer-request']==429,'timer count')
 seed=[z for z in x if z['event']=='streams-reseed-leave']
 need(len(seed)==1 and seed[0]['seed']==0x77617233 and len(seed[0]['states'])==45,'seed/states')
 draws=[z for z in x if z['event']=='stream-real' and z['attach']]
 need(len(draws)==13 and all(z['index']==2 for z in draws),'attachment draws')
 need(counts['attack-attach-enter']==counts['attack-attach-leave']==13,'attachment count')
 old,_=lifetime.verify()
 old_rows=[{k:r[k] for k in ('phase','role','words')} for r in old['rows']]
 need(polls(a)==polls(b)==old_rows and len(old_rows)==7124,'all old7124 numeric rows')
 markers=[z['value'] for z in x if z['event']=='marker']
 need(len(markers)==7072,'public count')
 for name in ('observe1','observe2','control1'):
  public=re.findall(r'PATH[^"\r\n]*',(directory/(name+'-preload.txt')).read_text())
  need(public==markers,'full public/control equality: '+name)
 control=[json.loads(line) for line in (directory/'control1.jsonl').read_text().splitlines()]
 need({z['event'] for z in control}<={'metadata','owned-loading-input','loading-key','control-start-file','preload-file'},'unhooked control whitelist')
 need(control[0]['mode']=='control' and any(z.get('event')=='preload-file' and z.get('complete') for z in control),'complete fresh control')
 for rows in (aa,bb,control):
  need(rows[0]['owned'] and rows[0]['env'] in ('B','C') and rows[0]['sha256']=='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236','native identity')
 return x

def render_header(rows):
 """Literal typed evidence. Clock[4] is native; duration is NOT an inferred deadline.

 Event ordinals preserve placement among ALL15356 rows. Pointer IDs are the
 normalized projection's pN, never engine entity indices. Actor identities are
 native canonical pairs. First scan clocks provide witnessed startup deadlines.
 """
 def pointer(v):return 0xffffffff if v=='null' else int(v[1:])
 def words(v):return ','.join('0x%08xu'%n for n in v)
 out=['/* Native272 controlled124c evidence; pointer IDs are projection-local. */',
 '#ifndef RETAIL_PATROL_STARTUP272_H','#define RETAIL_PATROL_STARTUP272_H',
 'typedef struct { uint32_t ordinal,ability,actor,identity[2],counter,clock[4]; } patrol272Scan_t;',
 'typedef struct { uint32_t ordinal,timer,receiver,code,caller,duration,counter,clock[4]; } patrol272Timer_t;',
 'typedef struct { uint32_t ordinal,ability,actor,identity[2],before[2],after[2],result,counter,clock[4]; } patrol272Draw_t;']
 seed=next(z for z in rows if z['event']=='streams-reseed-leave')
 out+=['static uint32_t const patrol272_seed = 0x77617233u;', 'static uint32_t const patrol272_states[45][2] = {']
 out+=[' {'+words(z['words'])+'},' for z in seed['states']];out+=['};']
 for kind,ctype,name in [('attack-scan-enter','patrol272Scan_t','scans'),('attack-timer-request','patrol272Timer_t','timers'),('stream-real','patrol272Draw_t','draws')]:
  out+=['static '+ctype+' const patrol272_'+name+'[] = {']
  for i,z in enumerate(rows):
   if z['event']!=kind or (kind=='stream-real' and not z['attach']):continue
   clock='{'+words(z['clock']['primary'])+'}'
   if kind=='attack-scan-enter':
    body=f"{i}u,{pointer(z['ability'])}u,{pointer(z['actor']['pointer'])}u,{{{words(z['actor']['identity'])}}},{z['clock']['counter']}u,{clock}"
   elif kind=='attack-timer-request':
    body=words([i,pointer(z['timer']),pointer(z['receiver']),z['code'],z['caller'],z['duration'],z['clock']['counter']])+','+clock
   else:
    a=z['attach'];body=f"{i}u,{pointer(a['ability'])}u,{pointer(a['actor']['pointer'])}u,{{{words(a['actor']['identity'])}}},{{{words(z['before'])}}},{{{words(z['after'])}}},0x{z['result']:08x}u,{z['clock']['counter']}u,{clock}"
   out+=[' {'+body+'},']
  out+=['};']
 out+=['#endif',''];return '\n'.join(out)

def render_acquisition_header(rows):
 """Full measured13attach/343fire seam, in native event order; no timer subset claim."""
 roles={}; active={}; projected=[]; attached=0
 for z in rows:
  event=z['event']
  if event=='attack-attach-enter':
   key=z['ability'];need(key not in active,'overlapping attach owner')
   roles[key]=attached;attached+=1;active[key]=z
  elif event=='attack-timer-request' and z['receiver'] in active:
   before=active[z['receiver']]
   projected.append((1,roles[z['receiver']],z['clock']['primary'][0],z['duration'],1))
  elif event=='attack-attach-leave':
   need(z['ability'] in active,'attach leave without entry');del active[z['ability']]
  elif event=='attack-scan-enter':
   need(z['ability'] in roles,'scan unregistered owner')
   projected.append((0,roles[z['ability']],z['clock']['primary'][0],0,int(bool(z['flags']&0x2000))))
 need(attached==13 and len(projected)==356,'complete attach/fire seam')
 seed=next(z for z in rows if z['event']=='streams-reseed-leave')
 out=['/* Native272 full13attach/343fire seam. All429timer requests remain in JSON. */',
 '#ifndef RETAIL_PATROL_STARTUP272_ACQUISITION_H','#define RETAIL_PATROL_STARTUP272_ACQUISITION_H',
 'typedef struct { unsigned phase,actor; uint32_t clock,duration; unsigned initial_pending; } patrol272Acquisition_t;',
 'static uint32_t const patrol272_seed = 0x77617233u;',
 'static uint32_t const patrol272_stream2_initial[2] = {'+','.join('0x%08xu'%v for v in seed['states'][2]['words'])+'};',
 'static patrol272Acquisition_t const patrol272_acquisition[] = {']
 out+=[' {%du,%du,0x%08xu,0x%08xu,%du},'%row for row in projected]
 out+=['};','#endif',''];return '\n'.join(out)

def mutations(rows):
 result=[]
 for label in ['missing','extra','reordered','clock','rng','actor','deadline','public']:
  altered=copy.deepcopy(rows)
  if label=='missing':altered.pop(10)
  elif label=='extra':altered.insert(10,copy.deepcopy(altered[10]))
  elif label=='reordered':altered[10:12]=altered[10:12][::-1]
  elif label=='clock':next(x for x in altered if x['event']=='attack-scan-enter')['clock']['primary'][0]^=1
  elif label=='rng':next(x for x in altered if x['event']=='stream-real')['result']^=1
  elif label=='actor':next(x for x in altered if x['event']=='attack-scan-enter')['actor']['identity'][0]^=1
  elif label=='deadline':next(x for x in altered if x['event']=='attack-timer-request')['duration']^=1
  else:next(x for x in altered if x['event']=='marker')['value']+=' altered'
  try:check_rows(altered,rows)
  except ValueError:result.append(label)
  else:raise ValueError('mutation accepted: '+label)
 return result

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture-dir',type=Path,required=True);p.add_argument('--check',action='store_true');p.add_argument('--header',type=Path);p.add_argument('--mutation-checks',action='store_true');a=p.parse_args()
 rows=verify(a.capture_dir);header=render_header(rows);seam=render_acquisition_header(rows)
 if a.header:need(a.header.read_bytes()==seam.encode(),'literal acquisition header differs')
 print(json.dumps(dict(passed=True,core_records=len(rows),public=7072,scans=343,attachments=13,timers=429,old_numeric_rows=7124,rendered_header_sha256=sha(header.encode()),mutations=mutations(rows) if a.mutation_checks else [],engine_parity=False)))
if __name__=='__main__':main()
