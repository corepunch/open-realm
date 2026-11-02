#!/usr/bin/env python3
"""Verify guard periodic identity on its actual original event-clock dispatcher."""
import argparse,gzip,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_fog252 import fog_rows,target_rows,words_digest
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-guard255-1.27.json';BUNDLE=FIXTURE.with_suffix('.json.gz')
HEADER='games/warcraft-3/game/tests/fixtures/retail_reacquire252.h'
SOURCES=['tools/frida/research/target255_guard_requests.js','tools/frida/research/target03_capture.py',
 'tools/frida/research/target021_observer.js','tools/frida/research/target03_vis_observer.js',
 'tools/ghidra/research/Work255Evidence.java',HEADER]
TESTS=['wc3_order_lifecycle.guard25*','wc3_movement.target254*','wc3_order_subscribers.*','wc3_movement.public_timer*']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def normalized(rows):
 return [[r['event'],r['c'],*[r[side][key]for side in('before','after')for key in('deadline','period','flags','serial','value')]]for r in rows if r.get('event')in('guard-execute255','guard-rearm255')]
def verify_runtime(bundle,spec):
 if len(bundle['captures'])!=2 or len(bundle['controls'])!=1:raise ValueError('missing repeated/control capture')
 markers=None
 for i,c in enumerate(bundle['captures']+bundle['controls']):
  rows=c['rows'];m=rows[0];f=rows[-1];p=c['preload'];public=re.findall(r'call Preload\( "(T3R [^"\r\n]*)" \)',p)
  if(m.get('event')!='metadata'or m.get('mode')!=('observe'if i<2 else'control')or not m.get('owned')or m.get('sha256')!=SHA or
   m.get('remote')not in('127.0.0.1:27049','127.0.0.1:27050')or m.get('display')not in(':98',':99')or m['source_sha256']['map']!=spec['map_sha256']or
   f.get('event')!='preload-file'or not f.get('complete')or f['markers']!=215 or len(public)!=215 or
   hashlib.sha256(p.encode()).hexdigest()!=f['sha256']or any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('incomplete/unowned capture')
  if markers is not None and public!=markers:raise ValueError('observer changed public gameplay')
  markers=public
  if i==2:
   if any(r.get('event')in('gtick','marker','guard-execute255','guard-rearm255','trace-end')for r in rows):raise ValueError('control instrumented')
   continue
  for path in SOURCES[:4]:
   if m['source_sha256'][Path(path).name]!=spec['pins'][path]:raise ValueError('source provenance changed')
  end=[r for r in rows if r.get('event')=='trace-end']
  if len(end)!=1 or not end[0]['installed']or any(k.endswith('-dropped')for k in end[0]['counts']):raise ValueError('incomplete observer')
  if [r['value']for r in rows if r.get('event')=='marker']!=public:raise ValueError('observer marker missing')
  if(len(fog_rows(rows))!=618 or words_digest(fog_rows(rows))!=spec['follower_sha256']or len(target_rows(rows))!=624 or words_digest(target_rows(rows))!=spec['target_sha256']):raise ValueError('old retail trajectory changed')
  if normalized(rows)!=spec['requests']:raise ValueError('original request trace changed')
  rearms=[r for r in rows if r.get('event')=='guard-rearm255']
  if len(rearms)!=4:raise ValueError('missing guard rearm witnesses')
  for r in rearms:
   a,b=r['before'],r['after']
   if(any(a[k]!=b[k]for k in('request','clock','receiver','serial','value','period'))or a['value']!=0xd01ad or a['period']!=0x40000000 or a['flags']!=1 or b['flags']!=0x20001):raise ValueError('periodic guard identity changed')
  for r in rows:
   if r.get('event')=='guard-execute255'and r['before']['flags']&0x10000 and r['before']!=r['after']:raise ValueError('cancelled request executed')
 return dict(captures=2,controls=1,public_markers=645,raw_follower_rows=618,raw_target_rows=624,guard_rearms=8,guard_request_events=26)
def validate(spec):
 if(spec['version']!=1 or spec['task']!='TARGET-03.2'or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS or set(spec['pins'])!=set(SOURCES)):raise ValueError('guard periodic contract changed')
 for path,h in spec['pins'].items():
  if digest(ROOT/path)!=h:raise ValueError('source changed '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 b=json.loads(gzip.decompress(BUNDLE.read_bytes()));verify_runtime(b,spec);return b
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 s=json.loads(FIXTURE.read_text());b=validate(s);original_bytes(a.binary,dict(s['instructions']));a.report.parent.mkdir(parents=True,exist_ok=True)
 import verify_wc3_pathing_work242 as runner
 saved=runner.TESTS
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=saved
 result=dict(passed=True,binary_sha256=SHA,**verify_runtime(b,s),instructions=len(s['instructions']),engine=engine,fixture_sha256=digest(FIXTURE),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=s['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
