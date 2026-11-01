#!/usr/bin/env python3
"""Read-only native233 evidence projection and complete chronology verification.

Requires --capture-dir pointing to the frozen233 evidence directory. No mpqtool,
native game launch or original machine-specific auditor is invoked. Public
markers remain literal; numeric rawcode/alias/ability fields are WC3 FourCC words
(big-endian ASCII display), NOT engine host-packed integers. The typed_fourcc
map names those fields without byte-swapping arbitrary numeric data or clocks.
Only pointer/canonical identities are equality-preserving normalized tokens.
"""
from pathlib import Path
import json,re,hashlib,collections
ROOT=Path(__file__).resolve().parents[3]
FIXTURE=ROOT/'games/warcraft-3/game/tests/fixtures/retail_couple_instant233_ready.json'
sha=lambda b:hashlib.sha256(b).hexdigest()
def require(x,m):
 if not x:raise ValueError(m)
def parse(s):return dict(re.findall(r'(\w+)=([^ ]+)',s))
def project(capture_dir):
 R=Path(capture_dir)
 frozen=json.loads((R/'frozen-sources.json').read_text())
 for n,h in frozen.items():require(sha((R/n).read_bytes())==h,'freeze '+n)
 public=[];native=[];scenes=[]
 allowed={'metadata','owned-loading-input','owned-loading-input-skipped','loading-key','control-start-file','preload-file'}
 for name in ['observe1','observe2','control1']:
  rows=[json.loads(x) for x in (R/(name+'.jsonl')).read_text().splitlines()];meta=rows[0];env=meta['env']
  require(env in ['B','C'] and meta['owned'],'owned');require(meta['display']=={'B':':98','C':':99'}[env] and meta['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[env],'environment')
  require(meta['sha256']=='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236','dll')
  require(meta['source_sha256']=={**{n:frozen[n] for n in ['itemdrop_capture.py','itemdrop_make_map.py','itemdrop_probe.j','itemdrop_observer.js']},'map':frozen['RS-TARGET233-ready-adjacent-v1.w3m']},'provenance')
  raw=(R/(name+'-preload.txt')).read_bytes();lines=re.findall(r'call Preload\( "(CPR [^"\r\n]*)" \)',raw.decode());public.append(lines)
  require(lines[-1]=='CPR tick=4399 scene=11 label=complete','complete');require(all(len(x)<=160 and 'error' not in x for x in lines),'guards');require(rows[-1]=={'event':'preload-file','sha256':sha(raw),'markers':len(lines),'complete':True},'footer')
  require(not any(x.get('event') in ['trace-failed','error','script-error'] or x.get('type')=='error' for x in rows),'errors')
  for scene in range(11):
   p=[parse(s) for s in lines if int(parse(s)['scene'])==scene];alias=next(x for x in p if x['label']=='alias');inputs=next(x for x in p if x['label']=='inputs');require(alias['level']=='1','activealias publiclevel')
   expected=0x58633331+scene-4 if 4<=scene<=6 else 0x41636f33
   require(int(alias['alias'])==expected,'aliasidentity');require(alias['removed']==alias['added']==('1' if 4<=scene<=6 else '0'),'publicadd/remove')
   require(int(inputs['rangeBits'])==(0x43a67fd3+scene-4 if 4<=scene<=6 else 0x43a68000),'rangeinput')
   ret=next(x for x in p if x['label']=='order-return-setup');require(ret['accepted']=='1','public order admitted')
  if name=='control1':require(all(x['event'] in allowed for x in rows),'unhooked');continue
  require([x['value'] for x in rows if x['event']=='marker']==['CPR tick=0 scene=-1 label=start']+lines,'publichook')
  ends=[x for x in rows if x['event']=='trace-end'];require(len(ends)==1 and ends[0]['installed'],'installed');counts=ends[0]['counts'];require(collections.Counter(x['event'] for x in rows if x['event'] in counts)==collections.Counter(counts),'counts');require({x['event'] for x in rows}-allowed-{'trace-end'}==set(counts),'inventory')
  ptrs={};identities={}
  def norm(v,k=''):
   if isinstance(v,dict):return {a:norm(b,a) for a,b in v.items()}
   if isinstance(v,list):
    if k in ['identity','retained','head','task','group']:
     key=tuple(v);identities.setdefault(key,len(identities));return {'identity_token':identities[key]}
    return [norm(x) for x in v]
   if isinstance(v,str) and re.fullmatch(r'0x[0-9a-f]+',v):
    require(k in {'pointer','argument','mover','target','source'},'Unclassified hexadecimal string field '+k)
    if v=='0x0':return v
    ptrs.setdefault(v,len(ptrs));return {'pointer_token':ptrs[v]}
   return v
  projection=[norm(x) for x in rows if x['event'] in counts]
  native.append(projection)
  if name=='observe1':
   for scene in range(11):
    rs=[x for x in rows if x.get('scene')==scene];prod=[x for x in rs if x['event']=='instant-producer-enter'];require(prod,'producer')
    expected=0x58633331+scene-4 if 4<=scene<=6 else 0x41636f33
    for x in prod:
     a=x['ability'];alias=a['alias'];require(a['data'][0]==0x300000 and a['data'][6]==0x44610000 and a['data'][9]==(0 if alias==0x41636f32 else 0x3f800000) and a['data'][17]==0x65687072,'targs Area DataB UnitID');require(alias in [0x41636f32,expected],'runtimealias');require(a['rank']==0 and a['data'][7]==(0x43a67fd3+scene-4 if alias==expected and 4<=scene<=6 else 0x43a68000),'runtime range')
    scenes.append({'scene':scene,'producers':[x['ability']['alias'] for x in prod],'ranges':[{'range':x['range'],'result':x['result'],'movers':x['movers'],'clock':x['clock']} for x in rs if x['event']=='target-range-query'],'facing':[{'window':x['window'],'result':x['result'],'movers':x['movers'],'clock':x['clock']} for x in rs if x['event']=='facing-query'],'tasks':[x for x in rs if x['event']=='target-task'],'polls':sum(x['event']=='arrival' for x in rs),'public_events':[x['value'] for x in rs if x['event']=='marker' and ('label=spell-' in x['value'] or 'label=issued-' in x['value'])]})
 require([x['polls'] for x in scenes]==[0,0,0,0,2,0,0,0,2,2,0],'complete pollcounts')
 for scene in [4,5,6]:
  q=scenes[scene]['ranges'][0];require(q['range']==0x43a67fd3+scene-4 and q['result']==(0 if scene==4 else 1),'geometrybranch');require(len(q['movers'])==2 and q['movers'][0]['pose'][2:4]==[1111490560,1107296256] and q['movers'][1]['pose'][2:4]==[1108434944,1107296256] and sorted(m['radius'] for m in q['movers'])==[1048576000,1065353216],'actual boundary geometry/radii')
 for scene,heading,result in [(7,0x3e4cccda,1),(8,0x3e4cccdb,0),(9,0x3e4cccdc,0),(10,0x3e4ccccd,1)]:
  q=scenes[scene]['facing'][0];require(q['window']==0x3e4ccccd and q['result']==result and q['movers'][0]['heading']==heading,'angularrawbranch')
 require(public[0]==public[1]==public[2],'public repeat/control');require(native[0]==native[1],'full pointer-normalizednative repeat')
 return {'schema':1,'engine_parity':False,'public':public[0],'native':native[0],'typed_fourcc':{'0x41636f32':'Aco2','0x41636f33':'Aco3','0x58633331':'Xc31','0x58633332':'Xc32','0x58633333':'Xc33','0x65617263':'earc','0x65686970':'ehip','0x65687072':'ehpr'},'passed':True,'markers_per_capture':len(public[0]),'public_sha256':sha(('\n'.join(public[0])+'\n').encode()),'scene_poll_counts':[x['polls'] for x in scenes],'native_counts':dict(collections.Counter(x['event'] for x in native[0])),'limitations':['Observer makes no native calls/writes; exact authoredaliases are publicadd lab setup.','Role0 is neitherwatchedactor, not alone proof of nulltarget.','Publicrange/facinghelper predictions are not IssueOrder acceptance claims.','Pointer/canonicalidentity values normalized by encounter-order equality; all numericquery/poll/cache words retained.']}


def header_initializers(result):
 polls=[];public=[]
 kinds={'issued-immediate':0,'issued-target':1,'spell-channel':2,'spell-cast':3,'spell-effect':4,'spell-endcast':5,'spell-finish':6}
 for row in result['native']:
  if row['event'] in ['arrival','velocity']:
   phase=0 if row['event']=='arrival' else 2;role=0 if row['before']['pose'][6]==0x41070000 else 1
   words=[row['clock']['primary'][0],*row['source'],*row['destination'],row['heading'],row['threshold'],row['angle'],row['inside'],row['result']] if phase==0 else [row['clock']['primary'][0],*row['after']['pose'][2:6],row['after']['heading']]
   polls.append('{%d,%d,%d,%d,{%s}},'%(row['scene'],phase,role,len(words),','.join('0x%08xu'%w for w in words)))
  if row['event']=='marker':
   q=parse(row['value']);label=q['label']
   if label in kinds:
    values=[int(q['scene']),int(q['tick']),kinds[label],int(q['oid'] if label.startswith('issued-') else q['ability']),int(q['actor']),int(q['target']),int(q['ah']),int(q['hh']),row['clock']['primary'][0]]
    public.append('{%d,%d,%d,0x%08xu,%d,%d,%du,%du,0x%08xu},'%tuple(values))
 require(len(polls)==12 and len(public)==59,'Complete header projection counts')
 return {'couple_native_polls233':('coupleNativePoll233_t',polls),'couple_native_public233':('coupleNativePublic233_t',public)}

def verify_header_text(source,result):
 source=re.sub(r'/\*.*?\*/|//[^\n]*','',source,flags=re.S)
 for name,(typename,rows) in header_initializers(result).items():
  declarations=list(re.finditer(r'static\s+'+typename+r'\s+const\s+'+name+r'\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;',source,re.S))
  require(len(declarations)==1,'Unique literal initializer '+name)
  require(re.sub(r'\s+','',declarations[0][1])==''.join(rows),'Ordered header fields '+name)

def check_header_mutations(source,result):
 verify_header_text(source,result);checks=[]
 for name,(typename,rows) in header_initializers(result).items():
  variants={'missing':rows[1:],'extra':rows+[rows[-1]],'reordered':[rows[1],rows[0]]+rows[2:],'raw_word':[rows[0].replace('0x','0x1',1)]+rows[1:]}
  for label,changed in variants.items():
   pattern=r'(static\s+'+typename+r'\s+const\s+'+name+r'\s*\[\s*\]\s*=\s*\{)(.*?)(\}\s*;)'
   mutated=re.sub(pattern,lambda m:m[1]+''.join(changed)+m[3],source,flags=re.S)
   try:verify_header_text(mutated,result)
   except ValueError:checks.append(name+':'+label)
   else:raise ValueError('Accepted header mutation '+name+':'+label)
 return checks


def verify(capture_dir, fixture=FIXTURE):
 provenance=json.loads(fixture.with_suffix('.provenance.json').read_text())
 require(sha(fixture.read_bytes())==provenance['projection_sha256'],'fixture digest')
 for name,pin in provenance['capture_files_sha256'].items():
  require(sha((Path(capture_dir)/name).read_bytes())==pin,'frozen file '+name)
 result=project(capture_dir)
 require(result==json.loads(fixture.read_text()),'complete ordered native/public projection')
 header_path=fixture.with_name('retail_couple_instant233_boundaries.h')
 require(sha(header_path.read_bytes())==provenance['header_sha256'],'Header digest')
 header=header_path.read_text()
 verify_header_text(header,result)
 require(len(check_header_mutations(header,result))==8,'Header mutation guards')
 return result

def main():
 import argparse
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture-dir',type=Path,required=True);p.add_argument('--fixture',type=Path,default=FIXTURE);p.add_argument('--check',action='store_true');a=p.parse_args()
 d=verify(a.capture_dir,a.fixture)
 print('PASS:233 eleven scenes;2150 public records, full normalized native chronology, both observations and fresh unhooked control exact;12 poll +59 public header tuples exact;8 header mutations rejected.')

if __name__=='__main__':main()
