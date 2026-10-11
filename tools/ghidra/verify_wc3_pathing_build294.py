#!/usr/bin/env python3
"""Verify queued Build uses the shared point-order activation contract."""
import argparse,hashlib,json,re,sys
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-build294-1.27.json'
SOURCES=['tools/ghidra/research/build294_oracle.py','tools/ghidra/research/Build294Evidence.java','tools/ghidra/research/foot03_spatial_harness_copy.py']
TESTS=['wc3_building.*','wc3_order_lifecycle.*','wc3_order_subscribers.*','wc3_order_reentry.*','wc3_interrupt.*','wc3_save.*']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def validate(spec):
 if spec['game_sha256']!=SHA or set(spec['source_sha256'])!=set(SOURCES)or spec['engine_tests']!=TESTS or len(spec['instructions'])!=264 or len(spec['original'])!=12:raise ValueError('queued Build contract changed')
 for n,h in spec['source_sha256'].items():
  if digest(ROOT/n)!=h:raise ValueError('source changed '+n)
 expected={(c,b,k)for c in(0x68423933,0x7a423934)for b in(False,True)for k in('none','nested','stop')}
 if{(x['command'],x['busy'],x['callback'])for x in spec['original']}!=expected:raise ValueError('original matrix changed')
 for x in spec['original']:
  a=x['append'];trace=x['activation'];events=[e['event']for e in trace]
  if a['count']!=(2 if x['busy']else 1)or a['head']!=([0,100]if x['busy']else([0xffffffff]*2 if x['callback']=='stop'else[1,101]))or a['tail']!=[1,101]:raise ValueError('retained queue head changed')
  if x['busy']and a['trace']!=[dict(event='busy-task-notification',code=0xd02a5)]:raise ValueError('busy append publishes')
  if events!=(['point-event']if x['callback']=='nested'else['point-event','ability-execute','dispatch-tasks']):raise ValueError('callback task guard changed')
  event=trace[0]
  if event!=dict(event='point-event',command=x['command'],flags=4,head=[1,101],internal=[0xffffffff]*2)or x['flags']!=4:raise ValueError('point subtype publication changed')
  if x['callback']!='nested'and trace[1]['head']!=([0xffffffff]*2 if x['callback']=='stop'else[1,101]):raise ValueError('instant Stop task continuation changed')
 return spec

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=validate(json.loads(SPEC.read_text()));original_bytes(a.binary,spec['instructions'])
 # Reuse immutable existing Frida evidence; no new UI Shift-Build capture is claimed.
 import verify_wc3_pathing_build293 as build,verify_wc3_pathing_queue263 as queue,gzip
 for module,key in((build,'build293'),(queue,'queue263')):
  fixture=module.SPEC if key=='build293'else module.FIXTURE
  if digest(fixture)!=spec['prior_evidence'][key]:raise ValueError('prior evidence changed')
 retail_build=build.validate(json.loads(build.SPEC.read_text()))
 q=queue.validate(json.loads(queue.FIXTURE.read_text()));retail_queue=queue.runtime(json.loads(gzip.decompress(queue.BUNDLE.read_bytes())),q)
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));import build294_oracle
 cases=[{k:x[k]for k in('command','busy','callback')}for x in spec['original']]
 for repeat in range(2):
  if build294_oracle.original(a.binary,cases)!=spec['original']:raise ValueError('original append/dispatch differs')
 import wc3_pathfinding_engine_tests as runner
 engine=runner.run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
 report=dict(passed=True,status='verified',original_calls=24,instructions=len(spec['instructions']),retail_build=retail_build,retail_queue=retail_queue,engine=engine,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),scope=spec['scope'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
