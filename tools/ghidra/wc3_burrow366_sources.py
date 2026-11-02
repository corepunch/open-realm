#!/usr/bin/env python3
"""Verify native multi-alias Burrow ownership and retained base-form invisibility."""
import argparse,base64,copy,gzip,json,re,subprocess,tempfile
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_ethereal357 import ROOT,SHA,digest
from wc3_burrow366 import projection
from verify_wc3_pathing_target_normalize218 import original_bytes
from retail_capture_source import matches_capture_source
SPEC=ROOT/'tools/ghidra/fixtures/retail-burrow366-sources-1.27.json'
BUNDLE=ROOT/'tools/ghidra/fixtures/retail-burrow366-sources-inputs-1.27.json.gz'
TESTS=['wc3_unit.burrow366*','wc3_unit.morph331*','wc3_unit.alternate364*','wc3_unit.submerge360*','wc3_repulsion_policy.*','wc3_save.rejects_prior_save_versions',
       'wc3_save.rejects_layout_mismatch_before_selecting_map','wc3_save.current_format_uses_current_entity_layout','wc3_save.rejects_mismatched_entity_layout']
def runtime(bundle,spec):
    cases=bundle['cases']
    if len(cases)!=3 or [c['rows'][0].get('mode')for c in cases]!=['observe','observe','control']:raise ValueError('missing repeats/control')
    for i,case in enumerate(cases):
        rows=case['rows'];meta=rows[0];marks=re.findall(r'call Preload\( "(S366 [^"\r\n]*)" \)',case['preload'])
        if(meta.get('sha256')!=SHA or not meta.get('owned')or meta.get('env')not in('B','C')or
           meta.get('source_sha256')!=spec['capture_sources'][i]or meta['source_sha256']['map']!=spec['map_sha256']or
           any(r.get('type')=='error'or r.get('event')in('compile-error','trace-failed')for r in rows)):
            raise ValueError('capture provenance/execution differs')
        if(marks!=spec['public']or len(marks)!=105 or rows[-1]!={'event':'preload-file','sha256':digest(case['preload'].encode()),'markers':105,'complete':True}):
            raise ValueError('incomplete/changed public behavior')
        if i==2:
            if any('seq'in r or r.get('event')=='trace-end'for r in rows):raise ValueError('instrumented control')
            continue
        ends=[r for r in rows if r.get('event')=='trace-end']
        if len(ends)!=1 or ends[0].get('counts')!=dict(Counter(r['event']for r in rows if 'seq'in r)):raise ValueError('unfinished observer')
        if projection(rows)!=spec['events']or [r['value']for r in rows if r.get('event')=='marker']!=marks:raise ValueError('complete native stream differs')
    return dict(captures=3,scenes=2,public_markers=105,native_events=251)

def validate(spec):
    if(spec.get('version')!=1 or spec.get('task')!='SEP-01.2'or spec.get('game_sha256')!=SHA or
       len(spec.get('instructions',{}))!=1330 or len(spec.get('events',[]))!=251 or spec.get('engine_tests')!=TESTS):raise ValueError('incomplete Burrow contract')
    for path,sha in spec['pins'].items():
        if not matches_capture_source(ROOT,path,sha):raise ValueError('changed source '+path)
    if digest(BUNDLE.read_bytes())!=spec['bundle_sha256']:raise ValueError('changed frozen input')
    bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));runtime(bundle,spec)
    if digest(base64.b64decode(bundle['map_input']))!=spec['base_sha256']:raise ValueError('changed map input')
    return bundle

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
    if a.report.exists():p.error('fresh report required')
    spec=json.loads(SPEC.read_text());bundle=validate(spec);original_bytes(a.binary,spec['instructions'])
    with tempfile.TemporaryDirectory(prefix='burrow366-')as tmp:
        base=Path(tmp)/'base.w3m';base.write_bytes(base64.b64decode(bundle['map_input']));output=Path(tmp)/'burrow.w3m'
        subprocess.run([__import__('sys').executable,str(ROOT/'tools/frida/research/burrow366_sources_make_map.py'),'--base',str(base),'--output',str(output),'--mpqtool',str(ROOT/'build/bin/mpqtool'),'--empty-ability-units','ucry','ucrm'],check=True,stdout=subprocess.DEVNULL)
        if digest(output.read_bytes())!=spec['map_sha256']:raise ValueError('map reconstruction differs')
    import verify_wc3_pathing_work242 as runner
    previous=runner.TESTS
    try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
    finally:runner.TESTS=previous
    report=dict(passed=True,status='verified',binary_sha256=SHA,instructions=len(spec['instructions']),**runtime(bundle,spec),engine=engine)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
