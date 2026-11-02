"""Strict bounded original coupling lifecycle witness; no engine parity claim."""
import json,re,hashlib,struct,collections,gzip
from coupling_producer_evidence import literal
from pathlib import Path
R=Path(__file__).resolve().parents[3]
FIXTURES=R/'tools/ghidra/fixtures'
BUNDLE=FIXTURES/'retail-coupling-lifecycle-1.27.json.gz'
SPEC=json.loads((FIXTURES/'retail-coupling-lifecycle-1.27.json').read_text())
MAP='2c676db04ae5eacd4d2dc78e330c70cb6a912edaffa4f8b0ba0a69e33508fc2f'
DLL='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
CONTROL={'metadata','owned-loading-input','owned-loading-input-skipped','loading-key','control-start-file','preload-file'}
def need(v,m):
 if not v:raise ValueError(m)
def sha(b):return hashlib.sha256(b).hexdigest()
def parse(s):return dict(re.findall(r'(\w+)=([^ ]+)',s))
def xy(row,who):return(row[who+'x'],row[who+'y'])
def unpack(bundle):
 need(bundle['version']==SPEC['version']==1 and bundle['task']==SPEC['task']=='TARGET-01.2' and bundle['engine_parity'] is SPEC['engine_parity'] is False,'evidence scope')
 need(sha(literal(bundle['map']))==SPEC['map']==MAP,'map pin')
 need(set(bundle['sources'])==set(SPEC['sources']),'source inventory')
 for n,src in bundle['sources'].items():
  need(sha(literal(src))==SPEC['sources'][n],'source pin')
  need((FIXTURES/'research/coupling-lifecycle'/n).read_bytes()==literal(src),'readable source')
 return {'w3a':literal(bundle['w3a']),'captures':[{'rows':[json.loads(s) for s in literal(c).splitlines()],'preload':literal(c,'preload')} for c in bundle['captures']]}
def audit_loaded(data):
 frozen=SPEC['sources']
 need(len(data['captures'])==3,'capture cardinality')
 expected=struct.pack('<II',2,2)
 for a in (b'Acoa',b'Acoh'):expected+=a+bytes(4)+struct.pack('<I',1)+b'aran'+struct.pack('<IIII',2,1,0,0x43a68000)+bytes(4)
 expected+=struct.pack('<I',0)
 w3a=data['w3a']
 need(w3a==expected,'exact authored333 overrides')
 streams=[];projections=[]
 for index,mode in enumerate(['observe1','observe2','control1']):
  rows=data['captures'][index]['rows'];m=rows[0];env=m['env']
  need(m['event']=='metadata' and m['task']==SPEC['metadata_task'],'preserved metadata label')
  need(m['owned'] is True and m['sha256']==DLL and env in ['B','C'],'binary ownership')
  need(m['display']=={'B':':98','C':':99'}[env] and m['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[env],'environment')
  need(m['map']=='Maps/TargetCoupling178.w3m' and m['prefix']=='COUP ' and m['mode']==('control' if index==2 else 'observe'),'capture scope')
  need(m['source_sha256']=={**{n:frozen[n] for n in ['itemdrop_capture.py','itemdrop_observer.js','itemdrop_make_map.py','itemdrop_probe.j']},'map':MAP},'source capture pins')
  need(not any(r.get('event') in ['error','script-error','trace-failed'] or r.get('type')=='error' for r in rows),'observer errors')
  raw=data['captures'][index]['preload'];s=re.findall(r'call Preload\( "(COUP [^"\r\n]*)" \)',raw.decode())
  need(len(s)==597 and s[-1]=='COUP tick=3849 scene=11 label=complete','public coverage')
  need(rows[-1]==dict(event='preload-file',sha256=sha(raw),markers=597,complete=True),'completion footer');streams.append(s)
  if index==2:
   need(all(r['event'] in CONTROL and 'seq' not in r for r in rows),'unhooked control');continue
  need([r['value'] for r in rows if r['event']=='marker']==['COUP tick=0 scene=-1 label=start']+s,'observer public stream')
  ends=[r for r in rows if r['event']=='trace-end'];need(len(ends)==1 and ends[0]['installed'] is True,'trace installed')
  counts=ends[0]['counts'];need(collections.Counter(r['event'] for r in rows if r['event'] in counts)==collections.Counter(counts),'trace counts')
  need({r['event'] for r in rows}-CONTROL-{'trace-end'}==set(counts),'event inventory')
  projection=[]
  for scene in range(11):
   rs=[r for r in rows if r.get('scene')==scene];p=[r for r in rs if r['event']=='couple-producer-enter'];t=[r for r in rs if r['event']=='target-task'];ready=[r for r in rs if r['event']=='couple-ready'];merge=[r for r in rs if r['event']=='couple-merge']
   orders=[852143,852141]+([852143,852141] if scene==7 else [852141] if scene==8 else [])
   need([r['order'] for r in p]==orders,'producer resume distinction')
   actors={r['actor']['pointer']:r['actor'] for r in p[:2]};need(len(actors)==2 and [r['actor']['rawcode'] for r in p[:2]]==[0x65617263,0x65686970],'actor types')
   need(len(t)==2*len(p) and [r['code'] for r in t]==[0xd018a,0xd0171]*len(p),'tasks')
   for i,row in enumerate(t):
    a=p[i//2]['actor'];other=next(v for k,v in actors.items() if k!=a['pointer'])
    need(row['unit']['pointer']==a['pointer'] and row['unit']['identity']==a['identity'] and row['target']==other['pointer'],'opposing task identities')
    need(a['identity']==actors[a['pointer']]['identity'],'retained actor incarnation')
    need(row['range']==0 and row['caller']==[0x4e3d55,0x4e3d69][i%2],'zero raw task/caller')
   need(len(ready)==(2 if scene in [0,7] else 1 if scene in [8,9,10] else 0),'ready count')
   need(len(merge)==(1 if scene in [0,7] else 0),'merge count')
   for row in ready+merge:
    a=actors[row['actor']['pointer']];other=next(v for k,v in actors.items() if k!=a['pointer'])
    need(row['actor']['identity']==a['identity'] and row['partner']==other['identity'] and row['range']==0x43a68000,'ready incarnation/range')
   if merge:need(len({r['actor']['pointer'] for r in ready})==2 and max(rs.index(r) for r in ready)<rs.index(merge[0]),'two ready before merge')
   if scene in [9,10]:
    before=next(r for r in rs if r['event']=='marker' and 'label=before-interrupt' in r['value'])
    need(ready[0]['actor']['rawcode']==0x65686970 and rs.index(ready[0])<rs.index(before),'observed ready window')
   projection.append({'scene':scene,'orders':orders,'ready':[(r['actor']['rawcode'],r['tick']) for r in ready],'merge':[r['tick'] for r in merge]})
  projections.append(projection)
 need(streams[0]==streams[1]==streams[2],'exact complete public repeats/control')
 need(projections[0]==projections[1],'normalized producer repeats')
 need(sha('\n'.join(streams[0]).encode())==SPEC['public_sha256'],'fixed public projection')
 parsed=[parse(s) for s in streams[0]];summary=[]
 labels=['control','stop-archer','stop-hippo','move-archer','move-hippo','remove-archer','remove-hippo','pause-archer','pause-hippo','stop-archer','stop-hippo']
 for scene in range(11):
  p=[r for r in parsed if r['scene']==str(scene)];get=lambda label:next(r for r in p if r['label']==label)
  order=get('order-return');end=get('settled');action=get(labels[scene]);before=get('before-interrupt')
  need(order['accepted']==order['aa']==order['ha']==order['reht']=='1' and (order['ah'],order['hh'])==('852143','852141'),'public initial admission')
  if scene in [0,7]:need(end['riders']=='1' and float(end['al'])==float(end['hl'])==0,'merged')
  else:need(end['riders']=='0','cancelled or waiting without merge')
  if scene in [1,2,9,10]:
   need(action['ah']==action['hh']==end['ah']==end['hh']=='0','Stop both heads')
   need(all(xy(action,w)==xy(before,w)==xy(end,w) for w in ['a','h']),'Stop positions')
  if scene in [3,4]:
   moved,other=('a','h') if scene==3 else ('h','a')
   need(action[moved+'h']=='851986' and action[other+'h']=='0','replacement owner')
   need(xy(action,other)==xy(end,other) and xy(action,moved)!=xy(end,moved),'replacement vs stopped partner')
  if scene in [5,6]:
   removed,other=('a','h') if scene==5 else ('h','a')
   need(action[removed+'t']=='0' and action[other+'h']==end[other+'h']=='0' and xy(action,other)==xy(end,other),'removed partner inverse')
  if scene in [7,8]:
   paused,other=('a','h') if scene==7 else ('h','a')
   need(action[paused+'h']=='851973' and action[other+'h']=='0','pause drops reciprocal head')
   need(all(xy(action,w)==xy(get('before-resume'),w)==xy(get('resume'),w) for w in ['a','h']),'pause positions')
   if scene==8:
    need(get('resume')['tick']==str(SPEC['bounded_reciprocal_pause']['resume_tick']) and end['tick']==str(SPEC['bounded_reciprocal_pause']['settled_tick']),'bounded pause window')
    need(end['ah']=='0' and end['hh']=='852141' and xy(end,'a')==xy(action,'a'),'reciprocal resume waits alone')
  summary.append({'scene':scene,'action':action,'settled':end})
 return {'passed':True,'engine_parity':False,'remaining_owner':'TARGET-01.2','markers_per_capture':597,'matching_public_markers':1791,'scope':'Public-added Acoa/Acoh ownership only; bounded reciprocal-pause wait, not permanent deadlock claim.','time_caveat':'Hook tick is last public marker; clock counters preserved in raw captures.','projection':projections[0],'scenes':summary}

def audit(bundle=None):
 if bundle is None:
  raw=BUNDLE.read_bytes();need(sha(raw)==SPEC['bundle_sha256'],'compressed bundle digest')
  bundle=json.loads(gzip.decompress(raw))
 return audit_loaded(unpack(bundle))
