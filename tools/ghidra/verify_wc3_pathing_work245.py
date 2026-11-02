#!/usr/bin/env python3
"""Verify original formation-policy producers and facing-only target requests."""
import argparse,gzip,hashlib,json,re,subprocess,sys,tempfile
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
from research.work245_facing_oracle import original
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work245-1.27.json';BUNDLE=FIXTURE.with_suffix('.json.gz')
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_facing245.h'
SOURCES=['tools/frida/research/work245_'+s for s in('observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/target222_capture.py','tools/ghidra/research/Work245Evidence.java',
 'tools/ghidra/research/work245_facing_oracle.py','tools/ghidra/research/verify_FORM-01.3_references.py',
 'tools/frida/research/form013_policy_table.py']
TESTS=['wc3_movement.policy245*','wc3_movement.target222*','wc3_movement.target223*',
 'wc3_movement.target224*','wc3_facing.*',
 'wc3_ancient_root.*','wc3_movement.target_drop_policy*','wc3_movement.target_route_policy*',
 'wc3_movement.selected_point_retains_formation_toggle_policy']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(preload):return re.findall(r'call Preload\( "(W245 [^"\r\n]*)" \)',preload)
def sequence(rows):return[{k:v for k,v in r.items()if k!='seq'}for r in rows if 'seq'in r and r['event']!='module']
def journeys(rows):
 out=[]
 for scene in range(3):
  local=[r for r in rows if r.get('scene')==scene]
  swing=next(r['seq']for r in local if r['event']=='swing')
  motion=[r for r in local if r['event']=='motion'and r['seq']<swing]
  if len(motion)!=(6,60,1)[scene]:raise ValueError('initial turn journey incomplete')
  first=motion[0]['before']
  out.append(dict(input=[first['clock'][0],*first['pose'][:2],first['pose'][5],*first['parameters']],
   motion=[[r['c'],r['after']['clock'][0],*r['after']['pose'][:4],r['after']['pose'][5]]for r in motion]))
 return out
def header(rows,kernel):
 row=lambda a:'{'+','.join(f'0x{v:08x}u'for v in a)+'}'
 js=journeys(rows)
 out=['#ifndef RETAIL_FACING245_H','#define RETAIL_FACING245_H',
 '/* Original15f660 execution and initial public Attack turn commits; never engine-generated. */',
 'static struct {uint32_t input[6],output;} const facing245_queries[]={']
 out +=['{'+row(r['input'])+','+str(r['output'])+'},'for r in kernel]
 out +=['};','static uint32_t const facing245_inputs[3][7]={'+','.join(row(j['input'])for j in js)+'};',
 'static unsigned const facing245_counts[3]={6,60,1};','static uint32_t const facing245_motion[3][60][7]={']
 for j in js:out+=['{',',\n'.join(row(r)for r in j['motion']),'},']
 return '\n'.join(out+['};','#endif'])+'\n'
def validate_runtime(bundle,spec):
 if len(bundle['captures'])!=3:raise ValueError('two repeats and unhooked control required')
 for i,c in enumerate(bundle['captures']):
  rows,preload=c['rows'],c['preload'];meta,footer=rows[0],rows[-1]
  if(meta['sha256']!=SHA or not meta['owned']or meta['env']not in('B','C')or
     meta['source_sha256']['map']!=spec['map_sha256']or meta['mode']!=('control'if i==2 else'observe')or
     not footer.get('complete')or footer['markers']!=116 or markers(preload)!=spec['markers']or
     hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256']or
     any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('invalid capture')
  for p in(SOURCES[0],SOURCES[3]):
   if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('observer provenance differs')
  if i==2:
   if any('seq'in r or r.get('event')=='trace-end'for r in rows):raise ValueError('control was instrumented')
   continue
  ends=[r for r in rows if r.get('event')=='trace-end']
  if(len(ends)!=1 or not ends[0].get('readOnly')or not ends[0].get('installed')or
     ends[0]['counts']!=dict(Counter(r['event']for r in rows if 'seq'in r))or
     sequence(rows)!=spec['sequence']or [r['value']for r in rows if r.get('event')=='marker']!=spec['markers']):raise ValueError('repeat or observer completion differs')
  if journeys(rows)!=spec['journeys']:raise ValueError('turn commits differ')
  requests=[r for r in rows if r.get('event')=='target-request']
  if len(requests)!=11 or any(r['range']!=0x7f7fffff or r['persistent']or r['warp']!=1 for r in requests):raise ValueError('sentinel producer differs')
  if any(r['flags']!=0x200 for r in rows if r.get('event')=='publish')or any(r['flags']&0xffff!=0x1200 for r in rows if r.get('event')=='group'):raise ValueError('captured bypass policy differs')
 return dict(captures=2,controls=1,public_markers=348,motion_commits=150,initial_commits=134,sentinel_requests=22)
def validate_inventory(bundle,spec):
 from tools.frida.research.form013_policy_table import table
 frozen=ROOT/'tools/ghidra/fixtures/research/FORM-01.3-expected.json'
 if digest(frozen)!=spec['inventory_sha256']:raise ValueError('historical inventory changed')
 inventory=json.loads(frozen.read_text());refs=bundle['references'];constants=bundle['constants']
 if inventory['binary_sha256']!=SHA or refs['binary_sha256']!=SHA or not inventory['flag20_equivalent']:raise ValueError('inventory executable differs')
 if refs['setters']['6f16d7e0']['rel32']or refs['setters']['6f16d7e0']['absolute']:raise ValueError('unexpected spacing producer')
 if constants['flag20_pairs']!=inventory['flag20_pairs']or any(a!=b for a,b in constants['flag20_pairs'].values()):raise ValueError('spacing changed')
 if [[c['name'],hashlib.sha256(c['text'].encode()).hexdigest()]for c in bundle['inventory_captures']]!=inventory['captures']:raise ValueError('historical captures changed')
 with tempfile.TemporaryDirectory(prefix='wc3-policy245-')as d:
  paths=[]
  for c in bundle['inventory_captures']:
   task,name=c['name'].split('/');p=Path(d)/task/'captures'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(c['text']);paths.append(p)
  if table(paths)!=inventory['witnesses']:raise ValueError('policy witnesses differ')
 return dict(policy_captures=len(paths),policy_witnesses=len(inventory['witnesses']))
def verify_references(binary,bundle):
 with tempfile.TemporaryDirectory(prefix='wc3-policy245-scan-')as d:
  report=Path(d)/'report.json'
  subprocess.run([sys.executable,str(ROOT/SOURCES[6]),'--binary',str(binary),'--report',str(report)],stdout=subprocess.DEVNULL,check=True,timeout=120)
  actual=json.loads(report.read_text())
  if any(actual[k]!=bundle['references'][k]for k in('binary_sha256','setters','others')):raise ValueError('original producer references changed')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=json.loads(FIXTURE.read_text())
 if spec['version']!=1 or spec['task']!='FORM-01.3' or spec['game_sha256']!=SHA or set(spec['pins'])!=set(SOURCES)or spec['engine_tests']!=TESTS:raise ValueError('contract changed')
 for path,sha in spec['pins'].items():
  if digest(ROOT/path)!=sha:raise ValueError('changed source '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));live=validate_runtime(bundle,spec)
 sys.path.insert(0,str(ROOT));inventory=validate_inventory(bundle,spec);verify_references(a.binary,bundle);original_bytes(a.binary,spec['instructions'])
 kernel=original(a.binary)
 if kernel!=spec['facing_queries']or header(bundle['captures'][0]['rows'],kernel)!=HEADER.read_text():raise ValueError('original facing contract changed')
 a.report.parent.mkdir(parents=True,exist_ok=True)
 from verify_wc3_pathing_work244 import run_engine
 import verify_wc3_pathing_work244 as prior
 old=prior.TESTS
 try:prior.TESTS=TESTS;engine=run_engine(a.test_binary,a.data,a.report)
 finally:prior.TESTS=old
 result=dict(passed=True,task=spec['task'],**live,**inventory,facing_queries=len(kernel),instructions=len(spec['instructions']),engine=engine,fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits']);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
