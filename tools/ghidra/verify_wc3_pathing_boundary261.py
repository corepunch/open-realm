#!/usr/bin/env python3
"""Verify clock rollover, backward load evidence and engine FIFO continuation."""
import argparse,hashlib,importlib.util,json,re,sys
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
import verify_wc3_pathing_work242 as runner
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-boundary261-1.27.json'
SOURCES=['tools/ghidra/research/verify_sched261_boundary.py','tools/ghidra/research/Clock261Evidence.java',
 'games/warcraft-3/game/tests/fixtures/retail_boundary261.h',
 'games/warcraft-3/game/tests/retail_timer_motion_117.h','tools/ghidra/fixtures/retail-timer-epoch-1.27.json',
 'tools/ghidra/fixtures/research/ORDER-05.3-expected.json',
 'tools/ghidra/research/verify_ORDER-05.1_request_heap.py','tools/ghidra/research/verify_ORDER-03.1_subscriber_dispatch.py']
TESTS=['wc3_movement.boundary261*',
 'wc3_movement.public_timer_getters_drive_motion_across_segments_pause_and_epoch_save',
 'wc3_save.load_restores_server_clock_onto_saved_time']

def canonical(value):return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()
def module(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def validate(s):
 if s['version']!=1 or s['task']!='SCHED-01.2' or s['game_sha256']!=SHA or s['engine_tests']!=TESTS or set(s['pins'])!=set(SOURCES):raise ValueError('boundary contract differs')
 for p,sha in s['pins'].items():
  if hashlib.sha256((ROOT/p).read_bytes()).hexdigest()!=sha:raise ValueError('changed source '+p)
 if canonical(s['original'])!=s['original_sha256']:raise ValueError('original words changed')
 rows=s['original']['rows']
 if len(rows)!=80 or [r['step']for r in rows]!=list(range(1,81)) or sum(bool(r['buckets'])for r in rows)!=13:raise ValueError('incomplete timeline')
 if rows[24]['epoch']!=0 or rows[25]['epoch']!=1 or rows[-1]['counter']!=1013:raise ValueError('rollover differs')
 if len(s['instructions'])!=339:raise ValueError('instruction coverage missing')
 return s

def public_evidence(archive):
 timer=module('timer261',ROOT/'tools/frida/verify_wc3_timer_trace.py')
 fixture=json.loads((ROOT/SOURCES[4]).read_text())
 paths=[ROOT/'tools/ghidra/fixtures/retail-timer-epoch-1.27-b.jsonl.gz',ROOT/'tools/ghidra/fixtures/retail-timer-epoch-1.27-c.jsonl.gz']
 import gzip
 results=[timer.verify(gzip.decompress(p.read_bytes()),fixture,cap)for p,cap in zip(paths,fixture['captures'])]
 # Complete archived owner-clock/UI-load controls, including absolute restored requests.
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
 clocks=module('clocks261',ROOT/'tools/ghidra/research/verify_order181_clocks.py')
 load=clocks.verify(archive)
 return dict(retail_repeats=len(results),motion_commits=sum(r['motion_commits']for r in results),
  public_getters=sum(r['public_getters']for r in results),request_pops=load['pops'],load_continuation_rows=load['continuation_rows'],controls=load['observer_free_comparisons'])

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--archive',type=Path,default=Path('/GitHub/wc3-analysis/reports/pathfinding-1.27/research'));p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 s=validate(json.loads(FIXTURE.read_text()));original_bytes(a.binary,s['instructions'])
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));native=module('boundary261',ROOT/SOURCES[0])
 raw=a.binary.read_bytes()
 for i in range(2):
  actual=native.execute(raw)
  if actual!=s['original']:raise ValueError('fresh original clock/FIFO execution differs')
  if native.header(actual)!=(ROOT/SOURCES[2]).read_text():raise ValueError('C fixture differs from original execution')
 live=public_evidence(a.archive);a.report.parent.mkdir(parents=True,exist_ok=True);runner.TESTS=TESTS
 result=dict(passed=True,binary_sha256=SHA,game_library_sha256=runner.digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),task=s['task'],native_repeats=2,advances=80,owner_visits=13,**live,
  engine=runner.run_engine(a.test_binary,a.data,a.report),instructions=len(s['instructions']),limits=s['original']['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
