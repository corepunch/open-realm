import json,re,hashlib,collections,struct,subprocess
from pathlib import Path
R=Path(__file__).resolve().parent
MAP='26e782a01c732ef12f2a5f27c47ec9b624706119a3afbee08d68c6d907829378'
DLL='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
CONTROL={'metadata','owned-loading-input','owned-loading-input-skipped','loading-key','control-start-file','preload-file'}
def need(x,m):
 if not x:raise ValueError(m)
def sha(x):return hashlib.sha256(x).hexdigest()
def parse(s):return dict(re.findall(r'(\w+)=([^ ]+)',s))
def audit():
 f=json.loads((R/'frozen-sources.json').read_text())
 for n,h in f.items():need(sha((R/n).read_bytes())==h,'freeze '+n)
 w3a=subprocess.check_output(['/run/media/lofcz/ssd_external/GitHub/open-realm/build/bin/mpqtool','-mpq',str(R/'TargetCouplingInstant193Callbacks.w3m'),'cat','war3map.w3a'])
 need(w3a==struct.pack('<III',2,0,0),'no ability overrides')
 streams=[];projections=[]
 for mode in ['observe1','observe2','control1']:
  rows=[json.loads(x) for x in (R/(mode+'.jsonl')).read_text().splitlines()];m=rows[0];e=m['env']
  need(m['owned'] is True and m['sha256']==DLL and e in ['B','C'],'original owned binary')
  need(m['display']=={'B':':98','C':':99'}[e] and m['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[e],'environment')
  need(m['task']=='TARGET-01.2-coupling-instant193-callbacks' and m['prefix']=='CPI ' and m['mode']==('control' if mode=='control1' else 'observe'),'metadata')
  need(m['source_sha256']=={**{n:f[n] for n in ['itemdrop_capture.py','itemdrop_observer.js','itemdrop_make_map.py','itemdrop_probe.j']},'map':MAP},'source/map')
  need(not any(x.get('event') in ['trace-failed','error','script-error'] or x.get('type')=='error' for x in rows),'errors')
  raw=(R/(mode+'-preload.txt')).read_bytes();s=re.findall(r'call Preload\( "(CPI [^"\r\n]*)" \)',raw.decode());streams.append(s)
  need(len(s)==1625 and s[-1]=='CPI tick=2399 scene=6 label=complete','all scenes')
  need(rows[-1]==dict(event='preload-file',sha256=sha(raw),markers=1625,complete=True),'footer')
  if mode=='control1':need(all(x['event'] in CONTROL for x in rows),'unhooked whitelist');continue
  need([x['value'] for x in rows if x['event']=='marker']==['CPI tick=0 scene=-1 label=start']+s,'hook/preload equivalence')
  ends=[x for x in rows if x['event']=='trace-end'];need(len(ends)==1 and ends[0]['installed'] is True,'installed')
  counts=ends[0]['counts'];need(collections.Counter(x['event'] for x in rows if x['event'] in counts)==collections.Counter(counts),'exact trace counts')
  need({x['event'] for x in rows}-CONTROL-{'trace-end'}==set(counts),'event inventory')
  proj=[]
  for scene in range(6):
   rs=[x for x in rows if x.get('scene')==scene];ps=[x for x in rs if x['event']=='instant-producer-enter'];ts=[x for x in rs if x['event']=='target-task'];terms=[x for x in rs if x['event']=='instant-terminal'];coords=[x for x in rs if x['event']=='instant-coordinate'];leaves=[x for x in rs if x['event']=='instant-producer-leave']
   expected=[] if scene==5 else ([0x65617263] if scene==2 else [0x65617263,0x65686970])
   need([x['ability']['actor']['rawcode'] for x in ps]==expected,'initial pair and exact pause replay role')
   need(len(ts)==len(ps)==len(leaves) and len(coords)==(0 if scene==5 else 1),'constructor/task/coordinate counts')
   actors={x['ability']['actor']['pointer']:x['ability']['actor'] for x in ps[:2]};
   for x in rs:
    if x['event']=='instant-admission':actors[x['ability']['actor']['pointer']]=x['ability']['actor'];actors[x['target']['pointer']]=x['target']
   need(len(actors)==2 and len({tuple(x['identity']) for x in actors.values()})==2,'actor inventory')
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
   need([x['ability']['actor']['rawcode'] for x in terms]==([0x65617263,0x65686970] if scene==0 else ([0x65686970] if scene==1 else ([] if scene==5 else [0x65617263]))),'only surviving merge role reaches terminal')
   tp=[]
   for z in terms:
    a=z['ability'];u=a['actor'];peer=next(x for x in actors.values() if x['pointer']!=u['pointer'])
    need(u['identity']==actors[u['pointer']]['identity'] and z['target']['identity']==peer['identity'] and a['retained']==peer['identity'],'terminal incarnation/retained pair')
    need(z['caller']==0x40a3af,'terminal caller');tp.append({'actor':u['rawcode'],'tick':z['tick'],'counter':z['clock']['counter']})
   issued=[x for x in rs if x['event']=='marker' and ' label=issued-' in x['value']]
   if scene!=5:
    need(rows.index(issued[0])<rows.index(ps[0])<rows.index(ts[0])<rows.index(issued[1])<rows.index(coords[0]),'auto event/producer chronology')
    if scene in [0,3,4]:need(rows.index(coords[0])<rows.index(issued[2])<rows.index(ps[1]),'reciprocal publication before constructor')
    if scene==1:need(rows.index(issued[2])<rows.index(coords[0])<rows.index(issued[3])<rows.index(ps[1]),'nested source Move precedes continued coordination')
   for x in rs:
    if x['event']=='instant-inverse':
     a=x['ability'];u=a['actor'];need(u['pointer'] in actors and u['identity']==actors[u['pointer']]['identity'],'inverse actor incarnation')
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
 base=[('issued-immediate',852508,1,0),('issued-target',852508,1,2),('issued-target',852507,2,1)]
 expected=[base+[('issued-immediate',851972,1,0)],base[:2]+[('issued-point',851986,1,0),base[2]],base+[('issued-point',851986,2,0)],base+[('issued-immediate',851972,1,0),('issued-immediate',851972,2,0),('issued-point',851986,1,0),('issued-immediate',851972,1,0)],base+[('issued-immediate',851972,1,0),('issued-point',851986,1,0),('issued-immediate',851972,2,0)],[base[0],('issued-point',851986,1,0)]]
 for scene in range(6):
  rs=[x for x in parsed if x['scene']==str(scene)];get=lambda label:next(x for x in rs if x['label']==label)
  spawn=get('spawn');end=get('settled');ret=get('order-return');events=[x for x in rs if x['label'].startswith('issued-')];death=[x for x in rs if x['label']=='death']
  need(spawn['aa']==spawn['ha']==spawn['reht']==ret['accepted']=='1','natural availability and admitted request')
  need(end['riders']==('1' if scene in [0,1] else '0'),'merge outcomes')
  need([(x['label'],int(x['oid']),int(x['actor']),int(x['target'])) for x in events]==expected[scene],'exact callback event sequence')
  need([int(x['actor']) for x in death]==([2,1] if scene in [0,1] else []),'public death order')
  if death:
   states=[x for x in rs if x['label']=='death-state'];need(len(states)==2 and all(x['at']==str(0x65617263) and x['ht']==str(0x65686970) and x['hl']=='0.000' for x in states) and states[0]['al']=='310.000' and states[1]['al']=='0.000','sequential death states retain types; caster zero precedes target zero')
   first=next(x for x in rs if x.get('riders')=='1');zero=next(x for x in rs if x.get('riders')=='1' and x.get('at')==x.get('ht')=='0');need(int(zero['tick'])>int(first['tick']),'deferred handle retirement after result')
  contexts=[x for x in rs if x['label']=='after-nested-context']
  need(len(contexts)==(0 if scene==0 else 1),'exact once nested callback')
  if contexts:need(contexts[0]['sameactor']=='1' and int(contexts[0]['oid'])=={1:852508,2:852507,3:851972,4:851972,5:852508}[scene],'outer event response restored')
  if scene==1:need(ret['ah']=='851986' and ret['hh']=='852507','selected-target replacement survives continued peer dispatch')
  if scene==2:need(ret['ah']=='852508' and ret['hh']=='851986' and end['ah']=='852508' and end['hh']=='0' and float(end['hx'])>1700,'reciprocal nested replacement leaves lone source wait')
  if scene==3:
   stop=get('outer-stop-a');need(stop['ah']==stop['hh']=='0' and end['ax']==spawn['ax'] and end['ay']==spawn['ay'],'outer source Stop supersedes nested source Move')
  if scene==4:
   stop=get('outer-stop-h');need(stop['ah']=='851986' and stop['hh']=='0' and float(end['ax'])<float(spawn['ax']),'outer peer Stop preserves nested source Move')
  if scene==5:need(ret['ah']=='851986' and ret['hh']=='0' and float(end['ax'])<float(spawn['ax']) and end['hx']==spawn['hx'],'initial immediate replacement suppresses producer')
  scenes.append({'scene':scene,'events':events,'contexts':contexts,'death_events':death,'return':ret,'settled':end})
 return {'passed':True,'markers_per_capture':1625,'matching_public_markers':4875,'public_stream_sha256':sha(('\n'.join(streams[0])+'\n').encode()),'projection':projections[0],'scenes':scenes,'limitations':['Nested Move only, not callback removal/owner/ability mutation.','Restored GetIssuedOrderId and GetTriggerUnit measured; target-response restoration not measured.','Death order Hippo then Archer observed in these two merge scenes; no universal death-order claim.','Deferred original lifetime sampled, not exact reclamation timestamp.','Lone source waiting bounded through age398.']}
if __name__=='__main__':
 o=audit()
 with (R/'validated-summary.json').open('x') as f:json.dump(o,f,indent=2)
 print({k:v for k,v in o.items() if k not in ['projection','scenes']})
