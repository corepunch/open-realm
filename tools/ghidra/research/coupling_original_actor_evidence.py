"""Audit complete public coupling event streams and scoped native constructors."""
import json,re,hashlib,struct,collections,gzip,base64
from pathlib import Path
R=Path(__file__).resolve().parents[3]
FIXTURES=R/'tools/ghidra/fixtures'
BUNDLE=FIXTURES/'retail-coupling-original-actor-1.27.json.gz'
SPEC=json.loads((FIXTURES/'retail-coupling-original-actor-1.27.json').read_text())
MAP='3edcafbc60904052d65bdcc9ff7539ff8c590b67ed0dbb7056f3e12c2b51fdc0'
DLL='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
CONTROL={'metadata','owned-loading-input','owned-loading-input-skipped','loading-key','control-start-file','preload-file'}
def need(v,m):
 if not v:raise ValueError(m)
def sha(b):return hashlib.sha256(b).hexdigest()
def parse(s):return dict(re.findall(r'(\w+)=([^ ]+)',s))
def literal(obj,prefix='raw'):
 b=base64.b64decode(obj[prefix+'_b64'],validate=True);need(sha(b)==obj[prefix+'_sha256'],'literal digest');return b
def unpack(bundle):
 need(bundle['version']==SPEC['version']==1 and bundle['task']==SPEC['task']=='TARGET-01.2' and bundle['engine_parity'] is SPEC['engine_parity'] is False and bundle['binary_sha256']==DLL,'evidence contract')
 need(sha(literal(bundle['map']))==SPEC['map']==MAP,'map pin')
 files={}
 for category in ['sources','artifacts']:
  need(set(bundle[category])==set(SPEC[category]),'source inventory')
  for n,entry in bundle[category].items():
   b=literal(entry);need(sha(b)==SPEC[category][n],'source pin');need((FIXTURES/'research/coupling-original-actor'/n).read_bytes()==b,'readable source');files[n]=b
 freeze=json.loads(files['frozen-sources-v2.json'])
 need(set(freeze)==(set(files)-{'frozen-sources-v2.json'})|{'TargetCoupling185.w3m'},'freeze inventory')
 for n,pin in freeze.items():need(sha(literal(bundle['map']) if n=='TargetCoupling185.w3m' else files[n])==pin,'frozen digest')
 return {'w3a':literal(bundle['w3a']),'captures':[{'rows':[json.loads(s) for s in literal(c).splitlines()],'preload':literal(c,'preload')} for c in bundle['captures']]}
def audit_loaded(data):
 frozen=SPEC['sources'];need(len(data['captures'])==3,'capture cardinality')
 w3a=data['w3a'];expected=struct.pack('<II',2,2)
 for a in (b'Acoa',b'Acoh'):expected+=a+bytes(4)+struct.pack('<I',1)+b'aran'+struct.pack('<IIII',2,1,0,0x43a68000)+bytes(4)
 need(w3a==expected+bytes(4),'authored333')
 streams=[];projections=[]
 for index,mode in enumerate(['observe1','observe2','control1']):
  rows=data['captures'][index]['rows'];m=rows[0];env=m['env']
  need(m['owned'] is True and m['sha256']==DLL and env in ['B','C'],'binary ownership')
  need(m['display']=={'B':':98','C':':99'}[env] and m['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[env],'environment')
  need(m['event']=='metadata' and m['task']=='TARGET-01.2-militia' and m['map']=='Maps/TargetCoupling185.w3m' and m['prefix']=='COUP ' and m['mode']==('control' if index==2 else 'observe'),'scope')
  need(m['source_sha256']=={**{n:frozen[n] for n in ['itemdrop_capture.py','itemdrop_observer.js','itemdrop_make_map.py','itemdrop_probe.j']},'map':MAP},'source pins')
  need(not any(r.get('event') in ['error','script-error','trace-failed'] or r.get('type')=='error' for r in rows),'errors')
  raw=data['captures'][index]['preload'];s=re.findall(r'call Preload\( "(COUP [^"\r\n]*)" \)',raw.decode())
  need(len(s)==122 and s[-1]=='COUP tick=699 scene=2 label=complete','coverage')
  need(rows[-1]==dict(event='preload-file',sha256=sha(raw),markers=122,complete=True),'footer');streams.append(s)
  if index==2:
   need(all(r['event'] in CONTROL for r in rows),'unhooked control');continue
  need([r['value'] for r in rows if r['event']=='marker']==['COUP tick=0 scene=-1 label=start']+s,'public hook equivalence')
  ends=[r for r in rows if r['event']=='trace-end'];need(len(ends)==1 and ends[0]['installed'] is True,'installed')
  counts=ends[0]['counts'];need(collections.Counter(r['event'] for r in rows if r['event'] in counts)==collections.Counter(counts),'counts')
  need({r['event'] for r in rows}-CONTROL-{'trace-end'}==set(counts),'inventory')
  projection=[]
  for scene in range(2):
   rs=[r for r in rows if r.get('scene')==scene];p=[r for r in rs if r['event']=='couple-producer-enter'];tasks=[r for r in rs if r['event']=='target-task'];ready=[r for r in rs if r['event']=='couple-ready']
   need([r['order'] for r in p]==[852143,852141],'constructor suppression')
   admission=next(r for r in rs if r['event']=='couple-admission' and r['actor']['rawcode']==0x65617263 and r['target']['rawcode']==0x65686970)
   actors={v['pointer']:v for v in [admission['actor'],admission['target']]}
   need(len(tasks)==len(p)*2 and [r['code'] for r in tasks]==[0xd018a,0xd0171]*len(p),'tasks')
   for i,t in enumerate(tasks):
    a=p[i//2]['actor'];b=next(v for k,v in actors.items() if k!=a['pointer'])
    need(t['unit']['pointer']==a['pointer'] and t['unit']['identity']==a['identity']==actors[a['pointer']]['identity'] and t['target']==b['pointer'],'task incarnation/opposite target')
    need(t['range']==0 and t['caller']==[0x4e3d55,0x4e3d69][i%2],'task raw range/caller')
   need(len(ready)==0 and not any(r['event']=='couple-merge' for r in rs),'bounded readiness/no merge')
   if ready:
    a=actors[ready[0]['actor']['pointer']];b=next(v for k,v in actors.items() if k!=a['pointer'])
    need(a['rawcode']==0x65617263 and ready[0]['actor']['identity']==a['identity'] and ready[0]['partner']==b['identity'] and ready[0]['range']==0x43a68000,'ready identity/range')
   projection.append({'scene':scene,'orders':[r['order'] for r in p],'ready_ticks':[r['tick'] for r in ready]})
  projections.append(projection)
 need(streams[0]==streams[1]==streams[2],'exact complete public stream')
 need(projections[0]==projections[1],'constructor repeats')
 parsed=[parse(s) for s in streams[0]];summary=[]
 targets=[('issued-target','852143','1','2'),('issued-target','852141','2','1')]
 events=[targets+[('issued-immediate','851972','2','0'),('issued-point','851986','1','0'),('issued-immediate','851972','1','0')],targets+[('issued-immediate','851972','2','0'),('issued-immediate','851972','1','0')]]
 for scene in range(2):
  rs=[r for r in parsed if r['scene']==str(scene)];get=lambda label:next(r for r in rs if r['label']==label)
  es=[r for r in rs if r['label'].startswith('issued-')]
  need([(r['label'],r['oid'],r['actor'],r['target']) for r in es]==events[scene],'exact event sequence/context')
  need(es[0]['ah']=='852143' and es[0]['hh']=='0' and es[1]['ah']=='852143' and es[1]['hh']=='852141','current heads during target events')
  order=get('order-return');end=get('settled');need(order['accepted']==order['aa']==order['ha']==order['reht']=='1','admission')
  need(end['riders']=='0','no merge')
  stop=get('stop-archer');before=get('nested-before')
  need(stop['ah']==stop['hh']==end['ah']==end['hh']=='0','outer Stop wins')
  need((stop['ax'],stop['ay'],stop['hx'],stop['hy'])==(end['ax'],end['ay'],end['hx'],end['hy']),'no post-Stop travel')
  if scene==0:
   nested=get('nested-original-move-return')
   need(nested['accepted']=='1' and nested['ah']=='851986' and nested['hh']=='851972','nested original Move accepted')
   need(es[-1]['ah']=='851972' and es[-1]['hh']=='0','outer Stop published after nested Move')
  else:
   nested=get('nested-original-remove-return')
   need(nested['ah']=='0' and nested['at']==before['at']==stop['at']==end['at'] and float(end['al'])==float(before['al'])>0,'removal attempt does not prove removal')
  summary.append({'scene':scene,'issued_events':es,'outer_return':order,'settled':end})
 need([r for r in parsed if r['label'].startswith('issued-')]==SPEC['issued_events'],'literal issued response context')
 need(sha('\n'.join(streams[0]).encode())==SPEC['public_sha256'],'literal public digest')
 return {'passed':True,'engine_parity':False,'remaining_owner':'TARGET-01.2','markers_per_capture':122,'matching_public_markers':366,'projection':projections[0],'scenes':summary,'scope':'Inverse peerStop callback targets ORIGINAL actor. OuterStop supersedes nestedMove on original.','caveats':['RemoveUnit attempt left original rawtype/life and world pose through boundedend; no successful removal or underlying cause claim.','Targetrole0 means neitherwatchedactor, notprovennull.','No postnested event response context query.','No generic callbackprecedence claim beyond thisfrontier.']}
def audit(bundle=None):
 if bundle is None:
  raw=BUNDLE.read_bytes();need(sha(raw)==SPEC['bundle_sha256'],'compressed bundle digest');bundle=json.loads(gzip.decompress(raw))
 return audit_loaded(unpack(bundle))
if __name__=='__main__':print(json.dumps(audit(),indent=2))
