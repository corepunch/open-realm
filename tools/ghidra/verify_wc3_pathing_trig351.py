#!/usr/bin/env python3
"""Audit complete trig caller output definitions, original aliases and public retail operands."""
import argparse,base64,ctypes as C,gzip,hashlib,json,re,subprocess,tempfile
from collections import Counter
from pathlib import Path
from research.trig351_callers import ledger,witness
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
import verify_wc3_pathing_numeric as n
from generate_wc3_math_tables import sine_table,acos_tables,reciprocal_table
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-trig351-1.27.json';BUNDLE=SPEC.with_suffix('.json.gz')
PUBLIC={0x1f75d0:'Acos',0x1f8250:'Asin',0x1f8290:'Atan2',0x1f8310:'Atan',0x1f9580:'Cos',0x215d00:'Sin',0x216750:'Tan'}
def digest(data):return hashlib.sha256(data).hexdigest()
def trace(rows):return [{k:v for k,v in r.items()if k!='seq'}for r in rows if r['event']in ('marker','caller-enter','caller-leave','scalar-enter','scalar-leave')]
def markers(text):return re.findall(r'call Preload\( "(T351 [^"\r\n]*)" \)',text)
def header(public):
 return '/* Immutable public native returns from repeated retail Trig351 observations. */\nstatic struct { char const *expression; uint32_t word; } const retail_trig351[] = {\n'+''.join('    { '+json.dumps(r['expression'])+', 0x%08xu },\n'%r['result']for r in public)+'};\n'
def models():
 a,b=acos_tables();rec=reciprocal_table();sines=sine_table()
 return {0x6ffa0:('acos',lambda x:n.acos_bits(x,a,b)),0x703a0:('asin',lambda x:n.asin_bits(x,a,b)),
  0x70530:('atan2',lambda y,x:n.atan2_bits(y,x,rec)),0x705b0:('atan',lambda x:n.atan_bits(x,rec)),
  0x70790:('cos',lambda x:n.trig_bits(x,True,sines)),0x71280:('sin',lambda x:n.trig_bits(x,False,sines)),
  0x71340:('sincos',lambda x:[n.trig_bits(x,False,sines),n.trig_bits(x,True,sines)]),
  0x71590:('tan',lambda x:n.divide(n.trig_bits(x,False,sines),n.trig_bits(x,True,sines),rec))}
def validate(spec,files,library=None):
 if spec['task']!='NUM-01.18'or spec['game_sha256']!=SHA:raise ValueError('wrong contract')
 if set(files)!=set(spec['files'])or any(digest(v.encode())!=spec['files'][k]for k,v in files.items()):raise ValueError('archive differs')
 inventory=json.loads(files['inventory.json'])
 if len(inventory['functions'])!=70 or len(inventory['instructions'])!=6897:raise ValueError('incomplete function/instruction inventory')
 try:sites=ledger(inventory)
 except (KeyError,AssertionError)as error:raise ValueError('invalid output definition inventory')from error
 if len(sites)!=140 or sites!=spec['ledger']:
  raise ValueError('caller/instruction inventory differs')
 if Counter(r['disposition']for r in sites)!={'own-frame-output':132,'forwarded-addresses':7,'value-angle-forwarded-outputs':1}:
  raise ValueError('output definitions differ')
 expected=None;public=None;scalar_count=0;model=models();engine=C.CDLL(str(library.resolve()))if library else None
 for name,mode in [('observe-a','observe'),('observe-b','observe'),('control-a','control')]:
  rows=[json.loads(l)for l in files[name+'.jsonl'].splitlines()];meta=rows[0];end=rows[-1]
  if (meta['event']!='metadata'or not meta.get('owned')or meta['task']!='NUM-01.18/trig351'or meta['mode']!=mode or
      meta['sha256']!=SHA or meta['source_sha256']['map']!=spec['map_sha256']or
      (meta['env'],meta['display'],meta['remote'])!=('B',':98','127.0.0.1:27049')):raise ValueError('capture provenance differs')
  if any(r.get('type')=='error'or r['event']=='trace-failed'for r in rows):raise ValueError('failed capture')
  for source,value in meta['source_sha256'].items():
   if source.endswith(('.j','.js','.c','.py'))and value not in spec['files'].values():raise ValueError('captured source missing')
  if end.get('event')!='preload-file'or not end.get('complete')or digest(files[name+'-preload.txt'].encode())!=end['sha256']:
   raise ValueError('missing complete preload')
  native=[r for r in rows if 'seq'in r and r['event']!='trace-end']
  if mode=='control':
   if native or any(r['event']=='trace-end'for r in rows):raise ValueError('hooked control')
   if markers(files[name+'-preload.txt'])!=[r['value']for r in expected if r['event']=='marker']:raise ValueError('observer-free markers differ')
   continue
  finish=[r for r in rows if r['event']=='trace-end']
  if len(finish)!=1 or not finish[0].get('installed')or not finish[0].get('readOnly')or finish[0]['counts']!=dict(Counter(r['event']for r in native))or [r['seq']for r in native]!=list(range(len(native))):raise ValueError('incomplete observation')
  stable=trace(rows)
  if digest(json.dumps(stable,sort_keys=True,separators=(',',':')).encode())!=spec['trace_sha256']:raise ValueError('frozen observation differs')
  if expected is not None and stable!=expected:raise ValueError('repeat differs')
  expected=stable
  observed_markers=[r['value']for r in stable if r['event']=='marker']
  if len(observed_markers)!=98 or not observed_markers[0].endswith(' label=start')or not observed_markers[-1].endswith(' label=complete')or markers(files[name+'-preload.txt'])!=observed_markers:raise ValueError('public markers differ')
  enter={r['id']:r for r in native if r['event']in ('caller-enter','scalar-enter')};leave={r['id']:r for r in native if r['event']in ('caller-leave','scalar-leave')}
  if len(enter)!=848 or set(enter)!=set(leave):raise ValueError('incomplete native lifetimes')
  for key,row in enter.items():
   after=leave[key]
   if row['c']!=after['c']:raise ValueError('query advanced owner clock')
   if row['event']!='scalar-enter':continue
   if row['parent']not in enter or enter[row['parent']]['event']!='caller-enter':raise ValueError('unbound scalar caller')
   ni=len(row['inputs']);count=ni+len(after['outputs'])
   if len(row['equality'])!=count or any(len(v)!=count for v in row['equality'])or any(v[i]for v in row['equality'][ni:]for i in range(ni)):raise ValueError('observed helper operands alias')
   if after['inputs']!=row['inputs']:raise ValueError('input overwritten')
   op,fn=model[row['entry']];value=fn(*row['inputs']);value=value if isinstance(value,list)else[value]
   if value!=after['outputs']:raise ValueError('original scalar/model differs')
   if engine:
    f=getattr(engine,'pathing_'+op)
    if op=='sincos':
     out=(C.c_uint32*2)();f(C.c_uint32(row['inputs'][0]),out);actual=list(out)
    else:f.restype=C.c_uint32;actual=[f(*(C.c_uint32(v)for v in row['inputs']))]
    if actual!=value:raise ValueError('engine scalar differs')
  scalar_count=len([r for r in enter.values()if r['event']=='scalar-enter'])
  if scalar_count!=454:raise ValueError('missing scalar family')
  inputs=re.findall(r'call T351Value\(([^,]+),([^\)]+)\)',files['sources/trig351_probe.j']);calls=[r for r in native if r['event']=='caller-enter'and r['entry']in PUBLIC]
  if len(inputs)!=12 or len(calls)!=84:raise ValueError('missing public native cases')
  current=[]
  for i,row in enumerate(calls):
   a,b=inputs[i//7];op=PUBLIC[row['entry']];expr=f'{op}({a}'+(','+b if op=='Atan2'else'')+')'
   current.append(dict(expression=expr,arguments=row['arguments'],result=leave[row['id']]['result'],entry=row['entry']))
  if current!=spec['public']:raise ValueError('literal public results differ')
  public=current
 if header(public)!= (ROOT/'games/warcraft-3/game/tests/fixtures/retail_trig351.h').read_text():raise ValueError('engine public fixture differs')
 return dict(captures=2,controls=1,public_results=len(public),scalar_calls=scalar_count,markers=98,sites=len(sites),functions=70,instructions=6897)
def main():
 from wc3_pathfinding_engine_tests import run_engine
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--engine-library',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text())
 if digest(BUNDLE.read_bytes())!=spec['bundle_sha256']:raise ValueError('bundle pin differs')
 files=json.loads(gzip.decompress(BUNDLE.read_bytes()));inv=json.loads(files['inventory.json']);original_bytes(a.binary,inv['instructions'])
 result=validate(spec,files,a.engine_library);forward=witness(a.binary,a.engine_library)
 if forward!=spec['forwarding']:raise ValueError('original forced-alias results differ')
 with tempfile.TemporaryDirectory(prefix='trig351-map-')as tmp:
  tmp=Path(tmp);base=tmp/'base.w3m';base.write_bytes(base64.b64decode(files['base.w3m.b64']));out=tmp/'new.w3m'
  subprocess.run([__import__('sys').executable,str(ROOT/'tools/frida/research/trig351_make_map.py'),'--base',str(base),'--output',str(out)],check=True,stdout=subprocess.DEVNULL)
  if digest(out.read_bytes())!=spec['map_sha256']:raise ValueError('rebuilt map differs')
 result.update(passed=True,status='verified',binary_sha256=SHA,forwarding=forward,engine=run_engine(a.test_binary,a.data,a.report,['wc3_api.trig351*','wc3_facing.*'],timeout=300))
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
