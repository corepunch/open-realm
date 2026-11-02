import json,re,hashlib,collections,struct,base64,gzip
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
R=ROOT/'tools/ghidra/fixtures'
MAP='3a229f1c103dc69597444bdbac63fd664591ba17da205765ea1d66a9ba96774d'
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
  need(m['task']=='TARGET-01.2-coupling-instant193' and m['prefix']=='CPI ' and m['mode']==('control' if mode=='control1' else 'observe'),'metadata')
  need(m['source_sha256']=={**{n:f[n] for n in ['itemdrop_capture.py','itemdrop_observer.js','itemdrop_make_map.py','itemdrop_probe.j']},'map':MAP},'source/map')
  need(not any(x.get('event') in ['trace-failed','error','script-error'] or x.get('type')=='error' for x in rows),'errors')
  raw=files[mode+'-preload.txt'];s=re.findall(r'call Preload\( "(CPI [^"\r\n]*)" \)',raw.decode());streams.append(s)
  need(len(s)==1539 and s[-1]=='CPI tick=2399 scene=6 label=complete','all scenes')
  need(rows[-1]==dict(event='preload-file',sha256=sha(raw),markers=1539,complete=True),'footer')
  if mode=='control1':need(all(x['event'] in CONTROL for x in rows),'unhooked whitelist');continue
  need([x['value'] for x in rows if x['event']=='marker']==['CPI tick=0 scene=-1 label=start']+s,'hook/preload equivalence')
  ends=[x for x in rows if x['event']=='trace-end'];need(len(ends)==1 and ends[0]['installed'] is True,'installed')
  counts=ends[0]['counts'];need(collections.Counter(x['event'] for x in rows if x['event'] in counts)==collections.Counter(counts),'exact trace counts')
  need({x['event'] for x in rows}-CONTROL-{'trace-end'}==set(counts),'event inventory')
  proj=[]
  for scene in range(6):
   rs=[x for x in rows if x.get('scene')==scene];ps=[x for x in rs if x['event']=='instant-producer-enter'];ts=[x for x in rs if x['event']=='target-task'];terms=[x for x in rs if x['event']=='instant-terminal'];coords=[x for x in rs if x['event']=='instant-coordinate']
   expected=[0x65617263,0x65686970] if scene<2 else ([0x65617263] if scene==4 else [0x65686970])
   need([x['ability']['actor']['rawcode'] for x in ps]==expected,'producer actors')
   need(len(ts)==len(ps)==len(terms),'one target approach/terminal per producer')
   need(len(coords)==(1 if scene<4 else 0),'coordination only immediate')
   actors={x['ability']['actor']['pointer']:x['ability']['actor'] for x in rs if x['event']=='instant-admission'}
   actors.update({x['target']['pointer']:x['target'] for x in rs if x['event']=='instant-admission'})
   need(len(actors)==2 and len({tuple(x['identity']) for x in actors.values()})==2,'two original actor incarnations')
   leaves=[x for x in rs if x['event']=='instant-producer-leave']
   need(len(leaves)==len(ps),'producer leaves')
   projection=[]
   for p,t,z in zip(ps,ts,terms):
    a=p['ability'];u=a['actor'];peer=actors[p['target']];data=a['data'];arch=u['rawcode']==0x65617263
    need(peer['pointer']!=u['pointer'] and {peer['rawcode'],u['rawcode']}=={0x65617263,0x65686970},'opposite actor')
    need(a['rank']==0 and len(data)==18,'rank/cache layout')
    need(data[0]==0x300000 and data[6]==0x44610000 and data[7]==(0x47c34f80 if arch else 0) and data[9]==(0 if arch else 0x3f800000) and data[17]==0x65687072,'actual targs/Area/Rng/DataB/UnitID')
    need(t['unit']['identity']==u['identity'] and t['unit']['pointer']==u['pointer'] and t['target']==peer['pointer'],'task identities')
    need(t['code']==0xd0174 and t['range']==(0x7f7fffff if arch else 0) and t['caller']==0x40aa2a,'task raw arguments/caller')
    need(z['ability']['identity']==a['identity'] and z['ability']['actor']['identity']==u['identity'] and z['target']['identity']==peer['identity'] and z['ability']['retained']==peer['identity'],'terminal identities')
    leave=next(x for x in leaves if x['ability']['identity']==a['identity'])
    need(leave['ability']['retained']==peer['identity'] and rows.index(t)<rows.index(leave)<rows.index(z),'producer leave ownership')
    need(z['caller']==0x40a3af and rows.index(p)<rows.index(t)<rows.index(z),'terminal caller/order')
    projection.append({'actor':u['rawcode'],'producer_caller':p['caller'],'task_range':t['range'],'task_caller':t['caller'],'terminal_tick':z['tick'],'terminal_counter':z['clock']['counter']})
   need([x['producer_caller'] for x in projection]==([0x423e67,0x40a77f] if scene<2 else ([0x423e67] if scene<4 else [0x40a77f])),'entry callers')
   issued=[x for x in rs if x['event']=='marker' and ' label=issued-' in x['value']]
   if scene<4:
    need(rows.index(issued[0])<rows.index(ps[0])<rows.index(ts[0])<rows.index(issued[1])<rows.index(coords[0]),'automatic selected target publication after own task')
    if scene<2:need(rows.index(coords[0])<rows.index(issued[2])<rows.index(ps[1])<rows.index(ts[1]),'reciprocal publication before peer task')
   else:need(rows.index(issued[0])<rows.index(ps[0])<rows.index(ts[0]),'explicit publication before task')
   proj.append(projection)
  projections.append(proj)
 need(streams[0]==streams[1]==streams[2],'exact public repeat/control')
 need(projections[0]==projections[1],'exact scoped native projection')
 parsed=[]
 for line in streams[0]:
  x=parse(line);label=x['label'];suffix=label.rsplit('-',1)[-1]
  need(len(line)<=160 and 'length-error' not in line,'public length guard')
  if suffix in ['a','h','setup','result']:
   label=label.rsplit('-',1)[0];x['label']=label
   if suffix=='a':parsed.append(x)
   else:
    need(parsed[-1]['tick']==x['tick'] and parsed[-1]['scene']==x['scene'] and parsed[-1]['label']==label,'split state continuity');parsed[-1].update(x)
  else:parsed.append(x)
 scenes=[]
 for scene in range(6):
  rs=[x for x in parsed if x['scene']==str(scene)];get=lambda label:next(x for x in rs if x['label']==label)
  spawn=get('spawn');end=get('settled');ret=next(x for x in rs if x['label'].endswith('-return'));events=[x for x in rs if x['label'].startswith('issued-')]
  need(spawn['aa']==spawn['ha']==spawn['reht']==ret['accepted']=='1','natural abilities and admission')
  need(end['riders']==('0' if scene==4 else '1'),'merge outcomes')
  samples=[x for x in rs if 'ax' in x and x['at']==str(0x65617263) and float(x['al'])>0]
  need(all(x['ax']==spawn['ax'] and x['ay']==spawn['ay'] for x in samples),'Archer does not translate')
  if scene==4:need(end['ah']=='852507' and end['hh']=='0' and end['hx']==spawn['hx'] and end['hy']==spawn['hy'],'bounded direct Archer wait')
  seq=[(x['label'],int(x['oid']),int(x['actor']),int(x['target'])) for x in events]
  expected=([('issued-immediate',852508,1,0),('issued-target',852508,1,2),('issued-target',852507,2,1),('issued-immediate',851972,1,0)] if scene<2 else ([('issued-immediate',852508,2,0),('issued-target',852508,2,1)] if scene<4 else [('issued-target',852507,1 if scene==4 else 2,2 if scene==4 else 1)]))
  need(seq==expected,'exact issued event sequence')
  heads=([(852508,0),(852508,0),(852508,852507),(851972,852507)] if scene<2 else ([(0,852508),(0,852508)] if scene<4 else [(852507,0) if scene==4 else (0,852507)]))
  need([(int(x['ah']),int(x['hh'])) for x in events]==heads,'heads during issued events')
  need((int(ret['ah']),int(ret['hh']))==((852508,852507) if scene<2 else ((0,852508) if scene<4 else ((852507,0) if scene==4 else (0,852507)))),'heads at public return')
  scenes.append({'scene':scene,'events':events,'return':ret,'settled':end})
 return {'passed':True,'markers_per_capture':1539,'matching_public_markers':4617,'public_stream_sha256':sha(('\n'.join(streams[0])+'\n').encode()),'projection':projections[0],'scenes':scenes,'limitations':['Direct Archer waiting bounded through age398; no permanent claim.','DataB and Rng stock values correlated; branch attribution additionally supported by static export, not independently mutated.','Result position is public quantized output near terminal Hippo position; exact coordinate transform not claimed.','No lifecycle interruption or reentrant callback cases in this producer witness.','V1 rejected public truncation and v2 was never launched. Accepted capture provenance is stock-v3/frozen-sources.json; prior sources and captures preserved.']}

SPEC=R/'retail-coupling-instant-stock-1.27.json'
BUNDLE=R/'retail-coupling-instant-stock-1.27.json.gz'
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
 for name in spec['readable_sources']:need((R/'research/coupling-instant-stock'/name).read_bytes()==files[name],'readable source '+name)
 result=audit_semantics(files)
 audit_schema(files)
 audit_map(files)
 need(result['matching_public_markers']==spec['public_records'] and result['public_stream_sha256']==spec['public_stream_sha256'],'public projection pin')
 return dict(observations=2,controls=1,public_records=4617,engine_parity=False,remaining_owner='TARGET-01.2')

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
    need(set(['accepted','reht','ah','at','ax','ay','af','al','aa','hh','ht','hx','hy','hf','hl','ha','riders','rx','ry'])<=set(merged),'complete state fields')
    states+=1;idx+=4
   else:
    need(not any(label.endswith('-'+suffix) for suffix in ('h','setup','result')),'orphan split record');idx+=1
  need(states==378 and data==speeds==6,'setup/state counts')
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
