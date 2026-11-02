#!/usr/bin/env python3
"""Verify request parameter overwrites against frozen retail getters and current engine."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import re
from verify_wc3_pathing_target_normalize218 import SHA, original_bytes
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-move348-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
TESTS=['wc3_movement.move348*','wc3_movement.shared189*','wc3_movement.scheduler190*',
       'wc3_movement.public_group_move_matches_original_member*','wc3_movement.public_group_move_matches_original_largest*']
def digest(data):return hashlib.sha256(data).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(M348 [^"\r\n]*)" \)',text)
def validate(spec,files):
    if spec['task']!='MOVE-01.3' or spec['game_sha256']!=SHA or len(spec['instructions'])!=1383 or len(spec['functions'])!=24:
        raise ValueError('wrong parameter contract')
    if set(files)!=set(spec['files']) or any(digest(v.encode())!=spec['files'][k]for k,v in files.items()):
        raise ValueError('archive differs')
    hashes={digest(s.encode())for s in files.values()};observed=[];public=None
    for name in ['observe-d','repeat-e','control-c']:
        rows=[json.loads(l)for l in files[name+'.jsonl'].splitlines()];meta,end=rows[0],rows[-1]
        trace=[r for r in rows if 'seq'in r];preload=files[name+'-preload.txt'];stream=markers(preload)
        if (meta.get('event')!='metadata' or meta.get('sha256')!=SHA or meta.get('task')!='MOVE-01.3/move348' or
            not meta.get('owned') or (meta['env'],meta['display'],meta['remote'])!=('B',':98','127.0.0.1:27049') or
            meta['source_sha256']['map']!=spec['map_sha256'] or end.get('event')!='preload-file' or
            not end.get('complete') or end['markers']!=549 or len(stream)!=549 or
            digest(preload.encode())!=end['sha256'] or
            any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):
            raise ValueError('unowned/incomplete capture')
        for source,value in meta['source_sha256'].items():
            if source.startswith('move348_') and value not in hashes:raise ValueError('captured source absent')
        if public is None:public=stream
        elif stream!=public:raise ValueError('public repeat/control differs')
        if meta['mode']=='control':
            if trace or any(r['event']in('module','trace-end')for r in rows):raise ValueError('instrumented control')
        else:
            ends=[r for r in rows if r['event']=='trace-end']
            if (len(ends)!=1 or not ends[0].get('readOnly') or
                ends[0]['counts']!=dict(Counter(r['event']for r in trace)) or
                [r['seq']for r in trace]!=list(range(1,len(trace)+1)) or
                [r['value']for r in trace if r['event']=='marker']!=stream):
                raise ValueError('incomplete native stream')
            observed.append([r for r in trace if r['event']!='module'])
    if len(observed)!=2 or observed[0]!=observed[1] or len(observed[0])!=2885:
        raise ValueError('native repeat differs')
    trace=observed[0]
    canonical=json.dumps(trace,sort_keys=True,separators=(',',':')).encode()
    if digest(canonical)!=spec['native_sha256']:raise ValueError('frozen native publications differ')
    public_rows=[];query=None
    for r in trace:
        if r['event']=='public-values':query=r
        if r['event']=='marker':
            m=re.search(r'tick=(\d+) label=unit u=(\d+) order=(\d+)',r['value'])
            if not m:continue
            tick,index,order=map(int,m.groups())
            if query is None or (query['u'],query['tick'])!=(index,tick):raise ValueError('stale getter window')
            public_rows.append([index,tick,order,*query['values']]);query=None
    if len(public_rows)!=488:raise ValueError('missing raw public values')
    abilities=[(r['kind'],r['code'],r['result'])for r in trace if r['event']=='ability-result']
    if abilities!=[('remove',0x416d6f76,1),('add',0x416d6f76,0)]:raise ValueError('intrinsic factory differs')
    writes=Counter((r['field'],r['caller'])for r in trace if r['event']=='write-enter')
    if writes!=Counter({('maximum','0x5c60d'):54,('increment','0x5c61b'):54,('turn','0x5c903'):12,
       ('window','0x5c8b4'):12,('maximum','0x5baf7'):6,('increment','0x5bb06'):6,
       ('maximum','0x5a6f1'):4,('increment','0x5a702'):4}):raise ValueError('writer admission/order differs')
    return dict(captures=3,public_markers=len(public),native_records=len(trace),public_rows=public_rows,writes=sum(writes.values()))
def check_header(result):
    text=(ROOT/'games/warcraft-3/game/tests/fixtures/retail_move348.h').read_text()
    body=text.split('move348_public[][9]={',1)[1].split('};',1)[0]
    actual=[[int(v.strip().removesuffix('u'),0)for v in row.split(',')]for row in re.findall(r'\{([^{}]+)\}',body)]
    if actual!=result['public_rows']:raise ValueError('engine literal evidence differs')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
    if a.report.exists():p.error('fresh report required')
    spec=json.loads(SPEC.read_text())
    if digest(BUNDLE.read_bytes())!=spec['bundle_sha256']:raise ValueError('bundle pin differs')
    result=validate(spec,json.loads(gzip.decompress(BUNDLE.read_bytes())));check_header(result)
    original_bytes(a.binary,spec['instructions'])
    from wc3_pathfinding_engine_tests import run_engine
    engine=run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
    result['public_states']=len(result.pop('public_rows'))
    result.update(passed=True,status='verified',binary_sha256=SHA,instructions=len(spec['instructions']),engine=engine)
    a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
