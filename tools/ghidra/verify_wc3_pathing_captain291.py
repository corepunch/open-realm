#!/usr/bin/env python3
"""Verify shared CTownSpot defaults and ordered physical Captain reset."""
import argparse,copy,gzip,hashlib,json,re,sys
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-captain291-1.27.json'
SOURCES=['tools/frida/research/captain291_'+s for s in ('make_map.py','probe.j','probe.ai','observer.js')]+['tools/frida/research/captain_enrollment157_capture.py','tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py','tools/ghidra/research/captain291_oracle.py','tools/ghidra/research/Captain291Evidence.java','tools/ghidra/fixtures/retail-captain291-types-1.27.json']
TESTS=['wc3_bot.*','wc3_movement.public_captain*','wc3_movement.capscope273*','wc3_save.*']
EVENTS=('location-init','location-refresh','town-admit-enter','town-admit-leave','create-enter','actor-create','create-leave','reset-enter','home-enter','home-leave','teleport-enter','teleport-leave','reset-leave')
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def digest_text(text):return hashlib.sha256(text.encode()).hexdigest()
def public(text):return re.findall(r'call Preload\( "(R291[^"\r\n]*)" \)',text)
def strip(x):
 if isinstance(x,dict):return {k:strip(v)for k,v in x.items()if k not in ('ms','pointer','unit','bridge')}
 if isinstance(x,list):return [strip(v)for v in x]
 return x
def normalized(rows):return [strip(x)for x in rows if x.get('event')in EVENTS]
def header(original):
 s='/* Original CTownSpot9bfe80 outputs. First nine cases also match live retail words. */\nstatic struct {uint32_t before[4],building[3],mine[3],after[4];bool has_building,has_mine;} const captain291_refresh[]={\n'
 for row in original:
  arrays=[row['before'],row['building']or[0]*3,row['mine']or[0]*3,row['after']]
  s+='    {'+','.join('{'+','.join('0x%08xu'%n for n in a)+'}'for a in arrays)+','+str(row['building']is not None).lower()+','+str(row['mine']is not None).lower()+'},\n'
 return s+'};\n'
def validate(spec):
 if spec['game_sha256']!=SHA or set(spec['source_sha256'])!=set(SOURCES)or spec['engine_tests']!=TESTS:raise ValueError('source/producer contract differs')
 for name,h in spec['source_sha256'].items():
  if digest(ROOT/name)!=h:raise ValueError('source changed '+name)
 path=SPEC.with_suffix('.json.gz')
 if digest(path)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 bundle=json.loads(gzip.decompress(path.read_bytes()))
 if len(bundle['captures'])!=2 or len(bundle['controls'])!=1 or len(bundle['exploratory'])!=1:raise ValueError('missing repeats/control/bare witness')
 for c in bundle['captures']+bundle['controls']+bundle['exploratory']:
  rows=c['rows'];meta=rows[0];explore=c in bundle['exploratory']
  if meta.get('event')!='metadata'or not meta.get('owned')or meta.get('sha256')!=SHA or meta['source_sha256']['map']!=spec['bare_map_sha256'if explore else'map_sha256']:raise ValueError('capture ownership/build/map')
  if(meta.get('display'),meta.get('remote'))not in((':98','127.0.0.1:27049'),(':99','127.0.0.1:27050')):raise ValueError('unowned retail environment')
  for name in ('captain291_observer.js','captain_enrollment157_capture.py'):
   if meta['source_sha256'][name]!=spec['source_sha256']['tools/frida/research/'+name]:raise ValueError('observer/controller changed')
  if any(x.get('type')=='error'for x in rows)or not any(x.get('event')=='preload-file'and x.get('complete')for x in rows):raise ValueError('incomplete/failed capture')
  footer=rows[-1]
  if footer.get('event')!='preload-file'or digest_text(c['preload'])!=footer['sha256']:raise ValueError('preload provenance changed')
  if len(public(c['preload']))!=151:raise ValueError('incomplete public markers')
  if meta['mode']=='observe'and public(c['preload'])!=[x['value']for x in rows if x.get('event')=='marker']:raise ValueError('observed markers differ from file')
  if not explore and public(c['preload'])!=spec['public']:raise ValueError('public query/member trajectory changed')
  if c in bundle['captures']or explore:
   if meta['mode']!='observe'or not any(x.get('event')=='trace-end'and x.get('installed')for x in rows):raise ValueError('uninstalled observer')
   if not explore and(normalized(rows)!=spec['native']or len(spec['native'])!=101):raise ValueError('native default/reset boundary changed')
  elif meta['mode']!='control'or any(x.get('event')in EVENTS+('module','snapshot')for x in rows):raise ValueError('control instrumented')
 native=spec['native'];refresh=[x for x in native if x['event']=='location-refresh']
 if len(refresh)!=9 or len(spec['original'])!=16 or len(spec['cases'])!=16:raise ValueError('refresh domains missing')
 for case,row in zip(spec['cases'],spec['original']):
  if case!={k:row[k]for k in ('before','offset','mine','building')}:raise ValueError('supplied original inputs changed')
 for x,row in zip(refresh,spec['original']):
  if x['before']['attack']+x['before']['defense']!=row['before']or x['after']['attack']+x['after']['defense']!=row['after']:raise ValueError('live/default original words disagree')
 creates=[x for x in native if x['event']=='actor-create']
 if len(creates)!=4 or [x['type']for x in creates]!=[2,1,2,1]or[x['captain']['state']for x in creates]!=[2,1,2,1]:raise ValueError('actor state/selection changed')
 # A reset must refresh, update attack home, relocate attack, update defense,
 # relocate defense, and only then return. Captured internal identity/pose
 # words remain in normalized(), not replaced by approximate comparisons.
 resets=[];inside=None
 for x in native:
  event=x['event']
  if event=='reset-enter':inside=[]
  elif inside is not None:
   if event=='reset-leave':resets.append(inside);inside=None
   else:inside.append(event)
 expected=['location-refresh','home-enter','home-leave','teleport-enter','teleport-leave','home-enter','home-leave','teleport-enter','teleport-leave']
 if len(resets)!=7 or any(x!=expected for x in resets):raise ValueError('reset observation order changed')
 if (ROOT/'games/warcraft-3/game/tests/fixtures/retail_captain291.h').read_text()!=header(spec['original']):raise ValueError('engine expected words differ from original')
 return dict(captures=2,controls=1,exploratory_bare=1,public_markers=151,native_boundaries=202,resets=7,live_refreshes=9)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());retail=validate(spec);original_bytes(a.binary,spec['instructions'])
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));import captain291_oracle
 if captain291_oracle.original(a.binary,spec['cases'])!=spec['original']:raise ValueError('original refresh differs')
 import verify_wc3_pathing_work242 as runner
 old=runner.TESTS
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report,timeout=300)
 finally:runner.TESTS=old
 report=dict(passed=True,status='verified',retail=retail,original_refreshes=16,instructions=len(spec['instructions']),engine=engine,binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['scope'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
