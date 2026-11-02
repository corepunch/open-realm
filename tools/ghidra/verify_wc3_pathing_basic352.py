#!/usr/bin/env python3
"""Audit scalar callers, alias stores, startup and public formatting-to-Move conversion."""
import argparse,base64,ctypes as C,gzip,hashlib,json,re,subprocess,tempfile
from collections import Counter
from pathlib import Path
from research.basic352_callers import aliases,normalize,public_window,compare_scalars,caller_storage
from research.basic352_format import witness as original_format
from research.basic352_startup import witness as original_startup
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-basic352-1.27.json';BUNDLE=SPEC.with_suffix('.json.gz')
BASIC=['6f06f9c0','6f06fa90','6f06fbb0','6f06fcd0','6f070120','6f070170','6f070700','6f070c80','6f070d20','6f070d80','6f070fe0','6f0711e0','6f071250','6f071480','6f0715c0']
TRIG=['6f06ffa0','6f0703a0','6f070530','6f0705b0','6f070790','6f071280','6f071340','6f071590']
POWER=['6f06fd50','6f06fe10','6f06ff20','6f070c20','6f070f70','6f0710e0','6f071180']
EXTRA=['6f071570','6f071530','6f06fd30','6f070de0','6f0701d0','6f070f20','6f2103b0','6f2103f0']
VECTORS=['6f1d4c80','6f1d4d20','6f16e6c0']
def digest(raw):return hashlib.sha256(raw).hexdigest()
def canonical(value):return json.dumps(value,sort_keys=True,separators=(',',':')).encode()
def marker_lines(text,prefix):return re.findall(r'call Preload\( "('+prefix+r' [^"\r\n]*)" \)',text)
def inventory_contracts(inv):
 if set(inv['calls'])!=set(BASIC+TRIG+POWER+EXTRA+VECTORS)or len(inv['functions'])!=2008 or len(inv['instructions'])!=179722:raise ValueError('incomplete scalar inventory')
 if sum(len(inv['calls'][a])for a in BASIC)!=6721 or sum(map(len,inv['calls'].values()))!=7097:raise ValueError('reference inventory differs')
 ledger=[];functions={a:dict(line.split(' ',1)for line in f['assembly'].splitlines())for a,f in inv['functions'].items()}
 for target,refs in inv['calls'].items():
  family='basic'if target in BASIC else'trig'if target in TRIG else'power'if target in POWER else'vector'if target in VECTORS else'extended'
  for r in refs:
   if not r['entry']or r['entry']not in functions:raise ValueError('uncontained reference')
   asm=functions[r['entry']].get(r['site'])
   if asm is None or ('0x'+target)not in asm or r['site']not in inv['instructions']:raise ValueError('missing reference instruction')
   opcode=asm.split()[0]
   if opcode not in ('CALL','JMP','PUSH')and not(opcode=='MOV'and r['type']=='DATA'):raise ValueError('unclassified reference opcode '+asm)
   ledger.append(dict(target=target,family=family,site=r['site'],entry=r['entry'],opcode=opcode,legacy_type=r['type']))
 return ledger

def header(rows):
 return '/* Literal repeated-retail R2SW strings; parsed words compose the verified S2R model. */\nstatic struct { char const *expression, *value; uint32_t parsed; } const retail_basic352_format[] = {\n'+''.join('    { '+json.dumps(r['expression'])+', '+json.dumps(r['value'])+', 0x%08xu },\n'%r['parsed']for r in rows)+'};\n'
def validate(spec,files,library=None):
 if spec['task']!='NUM-01.17'or spec['game_sha256']!=SHA:raise ValueError('wrong contract')
 if set(files)!=set(spec['files'])or any(digest(v.encode())!=spec['files'][k]for k,v in files.items()):raise ValueError('archive differs')
 inv=json.loads(files['inventory.json']);ledger=inventory_contracts(inv)
 if digest(canonical(ledger))!=spec['ledger_sha256']:raise ValueError('caller ledger differs')
 storage=caller_storage(inv)
 if len(storage)!=spec['storage_sites']or digest(canonical(storage))!=spec['storage_sha256']:raise ValueError('caller storage differs')
 if len(json.loads(files['startup.json']))!=199:raise ValueError('incomplete startup inventory')
 if header(spec['public'])!=(ROOT/'games/warcraft-3/game/tests/fixtures/retail_basic352_format.h').read_text():raise ValueError('literal engine fixture differs')
 normalized=[];public_windows=[];format_rows=[];scalars=[]
 for series,prefix in [('basic','B352'),('format','F352')]:
  first=None;public=None
  for name,mode in [('observe-a','observe'),('observe-b','observe'),('control-a','control')]:
   key=series+'/'+name;rows=[json.loads(l)for l in files[key+'.jsonl'].splitlines()];meta,end=rows[0],rows[-1]
   if meta['event']!='metadata'or not meta.get('owned')or meta['task']!='NUM-01.17/basic352'or meta['mode']!=mode or meta['sha256']!=SHA or meta['source_sha256']['map']!=spec['maps'][series]or(meta['env'],meta['display'],meta['remote'])!=('B',':98','127.0.0.1:27049'):raise ValueError('unowned capture')
   if any(r.get('type')=='error'or r['event']=='trace-failed'for r in rows):raise ValueError('failed capture')
   for source,value in meta['source_sha256'].items():
    if source.endswith(('.py','.j','.js','.c'))and value not in spec['files'].values():raise ValueError('captured source missing')
   if end.get('event')!='preload-file'or not end.get('complete')or digest(files[key+'-preload.txt'].encode())!=end['sha256']:raise ValueError('incomplete preload')
   marks=marker_lines(files[key+'-preload.txt'],prefix)
   if len(marks)!=(81 if series=='basic' else 426)or not marks[0].endswith(' label=start')or not marks[-1].endswith(' label=complete'):raise ValueError('incomplete public markers')
   native=[r for r in rows if 'seq'in r and r['event']!='trace-end']
   if mode=='control':
    if native or any(r['event']=='trace-end'for r in rows):raise ValueError('instrumented control')
    if marks!=public:raise ValueError('observer-free public stream differs')
    continue
   footer=[r for r in rows if r['event']=='trace-end']
   if len(footer)!=1 or not footer[0].get('installed')or not footer[0].get('readOnly')or footer[0]['counts']!=dict(Counter(r['event']for r in native))or [r['seq']for r in native]!=list(range(len(native))):raise ValueError('incomplete native observation')
   if marks!=[r['value']for r in native if r['event']=='marker']:raise ValueError('preload/observed markers differ')
   if public is not None and public!=marks:raise ValueError('public repeat differs')
   public=marks
   if library:scalars.append(compare_scalars(rows,library))
   if series=='basic':
    window=public_window(rows)
    if len(window)!=2272 or digest(canonical(window))!=spec['public_window_sha256']:raise ValueError('synchronous public producer differs')
    public_windows.append(window);normalized.append(normalize(rows))
   else:
    stable=normalize(rows)
    if len(stable)!=9553 or digest(canonical(stable))!=spec['format_trace_sha256']:raise ValueError('format caller trace differs')
    if first is not None and stable!=first:raise ValueError('format repeat differs')
    first=stable
    enters=[r for r in rows if r['event']=='format-enter'];leaves={r['parent']:r for r in rows if r['event']=='format-leave'}
    if len(enters)!=423 or set(leaves)!={r['parent']for r in enters}:raise ValueError('incomplete formatting lifetimes')
    for i,row in enumerate(enters[:420]):
     expected=spec['public'][i]
     if (row['input'],row['width'],row['precision'],leaves[row['parent']]['value'])!=(expected['input'],expected['width'],expected['precision'],expected['value']):raise ValueError('public format operands/result differ')
    points=[r for r in rows if r['event']=='point-order']
    if len(points)!=1 or {k:v for k,v in points[0].items()if k!='seq'}!=dict(event='point-order',order=851986,x=0,y=0x44400000,c=1094):raise ValueError('formatted Move producer differs')
    if len([r for r in rows if r['event']=='getter'])!=6:raise ValueError('missing retail motion getters')
    format_rows.append(enters)
 if public_windows[0]!=public_windows[1]:raise ValueError('public callback repeat differs')
 if normalized[0]==normalized[1]:raise ValueError('diagnostic broad-query streams were relabelled as a deterministic frame contract')
 return dict(references=len(ledger),storage_sites=len(storage),functions=2008,instructions=179722,startup_thunks=199,captures=4,controls=2,public_window_records=2272,format_trace_records=9553,public_results=420,format_calls=423,markers=507,scalar_comparisons=sum(r['scalars']for r in scalars),diagnostic_broad_queries='nondeterministic outside the declared synchronous public window; no frame/trajectory parity claimed')
def main():
 from wc3_pathfinding_engine_tests import run_engine
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--engine-library',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text())
 if digest(BUNDLE.read_bytes())!=spec['bundle_sha256']:raise ValueError('bundle pin differs')
 files=json.loads(gzip.decompress(BUNDLE.read_bytes()));result=validate(spec,files,a.engine_library);inv=json.loads(files['inventory.json']);original_bytes(a.binary,inv['instructions'])
 raw_aliases=aliases(a.binary,a.engine_library)
 if raw_aliases!=spec['aliases']:raise ValueError('original alias matrix differs')
 startup=original_startup(a.binary,a.engine_library,json.loads(files['startup.json']))
 if digest(canonical(startup))!=spec['startup_sha256']:raise ValueError('original startup differs')
 formatter=original_format(a.binary,a.engine_library,spec['format_cases'])
 if digest(canonical(formatter))!=spec['formatter_sha256']:raise ValueError('original formatter differs')
 with tempfile.TemporaryDirectory(prefix='basic352-maps-')as tmp:
  tmp=Path(tmp);base=tmp/'base.w3m';base.write_bytes(base64.b64decode(files['maps/base.w3m.b64']))
  for series,builder in [('basic','basic352_make_map.py'),('format','basic352_format_make_map.py')]:
   out=tmp/(series+'.w3m');subprocess.run([__import__('sys').executable,str(ROOT/'tools/frida/research'/builder),'--base',str(base),'--output',str(out)],check=True,stdout=subprocess.DEVNULL)
   if digest(out.read_bytes())!=spec['maps'][series]:raise ValueError('map rebuild differs')
 result.update(passed=True,status='verified',binary_sha256=SHA,alias_cases=raw_aliases['cases'],partial_overlap_cases=raw_aliases['partial_overlap_cases'],formatter_cases=formatter['cases'],engine=run_engine(a.test_binary,a.data,a.report,['wc3_api.basic352*','wc3_api.pathfinding_public*','wc3_api.pathfinding_compiled*','wc3_facing.*'],timeout=300))
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
