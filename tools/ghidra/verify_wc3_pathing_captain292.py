#!/usr/bin/env python3
"""Verify the original StopGathering producer and initial Town selection."""
import argparse,gzip,hashlib,json,re,sys
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-captain292-1.27.json'
SOURCES=['tools/frida/research/captain292_'+s for s in ('make_map.py','probe.j','probe.ai','observer.js')]+['tools/frida/research/captain_enrollment157_capture.py','tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py','tools/ghidra/research/captain292_oracle.py','tools/ghidra/research/Captain292Evidence.java']
TESTS=['wc3_bot.*','wc3_movement.public_captain*','wc3_movement.capscope273*','wc3_save.*']
EVENTS=('native-stop','select-enter','select-leave','location-init','actor-create','town-admit-enter','town-admit-leave')
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def public(s):return re.findall(r'call Preload\( "(R292[^"\r\n]*)" \)',s)
def strip(x):
 if isinstance(x,dict):return{k:strip(v)for k,v in x.items()if k not in ('ms','pointer','unit','bridge','caller')}
 if isinstance(x,list):return[strip(v)for v in x]
 return x
def normalized(rows):return[strip(x)for x in rows if x.get('event')in EVENTS]
def validate(spec):
 if(spec['game_sha256']!=SHA or set(spec['source_sha256'])!=set(SOURCES)or spec['engine_tests']!=TESTS or len(spec['original'])!=8):raise ValueError('producer contract changed')
 for n,h in spec['source_sha256'].items():
  if digest(ROOT/n)!=h:raise ValueError('source changed '+n)
 if digest(SPEC.with_suffix('.json.gz'))!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 bundle=json.loads(gzip.decompress(SPEC.with_suffix('.json.gz').read_bytes()))
 if set(bundle)!=set(('combat','structure','worker')):raise ValueError('priority domains missing')
 for kind,captures in bundle.items():
  if len(captures)!=3:raise ValueError('missing repeat/control')
  for i,c in enumerate(captures):
   rows=c['rows'];meta=rows[0];footer=rows[-1];pub=public(c['preload'])
   if(meta.get('mode')!=('control'if i==2 else'observe')or not meta.get('owned')or meta.get('sha256')!=SHA or
      meta.get('display')not in(':98',':99')or meta['source_sha256']['map']!=spec['maps'][kind]or
      footer.get('event')!='preload-file'or not footer.get('complete')or
      hashlib.sha256(c['preload'].encode()).hexdigest()!=footer['sha256']or
      pub!=spec['public'][kind]or len(pub)!=80 or any(x.get('type')=='error'for x in rows)):
    raise ValueError('incomplete/unowned/public capture changed')
   for n in SOURCES:
    name=Path(n).name
    if name in meta['source_sha256']and meta['source_sha256'][name]!=spec['source_sha256'][n]:raise ValueError('captured source changed')
   if i==2:
    if any(x.get('event')in EVENTS+('marker','module','snapshot','trace-end')for x in rows):raise ValueError('instrumented control')
   else:
    markers=[x['value']for x in rows if x.get('event')=='marker']
    # Probe emits three final samples after PreloadGenEnd. Keep the on-disk
    # public prefix and independently freeze those trailing observer-only rows.
    if markers!=spec['observed'][kind]or markers[:80]!=pub or len(markers)!=83:raise ValueError('live marker prefix changed')
    if not any(x.get('event')=='trace-end'and x.get('installed')for x in rows):raise ValueError('observer absent')
    if normalized(rows)!=spec['native'][kind]or len(spec['native'][kind])!=11:raise ValueError('native priority boundaries changed')
  native=spec['native'][kind];selects=[x for x in native if x['event']=='select-enter'];leaves=[x for x in native if x['event']=='select-leave']
  if len(selects)!=2 or selects[0]['owner']['locations']!=[0xffffffff]*4 or selects[1]['owner']['locations'][0]!=0 or any(x['owner']['locations'][0]!=0 for x in leaves):raise ValueError('initial/no-op selection changed')
  loc=next(x for x in native if x['event']=='location-init');actors=[x for x in native if x['event']=='actor-create']
  if len(actors)!=2 or [x['type']for x in actors]!=[2,1]or any(x['xy']!=loc['xy']for x in actors):raise ValueError('selected defaults do not feed Captain construction')
 return dict(captures=6,controls=3,public_markers=720,native_boundaries=66)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());retail=validate(spec);original_bytes(a.binary,spec['instructions'])
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));import captain292_oracle
 cases=[{k:v for k,v in x.items()if k not in('selected','flags')}for x in spec['original']]
 if captain292_oracle.original(a.binary,cases)!=spec['original']:raise ValueError('original selection changed')
 import wc3_pathfinding_engine_tests as runner
 engine=runner.run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
 report=dict(passed=True,status='verified',retail=retail,original_calls=8,instructions=len(spec['instructions']),engine=engine,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),scope=spec['scope'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
