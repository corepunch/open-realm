#!/usr/bin/env python3
"""Verify Pow caller relationships, Shadow Strike recovery and retail getter parity."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import re
import sys
from verify_wc3_pathing_target_normalize218 import SHA, original_bytes
from verify_wc3_pathing_power import power, log, exp, exp_positive, reducedlog, corelog
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-power349-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
TESTS=['wc3_movement.power349*','wc3_spell.power349*','wc3_save.*','wc3_pools.*','wc3_facing.*']
def digest(data):return hashlib.sha256(data).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(P349 [^"\r\n]*)" \)',text)
def validate(spec,files):
    if spec['task']!='NUM-01.19' or spec['game_sha256']!=SHA or len(spec['instructions'])!=2387 or len(spec['functions'])!=38:
        raise ValueError('wrong caller inventory')
    if set(files)!=set(spec['files']) or any(digest(v.encode())!=spec['files'][n]for n,v in files.items()):raise ValueError('archive differs')
    hashes={digest(v.encode())for v in files.values()};observed=[];public=None;diagnostic=None
    for name in ('observe-e','repeat-f','control-g','observe-j'):
        rows=[json.loads(l)for l in files[name+'.jsonl'].splitlines()];meta,end=rows[0],rows[-1]
        trace=[r for r in rows if 'seq'in r];preload=files[name+'-preload.txt'];stream=markers(preload)
        if (meta.get('event')!='metadata' or meta.get('sha256')!=SHA or meta.get('task')!='NUM-01.19/power349' or
            not meta.get('owned') or (meta['env'],meta['display'],meta['remote'])!=('B',':98','127.0.0.1:27049') or
            meta['source_sha256']['map']!=spec['map_sha256'] or end.get('event')!='preload-file' or
            not end.get('complete') or end['markers']!=428 or len(stream)!=428 or digest(preload.encode())!=end['sha256'] or
            any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):
            raise ValueError('unowned/incomplete capture')
        for source,value in meta['source_sha256'].items():
            if source.startswith('power349_') and value not in hashes:raise ValueError('captured source absent')
        if public is None:public=stream
        elif stream!=public:raise ValueError('public repeat/control differs')
        if meta['mode']=='control':
            if trace or any(r['event']in('module','trace-end')for r in rows):raise ValueError('instrumented control')
        else:
            ends=[r for r in rows if r['event']=='trace-end']
            if (len(ends)!=1 or not ends[0].get('readOnly') or ends[0]['counts']!=dict(Counter(r['event']for r in trace)) or
                [r['seq']for r in trace]!=list(range(1,len(trace)+1)) or [r['value']for r in trace if r['event']=='marker']!=stream):
                raise ValueError('incomplete native stream')
            if name=='observe-j':diagnostic=trace
            else:observed.append([r for r in trace if r['event']!='module'])
    if len(observed)!=2 or observed[0]!=observed[1] or len(observed[0])!=spec['native_records']:raise ValueError('native repeat differs')
    trace=observed[0]
    if digest(json.dumps(trace,sort_keys=True,separators=(',',':')).encode())!=spec['native_sha256']:raise ValueError('native publications differ')
    public_rows=[];query=None;pending=[];calls=0
    models={'power':power,'exp':exp,'exp-quarter':exp_positive,'log':log,'log-reduced':reducedlog,'log-core':corelog}
    for r in trace:
        if r['event']=='scalar-enter':
            if r['alias'] or r.get('exponentAlias') or r.get('inputExponentAlias'):raise ValueError('actual caller alias differs')
            pending.append(r)
        if r['event']=='scalar-leave':
            if not pending:raise ValueError('unpaired scalar return')
            enter=pending.pop();name=enter['name'];model=models[name]
            expected=model(enter['input'],enter['exponent']) if name=='power' else model(enter['input'])
            if name!=r['name'] or expected!=r['result']:raise ValueError('caller operation order differs')
            calls+=1
        if r['event']=='public-values':query=r
        if r['event']=='marker':
            m=re.search(r'tick=(\d+) label=unit u=(\d+) order=(\d+)',r['value'])
            if not m:continue
            tick,index,order=map(int,m.groups())
            if query is None or (query['u'],query['tick'])!=(index,tick):raise ValueError('stale getter window')
            public_rows.append([index,tick,order,*query['values']]);query=None
    if pending or calls!=1351 or len(public_rows)!=422:raise ValueError('missing public/math states')
    curves=[r for r in diagnostic if r['event']=='curve-age']
    if len(curves)!=15 or curves[0]['value']!=0x3f800008:raise ValueError('elapsed clock differs')
    writes=[r for r in diagnostic if r['event']=='scalar-value-committed' and r['rate']==0x3e800000]
    if [r['base']for r in writes]!=[0x459c4000,0x4599e800,0x45999dff,0x459953fe,0x459909fd,0x4598bffc,0x459875fb]:
        raise ValueError('health scalar commits differ')
    return dict(captures=3,controls=1,public_markers=428,native_records=len(trace),scalar_calls=calls,public_rows=public_rows)
def check_header(result):
    text=(ROOT/'games/warcraft-3/game/tests/fixtures/retail_power349.h').read_text()
    body=text.split('power349_public[][8]={',1)[1].split('};',1)[0]
    actual=[[int(v.strip().removesuffix('u'),0)for v in row.split(',')]for row in re.findall(r'\{([^{}]+)\}',body)]
    if actual!=result['public_rows']:raise ValueError('engine literal evidence differs')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True);p.add_argument('--engine-library',type=Path,required=True)
    p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
    if a.report.exists():p.error('fresh report required')
    spec=json.loads(SPEC.read_text())
    if digest(BUNDLE.read_bytes())!=spec['bundle_sha256']:raise ValueError('bundle pin differs')
    result=validate(spec,json.loads(gzip.decompress(BUNDLE.read_bytes())));check_header(result);original_bytes(a.binary,spec['instructions'])
    sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
    from power349_callers import witness
    callers=witness(a.binary,a.engine_library)
    if callers!=spec['callers']:raise ValueError('original/engine caller witness differs')
    from wc3_pathfinding_engine_tests import run_engine
    engine=run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
    result['public_states']=len(result.pop('public_rows'));result.update(passed=True,status='verified',binary_sha256=SHA,instructions=len(spec['instructions']),caller_cases=callers['cases'],callers=callers,engine=engine)
    a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
