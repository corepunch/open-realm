#!/usr/bin/env python3
"""Verify retained modal-off packets against unchanged retail pause captures."""
import argparse,gzip,hashlib,json,re,sys
from collections import Counter
from pathlib import Path
import verify_wc3_pathing_pause287 as parent
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-pause288-1.27.json'
SCENES=[0,2,3,19,11,12]
SOURCES=['tools/ghidra/research/Pause288Evidence.java','tools/ghidra/research/pause288_oracle.py','tools/frida/research/pause288_observer.js']
TESTS=['wc3_movement.pause288*']+parent.TESTS+['wc3_save.*','wc3_spell.defend*']
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def projection(public):
 rows={str(n):[] for n in SCENES}
 for marker in public:
  a=dict(x.split('=',1) for x in marker.split()[1:]);n=int(a['scene'])
  if n not in SCENES or a['label'] in ('scene-start','settled','complete','start','selector-0'):continue
  rows[str(n)].append([a[k]for k in ('label','order','paused','px','py','target','speed','defend')])
 return rows
def header(rows,counts):
 s='/* Retail d51e5680, accepted Payoff287 final1/final3/control final2.\n * Unchanged public values; trajectory and post-nested getters are separate contracts. */\n'
 s+='static struct {char const *label;uint32_t order;bool paused;float px,py;bool target;float speed;unsigned defend;} const pause288_rows[20][24]={\n'
 for n in SCENES:
  s+='    [%u]={'%n+','.join('{'+json.dumps(a[0])+','+a[1]+','+a[2]+','+a[3]+'f,'+a[4]+'f,'+a[5]+','+a[6]+'f,'+a[7]+'}'for a in rows[str(n)])+'},\n'
 return s+'};\n/* Read-only native pause-exit user counts, including forced packets. */\nstatic uint32_t const pause288_counts[20]={'+','.join('['+str(k)+']='+str(v) for k,v in counts.items())+'};\n'
def validate_ownership(spec,bundle,base,parent_bundle):
 old_names={r['event']for r in parent_bundle['captures'][0]['rows']if 'seq'in r}
 if len(bundle['captures'])!=2:raise ValueError('ownership repeats missing')
 for capture in bundle['captures']:
  rows=capture['rows'];native=[r for r in rows if 'seq'in r];footer=rows[-1];meta=rows[0]
  if meta.get('mode')!='observe' or meta.get('sha256')!=SHA or not meta.get('owned') or meta['env']not in ('B','C') or meta['source_sha256']['map']!=base['map_sha256']:raise ValueError('ownership provenance differs')
  if meta['source_sha256']['modal_observer.js']!=spec['source_sha256']['tools/frida/research/pause288_observer.js']:raise ValueError('observer source changed')
  if parent.markers(capture['preload'])!=base['public'] or digest_bytes(capture['preload'].encode())!=footer['sha256'] or not footer.get('complete') or footer.get('markers')!=285:raise ValueError('ownership public/control differs')
  if [r['seq']for r in native]!=list(range(1043)) or any(r.get('type')=='error' or r.get('event')=='trace-failed'for r in rows):raise ValueError('ownership stream incomplete')
  ends=[r for r in rows if r.get('event')=='trace-end']
  if len(ends)!=1 or not ends[0]['readOnly'] or not ends[0]['installed'] or ends[0]['depth'] or ends[0]['counts']!=dict(Counter(r['event']for r in native)):raise ValueError('unbalanced ownership observer')
  kept=[];index=0
  for row in rows:
   if 'seq'in row:
    if row['event']not in old_names:continue
    row=dict(row,seq=index);index+=1
   kept.append(row)
  if index!=815 or parent.chronology_hash(kept)!=base['chronology_sha256']:raise ValueError('original native chronology changed')
  calls={str(i):[[x['caller'],x['disabled']]for x in rows if x.get('event')=='modal-enter' and x.get('scene')==i]for i in range(20)}
  if calls!=spec['modal_calls'] or calls['11']!=[['0x40771e',[1,0,0]],['0x3f4ca2',[1,0,0]]] or calls['12']!=[['0x40771e',[1,0,0]],['0x3f4ca2',[2,0,0]]]:raise ValueError('retirement producers differ')
 return dict(captures=2,native_events=2086,public_markers=570,retail_core_unchanged=True)
def digest_bytes(data):return hashlib.sha256(data).hexdigest()
def validate(spec):
 if spec['game_sha256']!=SHA or set(spec['source_sha256'])!=set(SOURCES) or spec['engine_tests']!=TESTS:raise ValueError('contract differs')
 for name,h in spec['source_sha256'].items():
  if digest(ROOT/name)!=h:raise ValueError('source changed '+name)
 base=json.loads(parent.SPEC.read_text());bundle=parent.validate(base)
 if digest(parent.SPEC)!=spec['parent_sha256'] or projection(base['public'])!=spec['projection']:raise ValueError('retail parent/projection changed')
 counts={str(d['scene']):d['state']['count'] for d in bundle['captures'][0]['rows'] if d.get('event')=='pause-exit' and d.get('scene') in SCENES}
 if counts!=spec['counts']:raise ValueError('native queue counts changed')
 if (ROOT/'games/warcraft-3/game/tests/fixtures/retail_pause288.h').read_text()!=header(spec['projection'],spec['counts']):raise ValueError('engine fixture differs from retail')
 if digest(SPEC.with_suffix('.json.gz'))!=spec['bundle_sha256']:raise ValueError('ownership bundle changed')
 ownership=json.loads(gzip.decompress(SPEC.with_suffix('.json.gz').read_bytes()));validate_ownership(spec,ownership,base,bundle)
 return bundle,base

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());bundle,base=validate(spec);original_bytes(a.binary,spec['instructions']);original_bytes(a.binary,base['instructions'])
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));import pause288_oracle,pause287_oracle
 original=pause288_oracle.original(a.binary)
 if original!=spec['original'] or len(original)!=32 or pause288_oracle.counters(a.binary)!=spec['counters'] or len(spec['counters'])!=81 or pause287_oracle.original(a.binary)!=bundle['stages']:raise ValueError('original execution differs')
 import verify_wc3_pathing_work242 as runner
 before=runner.TESTS
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=before
 report=dict(passed=True,status='verified',retail=parent.validate_runtime(bundle,base),ownership=validate_ownership(spec,json.loads(gzip.decompress(SPEC.with_suffix('.json.gz').read_bytes())),base,bundle),engine_projection_scenes=len(SCENES),original_cases=213,instructions=len(spec['instructions']),engine=engine,binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['scope'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
