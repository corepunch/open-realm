#!/usr/bin/env python3
"""Portable native299 evidence check. No game, DLL, Frida or external packages required."""
import argparse,base64,collections,gzip,hashlib,json,re
from pathlib import Path
DEFAULT=Path(__file__).resolve().parents[1]/'fixtures/retail-attack-recovery299-1.27.json'
def sha(data):return hashlib.sha256(data).hexdigest()
def et(r):
 e=r.get('event')
 if isinstance(e,list):
  assert len(e)==4 and e[2]==0xd01de and set(r)=={'event','seq','tick','scene','buff','clock'}
  return 'invisibility-buff-event'
 return e

def audit(files,spec):
 """Check decoded literal files; callers can use this entry point for semantic mutations."""
 for name,digest in spec['source_sha256'].items():
  if 'sources/'+name in files:assert sha(files['sources/'+name])==digest
 def read(n):return [json.loads(s) for s in files[n+'.jsonl'].decode().splitlines()]
 streams={}; reports={}; projection=[]
 source_pins=None
 for name in ['observe1','observe2','control1']:
  rows=read(name); meta=rows[0]
  if source_pins is None:source_pins=meta['source_sha256']
  assert meta['source_sha256']==source_pins
  assert source_pins==spec['source_sha256']
  assert meta['sha256']==spec['game_sha256']
  end=[r for r in rows if et(r)=='source-end']; assert len(end)==1 and end[0]['unchanged'] and end[0]['source_sha256']==meta['source_sha256']
  public=[]
  for suffix,event in [('-start-preload.txt','start-preload-file'),('-preload.txt','preload-file')]:
   p=name+suffix; rec=[r for r in rows if et(r)==event]; assert len(rec)==1 and rec[0]['sha256']==sha(files[p])
   public+=re.findall(r'call Preload\( "(AR299 [^"\r\n]*)" \)',files[p].decode())
  assert len(public)==2066 and public[-1]=='AR299 tick=1799 scene=5 label=complete' and not any('OVERSIZE' in s for s in public)
  streams[name]=public
  assert not any(r.get('type')=='error' or et(r)=='trace-failed' for r in rows)
  report={'raw_sha256':sha(files[name+'.jsonl']),'public_markers':len(public),'source_frozen':True}
  if name!='control1':
   rr=[r for r in rows if 'seq' in r]; foot=[r for r in rows if et(r)=='trace-end']; assert len(foot)==1
   f=foot[0]; assert len(rr)==spec['native_sequence_per_observation']
   assert f['counts']['arrival']==spec['arrival_per_observation'] and f['counts']['velocity']==spec['velocity_per_observation']
   assert f['complete'] and f['readOnly'] and not f['dropped'] and not f['overflow'] and not f['pending_public_queries']
   assert [r['seq'] for r in rr]==list(range(1,len(rr)+1)) and f['sequence']==len(rr)
   assert dict(collections.Counter(et(r) for r in rr))==f['counts']
   assert [r['value'] for r in rr if et(r)=='marker']==public
   base=int(next(r['base'] for r in rr if et(r)=='module'),16)
   pointers={}
   def norm(x):
    if isinstance(x,str) and re.fullmatch(r'0x[0-9a-f]+',x):
     v=int(x,16)
     if not v:return 'NULL'
     if base<=v<base+meta['imageSize']:return {'module_rva':v-base}
     if x not in pointers:pointers[x]=len(pointers)+1
     return {'pointer_ordinal':pointers[x]}
    if isinstance(x,list):return [norm(y) for y in x]
    if isinstance(x,dict):return {k:norm(v) for k,v in x.items()}
    return x
   pp=[]
   for raw in rr:
    r=json.loads(json.dumps(raw))
    if isinstance(r['event'],list):
     w=r['event']; assert base<=w[0]<base+meta['imageSize']; assert w[3]==int(r['buff']['unit']['pointer'],16)
     r['event']='invisibility-buff-event';r['event_words']=[hex(w[0]),w[1],w[2],hex(w[3])]
    pp.append(norm(r))
   projection.append(pp)
   report.update(sequence=len(rr),counts=f['counts'])
  else:
   allowed={'metadata','owned-loading-input','loading-key','control-start-file','start-preload-file','preload-file','source-end'}
   assert all(et(r) in allowed for r in rows),set(et(r) for r in rows)
  reports[name]=report
 assert streams['observe1']==streams['observe2']==streams['control1']
 reports['public_equal']=True
 raw=read('observe1')
 active=[r for r in raw if et(r)=='recover-enter' and r['attack']['flags']&4]
 assert [(r['scene'],r['forced'],r['capture_word']) for r in active]==[(0,0,0),(1,0,0),(2,1,0),(3,0,0),(4,1,1)]
 points=[r for r in raw if et(r)=='point-task' and r['caller']==0x49d490]
 assert [r['scene'] for r in points]==[0,4]
 assert all(r['code']==0xd016c and r['range']==0x432b0000 and r['unit']['role']==1 for r in points)
 reports['active_recovery_entries']=active
 reports['D051']=points
 reports['limitations']=['Scene5 pause/loss/resume yields no active recovery entry; do not relabel it as observed retained-flag recovery.', 'All 9058 sequenced rows retained; pointer addresses alone canonicalized. Public markers and every numeric clock/range/identity word compare literally.', 'Native repeat/control evidence only; no C replay or saved parity established.']
 reports['native_pointer_only_equal']=projection[0]==projection[1]
 if projection[0]!=projection[1]:
  for i,(a,b) in enumerate(zip(*projection)):
   if a!=b:reports['first_native_difference']={'index':i,'a':a,'b':b};break
 reports['schema_decoding']='Only array-valued event rows with exact frozen 642dd0 hook schema and d01de payload are relabeled invisibility-buff-event; all four words retained. Word0 is module pointer; word3 must equal buff.unit pointer. No numeric clocks, identities, ranges or order normalized.'
 assert projection[0]==projection[1], 'native ordered projection differs'
 reports['engine_parity']=False
 return reports

def load(spec_path=DEFAULT,bundle_path=None):
 spec=json.loads(Path(spec_path).read_text())
 raw=Path(bundle_path or str(spec_path)+'.gz').read_bytes()
 assert sha(raw)==spec['bundle_sha256'],'compressed archive hash'
 bundle=json.loads(gzip.decompress(raw)); assert bundle['schema']==1
 files={n:base64.b64decode(v,validate=True) for n,v in bundle['files'].items()}
 assert set(files)==set(spec['files'])
 assert all(sha(b)==spec['files'][n] for n,b in files.items()),'literal archive member hash'
 return files,spec

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--spec',type=Path,default=DEFAULT)
 ap.add_argument('--bundle',type=Path)
 ap.add_argument('--report',type=Path)
 args=ap.parse_args(); report=audit(*load(args.spec,args.bundle))
 if args.report:args.report.write_text(json.dumps(report,indent=2)+'\n')
 print('PASS: 6198 public markers; two exact 9058-row native streams; 2671 arrival/velocity pairs each; engine_parity=false')
if __name__=='__main__':main()
