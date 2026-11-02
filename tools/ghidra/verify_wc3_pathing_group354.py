#!/usr/bin/env python3
"""Verify complete public MagicLeash acquisition repeats and production regressions."""
import argparse, base64, gzip, hashlib, json, re, subprocess, sys, tempfile
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_group341 import projection
from verify_wc3_pathing_target_normalize218 import original_bytes
from retail_capture_source import matches_capture_source
ROOT=Path(__file__).resolve().parents[2]
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
SPEC=ROOT/'tools/ghidra/fixtures/retail-group354-1.27.json'
BUNDLE=ROOT/'tools/ghidra/fixtures/retail-group354-inputs-1.27.json.gz'
TESTS=['wc3_order_lifecycle.*','wc3_ability_dispatch.*','wc3_combat.*',
       'wc3_save.rejects_prior_save_versions','wc3_save.rejects_layout_mismatch_before_selecting_map',
       'wc3_save.current_format_uses_current_entity_layout','wc3_save.rejects_mismatched_entity_layout']

def digest(data):return hashlib.sha256(data).hexdigest()
def public(text):return re.findall(r'call Preload\( "(G354 [^"\r\n]*)" \)',text)

def runtime(cases,spec):
    if len(cases)!=3 or [c['rows'][0]['mode']for c in cases]!=['observe','observe','control']:
        raise ValueError('missing repeats/control')
    for i,case in enumerate(cases):
        rows=case['rows'];meta=rows[0];marks=public(case['preload'])
        if(meta.get('task')!='GROUP-03.2' or meta.get('sha256')!=SHA or not meta.get('owned') or
           meta.get('env')not in('B','C') or meta.get('source_sha256')!=spec['capture_sources'][i] or
           meta['source_sha256']['map']!=spec['map_sha256'] or
           any(r.get('type')=='error' or r.get('event')in('compile-error','trace-failed')for r in rows)):
            raise ValueError('capture provenance/failure')
        if marks!=spec['public'] or len(marks)!=37 or rows[-1]!={
            'event':'preload-file','sha256':digest(case['preload'].encode()),'markers':37,'complete':True}:
            raise ValueError('public stream differs or is incomplete')
        if meta['mode']=='control':
            if any('seq'in r or r.get('event')=='trace-end'for r in rows):raise ValueError('instrumented control')
            continue
        ends=[r for r in rows if r.get('event')=='trace-end']
        if(len(ends)!=1 or not ends[0].get('installed')or not ends[0].get('readOnly')or ends[0]['depth']or
           ends[0]['counts']!=dict(Counter(r['event']for r in rows if 'seq'in r))):
            raise ValueError('unfinished observer')
        if projection(rows)!=spec['events'] or [r['value']for r in rows if r.get('event')=='marker']!=marks:
            raise ValueError('native stream differs')
        expected=[[0x64000000,0x0be00000],[0x64400000,0x0be00000],
                  [0x64000000,0x0be00000],[0x64000000,0x0ba00000],[0x64000000,0x0be00000]]
        for scene,key in enumerate(expected):
            selected=[r for r in rows if r.get('scene')==scene]
            if next(r for r in selected if r['event']=='rank-end')['key']!=key:
                raise ValueError('priority decision differs')
        positive=[r for r in rows if r.get('event')=='predicate'and r.get('address')=='48e2c0'and r['result']]
        if len(positive)!=1 or positive[0]['scene']!=1 or positive[0]['tick']!=28:
            raise ValueError('override positive/inverse differs')
    return dict(captures=3,scenes=5,public_markers=37,native_events=len(spec['events']))

def validate(spec):
    if(spec.get('version')!=1 or spec.get('task')!='GROUP-03.2' or spec.get('game_sha256')!=SHA or
       spec.get('engine_tests')!=TESTS or len(spec.get('instructions',{}))!=818 or len(spec.get('events',[]))!=547):
        raise ValueError('incomplete contract/inventory')
    for p,h in spec['pins'].items():
        if not matches_capture_source(ROOT,p,h):raise ValueError('source changed '+p)
    if digest(BUNDLE.read_bytes())!=spec['bundle_sha256']:raise ValueError('bundle changed')
    cases=json.loads(gzip.decompress(BUNDLE.read_bytes()))
    runtime(cases,spec)
    if digest(base64.b64decode(cases[0]['map_input']))!=spec['base_sha256']:
        raise ValueError('map base differs')
    return cases

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
    if a.report.exists():p.error('fresh report required')
    spec=json.loads(SPEC.read_text());cases=validate(spec);original_bytes(a.binary,spec['instructions'])
    with tempfile.TemporaryDirectory(prefix='group354-rebuild-')as tmp:
        base=Path(tmp)/'base.w3m';base.write_bytes(base64.b64decode(cases[0]['map_input']))
        output=Path(tmp)/'rebuilt.w3m'
        subprocess.run([sys.executable,str(ROOT/'tools/frida/research/group341_make_map.py'),
                        '--base',str(base),'--output',str(output),
                        '--probe',str(ROOT/'tools/frida/research/group354_probe.j')],check=True,stdout=subprocess.DEVNULL)
        if digest(output.read_bytes())!=spec['map_sha256']:raise ValueError('map reconstruction differs')
    import verify_wc3_pathing_work242 as runner
    previous=runner.TESTS
    try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
    finally:runner.TESTS=previous
    report=dict(passed=True,status="verified",binary_sha256=SHA,instructions=len(spec["instructions"]),**runtime(cases,spec),engine=engine)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
