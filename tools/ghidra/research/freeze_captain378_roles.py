#!/usr/bin/env python3
"""Freeze complete, uniformly flushed public cargo/Captain role-mask witnesses."""
import argparse,gzip,hashlib,json,re
from pathlib import Path
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
ADMIN={'metadata','module','owned-loading-input','loading-key','trace-end','owned-detached','preload-file'}
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(path):
 return re.findall(r'call Preload\( "(S323 [^"\r\n]*)" \)',path.read_text())
def capture(path):
 rows=[json.loads(l) for l in path.read_text().splitlines()]
 meta=next(r for r in rows if r['event']=='metadata');end=next(r for r in rows if r['event']=='preload-file')
 assert meta['sha256']==SHA and meta['map']=='Maps/Cargo329assault.w3m'
 assert end.get('complete') and end['markers']==678
 public=markers(path.with_name(path.stem+'-preload.txt'));assert len(public)==678 and ' label=complete' in public[-1]
 if meta['mode']=='control':return dict(mode='control',markers=public,metadata=meta)
 assert [r['value'] for r in rows if r['event']=='marker']==public
 active=False;canonical=[];captains={}
 for row in rows:
  if row['event']=='marker' and ' label=start ' in row['value']:active=True
  if not active or row['event'] in ADMIN:continue
  assert row['event'] not in {'native-exception','error','trace-failed'},row
  row=dict(row)
  if isinstance(row.get('captain'),str):
   key=row['captain'];captains.setdefault(key,len(captains));row['captain']=captains[key]
  canonical.append(row)
  if row['event']=='marker' and ' label=complete ' in row['value']:active=False
 assert not active and len(captains)==1
 assert [r['seq']for r in canonical]==list(range(2,1266)),'raw sequence or complete row order differs'
 return dict(mode='observe',markers=public,records=canonical,metadata=meta,captain_aliases=captains)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 observations=[a.root/f'cargo-exact-observe{i}.jsonl' for i in (1,2)];control=a.root/'cargo-exact-control1.jsonl'
 rows=[capture(p) for p in observations];other=capture(control)
 assert rows[0]['records']==rows[1]['records'],'complete semantic records differ'
 assert rows[0]['markers']==rows[1]['markers']==other['markers'],'public control differs'
 records=rows[0]['records'];wisp=lambda r:r.get('state',{}).get('rawcode')==0x65777370
 registered=[r for r in records if r['event']=='captain-subscription' and wisp(r) and r['enabled']]
 assert len(registered)==1 and [registered[0][k]for k in ('lost','death','owner')]==[0,1,1]
 lost=[r for r in records if r['event']=='cargo-target-lost' and wisp(r) and r['tick']==551]
 assert len(lost)==1 and lost[0]['caller']=='688373'
 callbacks=[r for r in records if r['event']=='captain-target-event-enter' and wisp(r)]
 assert len(callbacks)==1 and callbacks[0]['tick']==620 and callbacks[0]['count']==1
 recenter=[r for r in records if r['event']=='cargo-captain-recenter' and wisp(r)]
 assert len(recenter)==1 and recenter[0]['tick']==579 and recenter[0]['result']==1
 guards=json.loads((a.root/'role-mask-baseline-static-guards.json').read_text())['instructions']
 death=json.loads((a.root/'death-producer-static-guards.json').read_text())['instructions']
 for address,data in death.items():
  assert address not in guards or guards[address]==data
  guards[address]=data
 pins={}
 for name,pin in rows[0]['metadata']['source_sha256'].items():
  path=Path('tools/frida/research')/name
  if not path.exists():path=Path('tools/frida')/name
  if path.exists():
   assert digest(path)==pin,('capture source differs',str(path))
   pins[str(path)]=pin
 bundle=dict(pins=pins,task='GROUP-03.4.7.3',payoff=378,binary_sha256=SHA,instructions=guards,markers=rows[0]['markers'],records=records,captures=2,controls=1,normalization=['Only raw Captain pointer canonicalized by first occurrence; all raw seq2..1265, unit/mover canonical identities, primary clocks, flags and callers retained exactly'],observation_sequences=[[r['seq']for r in o['records']]for o in rows],captain_aliases=[o['captain_aliases']for o in rows],provenance=[dict(path=str(p),sha256=digest(p),preload_sha256=digest(p.with_name(p.stem+'-preload.txt')))for p in observations+[control]],scope='Roster-only death/owner subscription versus retained-target lost/death/owner; real Wisp cargo TargetLost preserves roster until death and permits Captain recenter. Complete original-map semantic records repeat; full Captain/Town policy remains in existing leaf.')
 a.output.write_bytes(gzip.compress(json.dumps(bundle,sort_keys=True,separators=(',',':')).encode(),mtime=0))
 print(json.dumps(dict(public_markers=len(bundle['markers']),records=len(records),instructions=len(guards),sha256=digest(a.output))))
if __name__=='__main__':main()
