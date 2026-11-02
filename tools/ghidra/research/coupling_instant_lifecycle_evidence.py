import json,re,hashlib,collections,struct,base64,gzip
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
R=ROOT/'tools/ghidra/fixtures'
MAP='06615af5d73f58ae2258947140eb3055a491a575ec97ec96d545d4ce56a949ac'
DLL='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
CONTROL={'metadata','owned-loading-input','owned-loading-input-skipped','loading-key','control-start-file','preload-file'}
def need(x,m):
 if not x:raise ValueError(m)
def sha(x):return hashlib.sha256(x).hexdigest()
def parse(s):return dict(re.findall(r'(\w+)=([^ ]+)',s))
def audit_semantics(files):
 f=json.loads(files['frozen-sources.json'])
 for n,h in f.items():need(sha(files[n])==h,'freeze '+n)
 w3a=files['war3map.w3a']
 need(w3a==struct.pack('<III',2,0,0),'no ability overrides')
 streams=[];projections=[]
 for mode in ['observe1','observe2','control1']:
  rows=[json.loads(x) for x in files[mode+'.jsonl'].decode().splitlines()];m=rows[0];e=m['env']
  need(m['owned'] is True and m['sha256']==DLL and e in ['B','C'],'original owned binary')
  need(m['display']=={'B':':98','C':':99'}[e] and m['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[e],'environment')
  need(m['task']=='TARGET-01.2-coupling-instant193-lifecycle' and m['prefix']=='CPI ' and m['mode']==('control' if mode=='control1' else 'observe'),'metadata')
  need(m['source_sha256']=={**{n:f[n] for n in ['itemdrop_capture.py','itemdrop_observer.js','itemdrop_make_map.py','itemdrop_probe.j']},'map':MAP},'source/map')
  need(not any(x.get('event') in ['trace-failed','error','script-error'] or x.get('type')=='error' for x in rows),'errors')
  raw=files[mode+'-preload.txt'];s=re.findall(r'call Preload\( "(CPI [^"\r\n]*)" \)',raw.decode());streams.append(s)
  need(len(s)==2902 and s[-1]=='CPI tick=4399 scene=11 label=complete','all scenes')
  need(rows[-1]==dict(event='preload-file',sha256=sha(raw),markers=2902,complete=True),'footer')
  if mode=='control1':need(all(x['event'] in CONTROL for x in rows),'unhooked whitelist');continue
  need([x['value'] for x in rows if x['event']=='marker']==['CPI tick=0 scene=-1 label=start']+s,'hook/preload equivalence')
  ends=[x for x in rows if x['event']=='trace-end'];need(len(ends)==1 and ends[0]['installed'] is True,'installed')
  counts=ends[0]['counts'];need(collections.Counter(x['event'] for x in rows if x['event'] in counts)==collections.Counter(counts),'exact trace counts')
  need({x['event'] for x in rows}-CONTROL-{'trace-end'}==set(counts),'event inventory')
  proj=[]
  for scene in range(11):
   rs=[x for x in rows if x.get('scene')==scene];ps=[x for x in rs if x['event']=='instant-producer-enter'];ts=[x for x in rs if x['event']=='target-task'];terms=[x for x in rs if x['event']=='instant-terminal'];coords=[x for x in rs if x['event']=='instant-coordinate'];leaves=[x for x in rs if x['event']=='instant-producer-leave']
   expected=[0x65617263,0x65686970]+([0x65686970] if scene==8 else [])
   need([x['ability']['actor']['rawcode'] for x in ps]==expected,'initial pair and exact pause replay role')
   need(len(ts)==len(ps)==len(leaves) and len(coords)==1,'constructor/task/coordinate counts')
   actors={x['ability']['actor']['pointer']:x['ability']['actor'] for x in ps[:2]};need(len(actors)==2 and len({tuple(x['identity']) for x in actors.values()})==2,'two actor incarnations')
   pp=[]
   for i,(p,t,leave) in enumerate(zip(ps,ts,leaves)):
    a=p['ability'];u=a['actor'];peer=actors[p['target']];d=a['data'];arch=u['rawcode']==0x65617263
    need(peer['pointer']!=u['pointer'] and {peer['rawcode'],u['rawcode']}=={0x65617263,0x65686970},'opposite actor')
    need(a['rank']==0 and len(d)==18 and d[0]==0x300000 and d[6]==0x44610000 and d[7]==(0x47c34f80 if arch else 0) and d[9]==(0 if arch else 0x3f800000) and d[17]==0x65687072,'actual cache/role')
    need(t['unit']['pointer']==u['pointer'] and t['unit']['identity']==u['identity']==actors[u['pointer']]['identity'] and t['target']==peer['pointer'],'task incarnation/target')
    need(t['code']==0xd0174 and t['range']==(0x7f7fffff if arch else 0) and t['caller']==0x40aa2a,'raw physical selector/range/caller')
    need(p['caller']==(0x423e67 if i==0 else 0x40a77f),'producer caller')
    need(leave['ability']['identity']==a['identity'] and leave['ability']['retained']==peer['identity'] and rows.index(p)<rows.index(t)<rows.index(leave),'published retained target')
    pp.append({'actor':u['rawcode'],'tick':p['tick'],'caller':p['caller'],'range':t['range'],'counter':p['clock']['counter']})
   need([x['ability']['actor']['rawcode'] for x in terms]==([0x65617263,0x65686970] if scene in [0,8] else [0x65617263]),'only surviving merge role reaches terminal')
   tp=[]
   for z in terms:
    a=z['ability'];u=a['actor'];peer=next(x for x in actors.values() if x['pointer']!=u['pointer'])
    need(u['identity']==actors[u['pointer']]['identity'] and z['target']['identity']==peer['identity'] and a['retained']==peer['identity'],'terminal incarnation/retained pair')
    need(z['caller']==0x40a3af,'terminal caller');tp.append({'actor':u['rawcode'],'tick':z['tick'],'counter':z['clock']['counter']})
   before=next(x for x in rs if x['event']=='marker' and ' label=before-intervene-a ' in x['value'])
   need(rows.index(terms[0])<rows.index(before),'Archer wait entered before interruption')
   issued=[x for x in rs if x['event']=='marker' and ' label=issued-' in x['value']]
   need(rows.index(issued[0])<rows.index(ps[0])<rows.index(ts[0])<rows.index(issued[1])<rows.index(coords[0])<rows.index(issued[2])<rows.index(ps[1])<rows.index(ts[1]),'initial event/producer chronology')
   if scene==8:
    resumed=next(x for x in issued if x['tick']==3416);need(rows.index(resumed)<rows.index(ps[2]) and ps[2]['tick']==3416,'resumed peer event before constructor')
   for x in rs:
    if x['event']=='instant-inverse':
     a=x['ability'];u=a['actor'];initial=next(v['ability'] for v in ps[:2] if v['ability']['actor']['pointer']==u['pointer'])
     need(u['identity']==initial['actor']['identity'] and a['identity']==initial['identity'],'inverse actor and ability incarnations')
   inverse=[{'actor':x['ability']['actor']['rawcode'],'tick':x['tick'],'caller':x['caller'],'counter':x['clock']['counter']} for x in rs if x['event']=='instant-inverse']
   proj.append({'scene':scene,'producers':pp,'terminals':tp,'inverses':inverse})
  projections.append(proj)
 need(streams[0]==streams[1]==streams[2],'exact public repeat/control')
 need(projections[0]==projections[1],'exact scoped native projection')
 parsed=[]
 for line in streams[0]:
  x=parse(line);label=x['label'];suffix=label.rsplit('-',1)[-1]
  need(len(line)<=160 and 'length-error' not in line and 'precondition-error' not in line,'public length guard')
  if suffix in ['a','h','setup','result']:
   label=label.rsplit('-',1)[0];x['label']=label
   if suffix=='a':parsed.append(x)
   else:
    need(parsed[-1]['tick']==x['tick'] and parsed[-1]['scene']==x['scene'] and parsed[-1]['label']==label,'split state continuity');parsed[-1].update(x)
  else:parsed.append(x)
 scenes=[]
 tails=[
 [('issued-immediate',851972,1,0)],
 [('issued-immediate',851972,1,0),('issued-immediate',851972,2,0),('issued-immediate',851972,1,0)],
 [('issued-immediate',851972,1,0),('issued-immediate',851972,2,0)],
 [('issued-immediate',851972,1,0),('issued-immediate',851972,2,0),('issued-point',851986,1,0)],
 [('issued-immediate',851972,1,0),('issued-point',851986,2,0)],
 [('issued-immediate',851972,1,0),('issued-target',852507,2,1)],
 [('issued-immediate',851972,1,0)],
 [('issued-immediate',851972,1,0),('issued-immediate',851972,2,0),('issued-point',851973,1,0)],
 [('issued-immediate',851972,1,0),('issued-point',851973,2,0),('issued-target',852507,2,1)],
 [('issued-immediate',851972,2,0)],
 [('issued-immediate',851972,1,0)]]
 labels=['control','stop-a','stop-h','move-a','move-h','remove-a','remove-h','pause-a','pause-h','remove-Aco2','remove-Aco3']
 for scene in range(11):
  rs=[x for x in parsed if x['scene']==str(scene)];get=lambda label:next(x for x in rs if x['label']==label)
  spawn=get('spawn');before=get('before-intervene');after=get(labels[scene]);end=get('settled');ret=get('order-return');events=[x for x in rs if x['label'].startswith('issued-')]
  need(spawn['aa']==spawn['ha']==spawn['reht']==ret['accepted']==after['accepted']=='1','natural availability and public operation accepted')
  need(before['at']==str(0x65617263) and before['ht']==str(0x65686970) and before['ah']=='852508' and before['hh']=='852507' and float(before['al'])>0 and float(before['hl'])>0 and before['riders']=='0' and before['aa']==before['ha']==before['reht']=='1' and before['pa']==before['ph']=='0','both active live heads before intervention')
  need(end['riders']==('1' if scene in [0,8] else '0'),'control/pause-role merge outcomes')
  need(end['ah']==end['hh']=='0','bounded settled heads')
  seq=[(x['label'],int(x['oid']),int(x['actor']),int(x['target'])) for x in events]
  need(seq==[('issued-immediate',852508,1,0),('issued-target',852508,1,2),('issued-target',852507,2,1)]+tails[scene],'exact role-specific issued event sequence')
  need([(int(x['ah']),int(x['hh'])) for x in events[:3]]==[(852508,0),(852508,0),(852508,852507)],'heads during initial public events')
  if scene in [1,2]:need(after['ah']==after['hh']=='0','Stop clears both logical heads')
  if scene==3:need(after['ah']=='851986' and after['hh']=='0' and float(end['ax'])<float(before['ax']),'Archer Move retained and travels')
  if scene==4:need(after['hh']=='851986' and after['ah']=='0' and float(end['hx'])>float(before['hx']),'Hippo Move retained and travels')
  if scene==5:need(end['at']=='0' and end['ht']==str(0x65686970),'Archer removal completes while peer survives')
  if scene==6:need(end['ht']=='0' and end['at']==str(0x65617263),'Hippo removal completes while peer survives')
  if scene in [7,8]:
   role='a' if scene==7 else 'h';other='h' if scene==7 else 'a';resume=get('resume-'+role);pr=get('before-resume')
   need(after['p'+role]=='1' and after['p'+other]=='0' and resume['p'+role]=='0','pause flag composition')
   need(after[role+'h']=='851973' and after[other+'h']=='0' and pr[role+'h']=='851973','pause head and peer inverse')
   need((after[role+'x'],after[role+'y'])==(pr[role+'x'],pr[role+'y']),'paused actor does not translate')
  if scene==9:need(after['aa']=='0' and after['ha']=='1' and after['ah']==after['hh']=='0' and end['aa']=='0','Aco2 detach clears own and peer')
  if scene==10:need(after['ha']=='0' and after['aa']=='1' and after['ah']=='0' and after['hh']=='852507' and float(end['hx'])<float(after['hx']) and end['ha']=='0','Aco3 detach retains physical approach without merge')
  scenes.append({'scene':scene,'events':events,'before':before,'after':after,'settled':end})
 return {'passed':True,'markers_per_capture':2902,'matching_public_markers':8706,'public_stream_sha256':sha(('\n'.join(streams[0])+'\n').encode()),'projection':projections[0],'scenes':scenes,'limitations':['All pauses and waits are bounded through age398; no permanent-state claim.','Pause uses stock authored roles, not legacy paired-ready replay.','Issued target role0 means neither watched actor, not proven null.','No nested callback mutation, save/load, HP transfer or dismount witness.','Aco3 removal preserves physical travel while ability is absent; terminal callback does not execute and no rider is created.']}
SPEC=R/'retail-coupling-instant-lifecycle-1.27.json'
BUNDLE=R/'retail-coupling-instant-lifecycle-1.27.json.gz'
def unpack(bundle):
 need(bundle.get('schema')==1 and bundle.get('engine_parity') is False,'bundle scope')
 return {n:base64.b64decode(b,validate=True) for n,b in bundle['files'].items()}
def audit(bundle=None):
 spec=json.loads(SPEC.read_text())
 need(spec['schema']==1 and spec['engine_parity'] is False,'spec scope')
 if bundle is None:
  raw=BUNDLE.read_bytes();need(sha(raw)==spec['compressed_sha256'],'compressed bundle digest')
  bundle=json.loads(gzip.decompress(raw))
 files=unpack(bundle)
 need(set(files)==set(spec['files_sha256']),'literal inventory')
 for name,pin in spec['files_sha256'].items():need(sha(files[name])==pin,'literal digest '+name)
 for name in spec['readable_sources']:need((R/'research/coupling-instant-lifecycle'/name).read_bytes()==files[name],'readable source '+name)
 result=audit_semantics(files)
 audit_schema(files)
 audit_map(files)
 need(result['matching_public_markers']==spec['public_records'] and result['public_stream_sha256']==spec['public_stream_sha256'],'public projection pin')
 return dict(observations=2,controls=1,public_records=8706,engine_parity=False,remaining_owner='TARGET-01.2')

def audit_schema(files):
 # Independently require complete ordered split state records and all setup labels.
 for name in ('observe1','observe2','control1'):
  lines=re.findall(r'call Preload\( "(CPI [^"\r\n]*)" \)',files[name+'-preload.txt'].decode())
  idx=states=data=speeds=0
  while idx<len(lines):
   p=parse(lines[idx]);label=p['label']
   if label=='stock-data':
    need([p[k] for k in ('arange','hrange','area','ab','hb','targs')]==['99999','0','900','0','1','vuln,invu'],'stock data labels');data+=1
   if label=='spawn-speed':need(p['as']=='270.000' and p['hs']=='400.000','stock speeds');speeds+=1
   if label.endswith('-a'):
    four=[parse(s) for s in lines[idx:idx+4]];base=label[:-2]
    need([x['label'] for x in four]==[base+'-'+x for x in ('a','h','setup','result')],'split schema order')
    need(len({(x['scene'],x['tick']) for x in four})==1,'split schema identity')
    merged={k:v for x in four for k,v in x.items()}
    need(set(['accepted','reht','ah','at','ax','ay','af','al','aa','hh','ht','hx','hy','hf','hl','ha','riders','rx','ry','pa','ph'])<=set(merged),'complete state fields')
    states+=1;idx+=4
   else:
    need(not any(label.endswith('-'+suffix) for suffix in ('h','setup','result')),'orphan split record');idx+=1
  need(states==706 and data==speeds==11,'setup/state counts')
def audit_map(files):
 b=files['war3map.w3u'];pos=0
 def integer():
  nonlocal pos
  value=struct.unpack_from('<I',b,pos)[0];pos+=4;return value
 def raw():
  nonlocal pos
  value=b[pos:pos+4];pos+=4;return value
 need(integer()==2,'w3u version')
 for group in range(2):
  for _ in range(integer()):
   old,new=raw(),raw();need(old not in (b'earc',b'ehip',b'ehpr') and new not in (b'earc',b'ehip',b'ehpr'),'stock unit override')
   for _ in range(integer()):
    field=raw();kind=integer();need(field!=b'uabi','stock ability-list override')
    if kind==3:pos=b.index(0,pos)+1
    else:need(kind in (0,1,2),'w3u field kind');pos+=4
    raw()
 need(pos==len(b),'w3u exact end')
 names=files['(listfile)'].decode().lower().replace(chr(92),'/').splitlines()
 need(not any(n.endswith(('unitabilities.slk','abilitydata.slk','nightelfabilityfunc.txt')) for n in names),'map archive override')

if __name__=='__main__':print(json.dumps(audit(),sort_keys=True))
