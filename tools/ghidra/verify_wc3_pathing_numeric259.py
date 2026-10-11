#!/usr/bin/env python3
"""Verify the original numeric DFA, real source compilation and engine Move producer."""
from retail_capture_source import matches_capture_source
import argparse,gzip,hashlib,json,re,sys
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-numeric259-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SOURCES=['tools/frida/research/numeric259_'+s for s in ('capture.py','observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c',
 'tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py',
 'tools/ghidra/research/Numeric259Evidence.java','tools/ghidra/research/lexer259_oracle.py',
 'tools/ghidra/research/foot03_spatial_harness_copy.py','tools/ghidra/research/foot03_rig.py',
 'tools/ghidra/verify_wc3_pathing_numeric.py','tools/ghidra/fixtures/retail-numeric259-1.27.h']
TESTS=['wc3_jass_map.*','wc3_api.pathfinding_compiled*','wc3_save.round_trip_jass*',
 'wc3_save.resumes_sleeping_jass_coroutine']
PATTERNS=[(42,r'[1-9][0-9]*'),(43,r'0[0-7]*'),(44,r'\$[0-9a-fA-F]+'),
 (45,r'0[xX][0-9a-fA-F]+'),(46,r'(?:[0-9]+\.[0-9]*|\.[0-9]+)')]
BAD=['078','08','09','0x','0xG','$','$G','1e3','1.2e3','0x1p4','1.2.3','.']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(N259 [^"\r\n]*)" \)',text)
def primary(rows):
 result=[]
 for r in rows:
  if r.get('event')in('source-token','compile-error'):result.append(r)
  if r.get('event')=='compile-error':break
 return result

def header(rows):
 def c(s):return '"'+''.join('\\%03o'%ord(x)for x in s)+'"'
 lines=['/* Frozen original9249d0 first tokens; generated from NUM-01.15/payoff259. */',
 'static struct { char const *source, *token; jlexNumberKind_t kind; } const numeric259_tokens[] = {']
 selected=[r for r in rows if r['input'][0]in'0123456789.$+-']
 for r in selected:
  kind='JLEX_REAL'if r['action']==46 else'JLEX_INTEGER'if r['action']in(42,43,44,45)else'JLEX_NOT_NUMBER'
  lines.append('    { '+c(r['input'])+', '+c(r['text'])+', '+kind+' },')
 return '\n'.join(lines+['};'])+'\n'

def grammar(original):
 rows=original['rows'];states=original['numeric_states'];lookup={s['state']:s for s in states}
 if len(rows)!=5139 or len(states)!=16 or len(lookup)!=16:raise ValueError('numeric exploration incomplete')
 for s in states:
  if len(s['transitions'])!=256 or any(a>>9 for a in s['actions']):raise ValueError('DFA trailing context differs')
  for ch,nxt in enumerate(s['transitions']):
   if nxt!=231 and nxt not in lookup:raise ValueError('DFA state missing')
   expected=[a for a,p in PATTERNS if re.fullmatch(p,s['prefix']+chr(ch))]
   actual=[]if nxt==231 else[a for a in lookup[nxt]['actions']if a in range(42,47)]
   if actual!=expected:raise ValueError('numeric DFA language differs')
 for r in rows:
  matches=[(len(m[0]),a,m[0])for a,p in PATTERNS if(m:=re.match(p,r['input']))]
  if matches:
   n,a,text=max(matches)
   if (r['length'],r['action'],r['text'])!=(n,a,text)or not isinstance(r.get('word'),int):raise ValueError('numeric prefix/conversion differs')
  elif r['action']in range(42,47):raise ValueError('non-number became numeric')
 if (ROOT/SOURCES[-1]).read_text()!=header(rows):raise ValueError('engine header differs from original')
 return dict(original_cases=len(rows),numeric_states=len(states),dfa_edges=4096,engine_lexer_cases=4872)

def runtime(bundle,spec):
 captures=bundle['captures'];maps=bundle['maps']
 if len(captures)!=27 or set(maps)!={'valid',*('bad'+str(i)for i in range(12))}:raise ValueError('missing source repetitions/control')
 counts={}
 for c in captures:
  name=c['name'];rows=c['rows'];meta=rows[0];end=rows[-1];mode=meta['mode'];counts[name,mode]=counts.get((name,mode),0)+1
  if (meta.get('task')!='NUM-01.15'or not meta['owned']or meta['env']not in('B','C')or meta['sha256']!=SHA or
      meta['source_sha256']!={**spec['capture_sources'],'map':maps[name]['map_sha256']}or
      any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):
   raise ValueError('capture provenance/failure')
  if name=='valid':
   text=c['preload']
   if not end.get('complete')or end['markers']!=2 or hashlib.sha256(text.encode()).hexdigest()!=end['sha256']or markers(text)!=spec['markers']:raise ValueError('public numeric producer/control differs')
   events=[r for r in rows if r.get('event')in('source-token','compile-result')]
   if mode=='control':
    if events or any(r.get('event')in('module','trace-end','compile-error')for r in rows):raise ValueError('control instrumented')
   elif events!=spec['positive_source']or [r for r in rows if r.get('event')=='trace-end']!=[dict(event='trace-end',installed=True,readOnly=True,tokens=414,results=2,open=0)]or any(r.get('event')=='compile-error'for r in rows):raise ValueError('valid source compilation differs')
  else:
   if mode!='observe'or end!={'event':'preload-file','missing':True,'expected_reject':True,'observed_reject':True}or primary(rows)!=spec['invalid_source'][name]:raise ValueError('invalid source accepted/diagnostic differs')
   if not any(r.get('event')in('trace-end','observer-unavailable-after-diagnostic')for r in rows):raise ValueError('missing diagnostic completion boundary')
 for name in maps:
  if counts.get((name,'observe'))!=2 or counts.get((name,'control'),0)!=(1 if name=='valid'else 0):raise ValueError('missing repeat/control')
  m=maps[name]
  expression='nan+inf+Infinity+089.5+I2R(077)+I2R(0Xf)+I2R($aF)'if name=='valid'else BAD[int(name[3:])]
  rendered=(ROOT/SOURCES[3]).read_text().replace('@EXPRESSION@',expression).encode()
  if m['expression']!=expression or m['probe_sha256']!=hashlib.sha256(rendered).hexdigest()or m['fine_origin']!=[0,0]or m['shadow_size']!=4096:raise ValueError('source map/geometry differs')
  for key,path in [('target_builder_sha256',SOURCES[2]),('probe_template_sha256',SOURCES[3]),('builder_sha256',SOURCES[6]),('shared_builder_sha256',SOURCES[7])]:
   if m[key]!=spec['pins'][path]:raise ValueError('map provenance differs')
 return dict(captures=26,controls=1,invalid_expressions=12,source_tokens=828,successful_compile_returns=4,public_markers=6)

def validate(spec):
 if spec['version']!=1 or spec['task']!='NUM-01.15'or spec['game_sha256']!=SHA or set(spec['pins'])!=set(SOURCES)or spec['engine_tests']!=TESTS:raise ValueError('numeric contract differs')
 for p,h in spec['pins'].items():
  if not matches_capture_source(ROOT,p,h):raise ValueError('source changed '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('bundle differs')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));grammar(bundle['original']);runtime(bundle,spec)
 return bundle

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 s=json.loads(FIXTURE.read_text());b=validate(s);original_bytes(a.binary,s['instructions'])
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));import lexer259_oracle as native
 if native.original(a.binary)!=b['original']:raise ValueError('fresh original lexer differs')
 import verify_wc3_pathing_work242 as runner
 before=runner.TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=before
 result=dict(passed=True,task=s['task'],**grammar(b['original']),**runtime(b,s),instructions=len(s['instructions']),engine=engine,binary_sha256=SHA,fixture_sha256=digest(FIXTURE),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=s['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
