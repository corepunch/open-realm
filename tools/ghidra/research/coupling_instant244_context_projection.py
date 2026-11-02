#!/usr/bin/env python3
"""Native244 public spell target-context extension; no numerical poll oracle.
The complete104-marker stream and12context rows repeat/control exactly. Removing
context markers preserves accepted243's92public+131scoped rows. Role0 alone is
not a null-handle measurement. No game launch, MPQ tool, or private calls.
"""
import pathlib,json,re,hashlib,collections,copy,struct
from coupling_instant243_preparation_projection import require,projection,preload,rows
ROOT=pathlib.Path(__file__).resolve().parents[3]
FIXTURE=ROOT/'games/warcraft-3/game/tests/fixtures/retail_couple_instant244_context.json'
BASELINE=FIXTURE.with_name('retail_couple_instant243_preparation.json')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def parse(s):return dict(re.findall(r'(\w+)=([^ ]+)',s))
def word(s):return struct.unpack('<I',struct.pack('<f',float(s)))[0]
def project(capture_dir):
 r=pathlib.Path(capture_dir);f=json.loads((r/'frozen-sources.json').read_text());base=json.loads(BASELINE.read_text())
 for n,h in f.items():require(sha(r/n)==h,'freeze '+n)
 public=[];prefix=[];native=[];contexts=[];counts=[]
 for n in ['observe1','observe2','control1']:
  a=rows(r/(n+'.jsonl'));m=a[0];env=m['env'];require(m['owned'] and env in ['B','C'],'ownership');require(m['display']=={'B':':98','C':':99'}[env] and m['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[env],'environment')
  require(m['sha256']=='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236','DLL')
  require(m['source_sha256']=={**{k:f[k] for k in ['itemdrop_capture.py','itemdrop_make_map.py','itemdrop_probe.j','itemdrop_observer.js']},'map':f['RS-TARGET244-preparatory-context-v1.w3m']},'source/map provenance')
  raw=r/(n+'-preload.txt');ms=preload(raw);public.append(ms);require(len(ms)==104 and max(map(len,ms))<=160,'markers');require(a[-1]=={'event':'preload-file','sha256':sha(raw),'markers':104,'complete':True},'footer')
  require([x for x in ms if 'label=spellcontext ' not in x]==base['public'],'243 complete public baseline')
  require(not any(x.get('event') in ['error','trace-failed','script-error'] or x.get('type')=='error' for x in a),'capture error')
  if n=='control1':require(m['mode']=='control' and all(x['event'] in ['metadata','owned-loading-input','loading-key','control-start-file','preload-file'] for x in a),'unhooked');continue
  ends=[x for x in a if x['event']=='trace-end'];require(len(ends)==1 and ends[0]['installed'],'trace end');c=ends[0]['counts'];require(collections.Counter(x['event'] for x in a if x['event'] in c)==collections.Counter(c),'counts');require(set(x['event'] for x in a)-set(c)=={'metadata','owned-loading-input','loading-key','trace-end','preload-file'},'inventory');counts.append(c)
  hs=[x for x in a if x['event']=='marker'];require(len(hs)==107 and [x['value'] for x in hs[3:]]==ms,'hook/public');prefix.append([x['value'] for x in hs[:3]])
  clean=[x for x in a if not(x['event']=='marker' and 'label=spellcontext ' in x['value'])];projected=projection(clean);require(projected==base['scoped_native'],'243 all131 scoped rows unchanged');native.append(projected)
  ctx=[]
  for i,row in enumerate(hs):
   q=parse(row['value'])
   if q['label']!='spellcontext':continue
   prev=parse(hs[i-1]['value']);require(prev['label']=='spell' and all(prev[k]==q[k] for k in ['event','actor','tick','scene']),'typed context predecessor')
   ctx.append({'marker':row,'ability':int(prev['ability']),'tick':int(q['tick']),'event':int(q['event']),'actor':int(q['actor']),'target':int(q['target']),'x_word':word(q['x']),'y_word':word(q['y'])})
  require(len(ctx)==12,'complete context count');contexts.append(ctx)
 require(public[0]==public[1]==public[2] and prefix[0]==prefix[1],'full repeat/control');require(native[0]==native[1] and contexts[0]==contexts[1],'scoped/context repeat')
 require([x['event'] for x in contexts[0]]==[272,273,274,276,272,275,276,272,273,274,275,276],'typed phase order')
 require([x['target'] for x in contexts[0]]==[1]*4+[0]*3+[4]*5,'target roles')
 require([(x['x_word'],x['y_word']) for x in contexts[0]]==[(word(1280),word(512))]*4+[(word(1280),word(1024))]+[(0,0)]*7,'target XY chronology')
 return {'schema':1,'public':public[0],'hook_prefix':prefix[0],'scoped_native':native[0],'contexts':contexts[0],'capture_event_counts':counts[0],'probe_jass':(r/'itemdrop_probe.j').read_text(),'typed_spell_events':base['typed_spell_events'],'limitations':['Role0 alone identifies neither watched actor; this probe has no explicitnull predicate.','No numeric movement poll oracle.','Same243 gameplay and all92public/131scoped baseline rows are unchanged.','Native275FINISH/276ENDCAST; FourCC and event normalization must be typed.']}

def header_text(d):
 def strings(name,items):return 'static char const *const '+name+'[] = {\n'+''.join('    '+json.dumps(s)+',\n' for s in items)+'};\n'
 source=d['probe_jass'].replace('call Preload(','call CouplePreparationObserve(')
 result='/* Native244; only Preload replaced by observation adapter. */\n#ifndef RETAIL_COUPLE_INSTANT244_CONTEXT_H\n#define RETAIL_COUPLE_INSTANT244_CONTEXT_H\nstatic char const couple_native_probe244[] =\n'+''.join('    '+json.dumps(s)+'\n' for s in source.splitlines(keepends=True))+';\n'
 result+=strings('couple_native_prefix244',d['hook_prefix'])+strings('couple_native_public244',d['public'])
 result+='/* Events272CHANNEL273CAST274EFFECT275FINISH276ENDCAST; ability is native FourCC. */\ntypedef struct { unsigned tick,event,actor,target; uint32_t ability,x_word,y_word,clock_word; } coupleNativeContext244_t;\nstatic coupleNativeContext244_t const couple_native_context244[] = {\n'
 for c in d['contexts']:result+='{%d,%d,%d,%d,0x%08xu,0x%08xu,0x%08xu,0x%08xu},\n'%(c['tick'],c['event'],c['actor'],c['target'],c['ability'],c['x_word'],c['y_word'],c['marker']['clock']['primary'][0])
 return result+'};\n#endif\n'
def compare(a,b):require(a==b,'complete ordered projection')
def verify_header(text,d):require(text==header_text(d),'generated header source/public/context fields')
def mutation_checks(d):
 checks=[]
 for key in ['public','contexts','scoped_native']:
  for kind in ['missing','extra','reordered','field']:
   x=copy.deepcopy(d);a=x[key]
   if kind=='missing':a.pop()
   elif kind=='extra':a.append(copy.deepcopy(a[-1]))
   elif kind=='reordered':a[0],a[1]=a[1],a[0]
   elif key=='public':a[0]+=' MUTATION'
   elif key=='contexts':a[0]['target']+=1
   else:a[0]['tick']+=1
   try:compare(x,d)
   except ValueError:checks.append(key+':'+kind)
   else:raise ValueError('accepted projection mutation')
 text=header_text(d);mark=json.dumps(d['public'][0])+',';other=json.dumps(d['public'][1])+','
 variants=[text.replace(mark,'',1),text.replace(mark,mark+mark,1),text.replace(mark,'SWAP',1).replace(other,mark,1).replace('SWAP',other,1),text.replace('0x44a00000u','0x44a00001u',1)]
 for i,t in enumerate(variants):
  try:verify_header(t,d)
  except ValueError:checks.append('header:'+str(i))
  else:raise ValueError('accepted header mutation')
 return checks

def verify(capture_dir,fixture=FIXTURE):
 p=json.loads(fixture.with_suffix('.provenance.json').read_text());require(sha(fixture)==p['projection_sha256'] and sha(BASELINE)==p['baseline243_sha256'],'fixture/baseline pins')
 for n,h in p['capture_files_sha256'].items():require(sha(pathlib.Path(capture_dir)/n)==h,'capture pin '+n)
 d=project(capture_dir);compare(d,json.loads(fixture.read_text()));h=fixture.with_suffix('.h');require(sha(h)==p['header_sha256'],'headerpin');verify_header(h.read_text(),d);require(len(mutation_checks(d))==16,'negative controls');return d
if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture-dir',type=pathlib.Path,required=True);p.add_argument('--fixture',type=pathlib.Path,default=FIXTURE);p.add_argument('--check',action='store_true');a=p.parse_args();verify(a.capture_dir,a.fixture);print('PASS:244104public/12context rows; repeat+control;24392public/131scoped baseline; semantic header;16negative controls.')
