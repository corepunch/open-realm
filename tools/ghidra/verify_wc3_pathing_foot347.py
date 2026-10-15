#!/usr/bin/env python3
"""Verify shared live/cached radii, captured target producers and saved public parity."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import re
from verify_wc3_pathing_target_normalize218 import SHA,original_bytes
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-foot347-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
TESTS=['wc3_movement.foot347*','wc3_movement.public_group_move_matches_original_member*',
       'wc3_movement.public_smart_follow_matches_original_target*','wc3_movement.shared189*',
       'wc3_movement.scheduler190*','wc3_movement.spell185*','wc3_movement.structure279*',
       'wc3_movement.flight280*','wc3_movement.follow278*',
       'wc3_movement.projectile_fine_search_uses_shared_class15_and_preserves_owner']
CALLERS={'6f9d3f38':'6f9d3bc0','6f9d46e4':'6f9d4600','6f5fc750':'6f5fc640',
         '6f6d3252':'6f6d3190','6f6d0681':'6f6d0630'}
def digest(data):return hashlib.sha256(data).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(F347 [^"\r\n]*)" \)',text)
def validate(spec,files):
    if spec['task']!='FOOT-01.3' or spec['game_sha256']!=SHA or len(spec['instructions'])!=962:
        raise ValueError('wrong radius contract')
    if ({r['site']:r['entry']for r in spec['target_callers']}!=CALLERS or
        any(r['type']!='UNCONDITIONAL_CALL'for r in spec['target_callers'])):
        raise ValueError('target producer inventory differs')
    if set(files)!=set(spec['files']) or any(digest(v.encode())!=spec['files'][k]for k,v in files.items()):
        raise ValueError('archive differs')
    hashes={digest(s.encode())for s in files.values()};observed=[];public=None
    for name in ['observe-p','repeat-q','control-o']:
        rows=[json.loads(l)for l in files[name+'.jsonl'].splitlines()];meta,end=rows[0],rows[-1]
        trace=[r for r in rows if 'seq'in r];preload=files[name+'-preload.txt'];stream=markers(preload)
        if (meta.get('event')!='metadata' or meta.get('sha256')!=SHA or meta.get('task')!='FOOT-01.3/foot347' or
            not meta.get('owned') or (meta['env'],meta['display'],meta['remote'])!=('B',':98','127.0.0.1:27049') or
            meta['source_sha256']['map']!=spec['map_sha256'] or end.get('event')!='preload-file' or
            not end.get('complete') or end['markers']!=1304 or len(stream)!=1304 or
            digest(preload.encode())!=end['sha256'] or
            any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):
            raise ValueError('unowned/incomplete capture')
        for source,value in meta['source_sha256'].items():
            if source.startswith('foot347_') and value not in hashes:raise ValueError('captured source absent')
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
    if len(observed)!=2 or observed[0]!=observed[1] or len(observed[0])!=4944:
        raise ValueError('native repeat differs')
    trace=observed[0];public_rows=[];xy=None
    for r in trace:
        if r['event']=='public-position':xy=r['xy']
        if r['event']=='marker':
            m=re.search(r'tick=(\d+) scene=0 label=unit u=(\d+) type=(-?\d+) order=(\d+)',r['value'])
            if not m:continue
            tick,index,code,order=map(int,m.groups())
            if xy is None:raise ValueError('missing public getter')
            if not code and xy!=[0,0]:raise ValueError('invalid handle getter/window differs')
            code=int.from_bytes((code&0xffffffff).to_bytes(4,'big'),'little')
            public_rows.append([index,tick,order,code,*xy]);xy=None
    positions={tuple(v-0x02800000 if v&0x7f800000 else v for v in r['xy']):r['u']
               for r in trace if r['event']=='public-position'and r['tick']==10}
    identities={}
    for r in trace:
        if r['event']=='commit-enter':
            for m in r['g']['members']:
                p=tuple(m['m']['pos']);key=tuple(m['m']['id'])
                if key not in identities and p in positions:identities[key]=positions[p]
    if len(identities)!=13:raise ValueError('missing original cohort identities')
    radii=[];changes=[];last={}
    for r in trace:
        if r['event']!='commit-enter' or not r['g']['shared']:continue
        g=r['g'];s=g['shared'];key=tuple(g['id'])
        mask=sum(1<<identities[tuple(m['m']['id'])]for m in g['members'])
        radii.append([r['c'],mask,s['radius'],g['cached']])
        state=(s['radius'],g['cached'])
        if last.get(key)!=state:changes.append([r['tick'],r['c'],g['count'],*state]);last[key]=state
    expected=[[22,1098,1,0x403e0000,0x403e0000],[22,1098,12,0x403e0000,0x403e0000],
              [30,1125,1,0x3ffc0000,0x403e0000],[30,1125,11,0x3ffc0000,0x403e0000],
              [40,1158,11,0x3f780000,0x403e0000]]
    if changes!=expected or len(radii)!=253 or len(public_rows)!=1209:
        raise ValueError('largest-member radius/cache lifecycle differs')
    return dict(captures=3,public_markers=len(public),native_records=len(trace),public_rows=public_rows,radii=radii,changes=changes)
def check_header(result):
    text=(ROOT/'games/warcraft-3/game/tests/fixtures/retail_foot347.h').read_text()
    for name,expected in [('public',result['public_rows']),('radii',result['radii'])]:
        body=text.split('foot347_'+name+'[][',1)[1].split('={',1)[1].split('};',1)[0]
        actual=[[int(v.strip().removesuffix('u'),0)for v in row.split(',')]for row in re.findall(r'\{([^{}]+)\}',body)]
        if actual!=expected:raise ValueError('engine literal evidence differs: '+name)
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
    result.pop('public_rows');result['shared_states']=len(result.pop('radii'))
    result.update(passed=True,status='verified',binary_sha256=SHA,instructions=len(spec['instructions']),engine=engine)
    a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
