#!/usr/bin/env python3
"""Verify retail blocked-Move AI/guard transitions and current engine ownership."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import re
from verify_wc3_pathing_target_normalize218 import SHA, original_bytes

ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-order344-1.27.json'
BUNDLE=ROOT/'tools/ghidra/fixtures/retail-order344-inputs-1.27.json.gz'
SOURCES=['tools/frida/research/'+n for n in ('order344_probe.j','order344_make_map.py','order344_observer.js','stop284_capture.py')]
TESTS=['wc3_bot.*','wc3_save.*','wc3_movement.order344*','wc3_movement.recovery197*',
       'wc3_movement.completion194*','wc3_movement.public_timed_life*']

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def public(text):return re.findall(r'call Preload\( "(O344 [^"\r\n]*)" \)',text)
def native(rows):return [r for r in rows if 'seq' in r and r['event']!='module']

def validate_runtime(bundle,spec):
    if len(bundle)!=3:raise ValueError('two observed repeats and control required')
    for i,capture in enumerate(bundle):
        rows=capture['rows'];meta=rows[0];footer=rows[-1]
        if (meta.get('mode')!=('control' if i==2 else 'observe') or meta.get('sha256')!=SHA or
            not meta.get('owned') or meta['source_sha256']['map']!=spec['map_sha256'] or
            (meta.get('env'),meta.get('display'),meta.get('remote')) not in
            (('B',':98','127.0.0.1:27049'),('C',':99','127.0.0.1:27050'))):
            raise ValueError('capture provenance differs')
        for path in SOURCES[2:]:
            if meta['source_sha256'][Path(path).name]!=spec['source_sha256'][path]:
                raise ValueError('captured observer/controller differs')
        if (footer.get('event')!='preload-file' or not footer.get('complete') or
            footer['markers']!=1103 or len(spec['public'])!=1103 or public(capture['preload'])!=spec['public'] or
            hashlib.sha256(capture['preload'].encode()).hexdigest()!=footer['sha256']):
            raise ValueError('public markers differ or incomplete')
        if any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
            raise ValueError('capture failed')
        events=[r for r in rows if 'seq' in r];ends=[r for r in rows if r.get('event')=='trace-end']
        if i==2:
            if events or ends or any(r.get('event')=='module' for r in rows):raise ValueError('instrumented control')
        elif ([r['seq'] for r in events]!=list(range(1,len(events)+1)) or len(ends)!=1 or
              not ends[0].get('readOnly') or ends[0]['counts']!=dict(Counter(r['event'] for r in events)) or
              native(rows)!=spec['native'] or [r['value'] for r in events if r['event']=='marker']!=spec['public']):
            raise ValueError('native trace differs')
    rows=spec['native']
    failed=[r for r in rows if r['event']=='cant-path-enter']
    if [(r['scene'],r['tick'],bool(r['flags']&2)) for r in failed]!=[(0,43,False),(1,223,True),(2,403,True),(5,943,True)]:
        raise ValueError('AI branch witness changed')
    for scene,tick,point in ((1,223,[1132986368,1134034944]),(2,403,[1132986368,1134034944]),(5,943,[1136007793,1136943983])):
        returns=[r for r in rows if r['event']=='guard-return' and r['scene']==scene and r['tick']==tick]
        if len(returns)!=1 or returns[0]['point']!=point or returns[0]['caller']!='0x9cd600':
            raise ValueError('guard destination/dispatch changed')
    if [r['found'] for r in rows if r['event']=='find-transport-leave']!=[False]*3:
        raise ValueError('transport-null branch changed')
    samples={int(m.group(1)):m.group(2) for line in spec['public']
             if (m:=re.match(r'O344 tick=(\d+) scene=\d+ label=sample (.*)',line))}
    for tick in (550,551,700,730,731,880):
        if not samples[tick].startswith('order=0 '):raise ValueError('guard removal/owner transfer did not stop')
    for tick in (224,404,944):
        if not samples[tick].startswith('order=851983 '):raise ValueError('missing guard Attack')
    return dict(captures=2,controls=1,scenes=6,public_markers=3309,native_records=2*len(rows))

def validate(spec):
    if spec['game_sha256']!=SHA or set(spec['source_sha256'])!=set(SOURCES):raise ValueError('evidence contract changed')
    for path,expected in spec['source_sha256'].items():
        if digest(ROOT/path)!=expected:raise ValueError('source changed: '+path)
    if digest(BUNDLE)!=spec['bundle_sha256'] or len(spec['instructions'])!=spec['instruction_count']:
        raise ValueError('bundle/instruction guards changed')
    return validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
    if a.report.exists():p.error('fresh report required')
    spec=json.loads(SPEC.read_text());retail=validate(spec);original_bytes(a.binary,spec['instructions'])
    from wc3_pathfinding_engine_tests import run_engine
    engine=run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
    result=dict(passed=True,status='verified',retail=retail,engine=engine,instructions=len(spec['instructions']),
                game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'))
    a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
