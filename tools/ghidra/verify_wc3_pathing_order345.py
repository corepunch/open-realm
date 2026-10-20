#!/usr/bin/env python3
"""Verify retail blocked-Attack alternatives, forwarding and engine ownership."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import re
from verify_wc3_pathing_target_normalize218 import SHA, original_bytes

ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-order345-1.27.json'
BUNDLE=ROOT/'tools/ghidra/fixtures/retail-order345-inputs-1.27.json.gz'
SOURCES=['tools/frida/research/'+n for n in ('order345_probe.j','order345_make_map.py','order345_observer.js','stop284_capture.py')]
TESTS=['wc3_combat.*','wc3_order_lifecycle.*','wc3_save.*','wc3_movement.order345*',
       'wc3_movement.order344*','wc3_movement.target166*','wc3_movement.target222*','wc3_movement.target223*','wc3_movement.attack306*','wc3_movement.policy245*']

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def public(text):return re.findall(r'call Preload\( "(O345 [^"\r\n]*)" \)',text)
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
            footer['markers']!=759 or len(spec['public'])!=759 or public(capture['preload'])!=spec['public'] or
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
    failures=[r for r in rows if r['event']=='cant-path-enter']
    if Counter(r['scene'] for r in failures)!={0:10,1:4,2:1,3:21}:
        raise ValueError('failure branches changed')
    if any(bool(r['flags']&2)!=(r['scene']!=0) for r in failures):
        raise ValueError('AI ownership changed')
    candidates=[r for r in rows if r['event']=='alternative-enter']
    selected=[r for r in rows if r['event']=='alternative-leave']
    if Counter(r['scene'] for r in candidates)!={2:1,3:21} or len(selected)!=22:
        raise ValueError('alternative coverage differs')
    for before,after in zip(candidates,selected):
        # Historical observer field name: invulnerable is actually Unit+ec,
        # acquisition prevention. Script invulnerability is widgetFlags bit8.
        if (before['invulnerable']!=0 or bool(before['candidate']['widgetFlags']&8)!=(before['scene']==3) or
            before['best']!=0x7f7fffff or not after['selected'] or
            before['candidate']['identity']!=after['candidate'] or after['best']!=1168985313):
            raise ValueError('nearest alternative predicate changed')
    for scene in (2,3):
        if any(r['event']=='recover-enter' and r['scene']==scene for r in rows):
            raise ValueError('selected alternative incorrectly recovered old movement')
        choices=[r['candidate'] for r in selected if r['scene']==scene]
        issued=[r for r in rows if r['event']=='ai-append' and r['scene']==scene and r['caller']=='0x60328c']
        expected=choices if scene==2 else [[0xffffffff,0xffffffff] for _ in choices]
        if [r['unit']['attackTarget'] for r in issued]!=expected:
            raise ValueError('target Attack admission differs')
    # Scene1 has no alternative; the probe labels null target equality too.
    if sum('label=event_851983_alternative ' in r and ('scene=2 ' in r or 'scene=3 ' in r)
           for r in spec['public'])!=22:
        raise ValueError('public target order notification missing')
    return dict(captures=2,controls=1,scenes=4,public_markers=2277,native_records=2526)

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
