#!/usr/bin/env python3
"""Verify complete Corporeal/Ethereal native streams and game-owned lifetimes."""
import argparse,base64,gzip,hashlib,json,re,subprocess,sys,tempfile
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes
from retail_capture_source import matches_capture_source
ROOT=Path(__file__).resolve().parents[2]
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
SPEC=ROOT/'tools/ghidra/fixtures/retail-ethereal357-1.27.json'
BUNDLE=ROOT/'tools/ghidra/fixtures/retail-ethereal357-inputs-1.27.json.gz'
TESTS=['wc3_unit.ethereal357*','wc3_repulsion_policy.*','wc3_save.rejects_prior_save_versions',
       'wc3_save.rejects_layout_mismatch_before_selecting_map',
       'wc3_save.current_format_uses_current_entity_layout','wc3_save.rejects_mismatched_entity_layout']
def digest(raw):return hashlib.sha256(raw).hexdigest()
def public(raw):return re.findall(r'call Preload\( "(M333 [^"\r\n]*)" \)',raw)
def projection(rows):
    # Original effect6d8100 owns presentation death timers; keep primary morph
    # timers, flags, Unit158/198, identities and clocks entirely unchanged.
    return [{k:v for k,v in row.items()if k!='seq'}for row in rows if 'seq'in row and row['event']!='module'
            and not(row['event']=='transition-timer'and row.get('caller')=='6d81b8')]
def stream(cases,spec,prefix,count,scenes,ethereal=True):
    if len(cases)!=3 or [c['rows'][0].get('mode')for c in cases]!=['observe','observe','control']:
        raise ValueError('missing independent repeats or control')
    for i,case in enumerate(cases):
        rows=case['rows'];meta=rows[0];marks=re.findall(r'call Preload\( "('+prefix+r' [^"\r\n]*)" \)',case['preload'])
        if(meta.get('sha256')!=SHA or not meta.get('owned')or meta.get('env')not in('B','C')or
           meta.get('source_sha256')!=spec['capture_sources'][i]or
           meta['source_sha256']['map']!=spec['map_sha256']or
           any(r.get('type')=='error'or r.get('event')in('compile-error','trace-failed')for r in rows)):
            raise ValueError('capture provenance or execution failed')
        if(marks!=spec['public']or len(marks)!=count or rows[-1]!={
            'event':'preload-file','sha256':digest(case['preload'].encode()),'markers':count,'complete':True}):
            raise ValueError('incomplete or changed public behavior')
        if i==2:
            if any('seq'in r or r.get('event')=='trace-end'for r in rows):raise ValueError('instrumented control')
            continue
        ends=[r for r in rows if r.get('event')=='trace-end']
        if len(ends)!=1 or ends[0].get('counts')!=dict(Counter(r['event']for r in rows if 'seq'in r)):
            raise ValueError('unfinished observer')
        if projection(rows)!=spec['events']or [r['value']for r in rows if r.get('event')=='marker']!=marks:
            raise ValueError('complete native stream differs')
        changes=[r for r in rows if r.get('event')in('counter158-acquire-leave','counter158-release-leave')]
        if ethereal and (len(changes)!=18 or any(r['after']['counter158']!=int(r['event']=='counter158-acquire-leave')for r in changes)):
            raise ValueError('ethereal attachment and inverse changed')
    return dict(captures=3,scenes=scenes,public_markers=count,native_events=len(spec['events']))
def runtime(cases,spec):return stream(cases,spec,'M333',500,6)
def lifetime_runtime(cases,spec):return stream(cases,spec['lifetime'],'L357',164,2,False)
def validate(spec):
    if(spec.get('version')!=2 or spec.get('task')!='SEP-01.2'or spec.get('game_sha256')!=SHA or
       len(spec.get('instructions',{}))!=164 or len(spec.get('events',[]))!=894 or spec.get('engine_tests')!=TESTS):
        raise ValueError('incomplete contract inventory')
    for path,sha in spec['pins'].items():
        if not matches_capture_source(ROOT,path,sha):raise ValueError('changed source '+path)
    if digest(BUNDLE.read_bytes())!=spec['bundle_sha256']:raise ValueError('changed frozen inputs')
    bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));runtime(bundle['cases'],spec)
    lifetime_runtime(bundle['lifetime_cases'],spec)
    if len(spec['lifetime']['events'])!=274:raise ValueError('missing positive lifetime stream')
    if digest(base64.b64decode(bundle['map_input']))!=spec['base_sha256']:raise ValueError('changed map input')
    return bundle

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
    if a.report.exists():p.error('fresh report required')
    spec=json.loads(SPEC.read_text());bundle=validate(spec);original_bytes(a.binary,spec['instructions'])
    with tempfile.TemporaryDirectory(prefix='ethereal357-')as tmp:
        base=Path(tmp)/'base.w3m';base.write_bytes(base64.b64decode(bundle['map_input']));output=Path(tmp)/'map.w3m'
        subprocess.run([sys.executable,str(ROOT/'tools/frida/research/morph330_make_map.py'),
            '--base',str(base),'--output',str(output),'--mpqtool',str(ROOT/'build/bin/mpqtool'),
            '--probe',str(ROOT/'tools/frida/research/morph333_probe.j'),'--empty-ability-units','ospm','ospw'],
            check=True,stdout=subprocess.DEVNULL)
        if digest(output.read_bytes())!=spec['map_sha256']:raise ValueError('map reconstruction differs')
        output=Path(tmp)/'lifetime.w3m'
        subprocess.run([sys.executable,str(ROOT/'tools/frida/research/ethereal357_lifetime_make_map.py'),
            '--base',str(base),'--output',str(output),'--mpqtool',str(ROOT/'build/bin/mpqtool'),
            '--probe',str(ROOT/'tools/frida/research/ethereal357_lifetime_probe.j'),'--empty-ability-units','ospm','ospw'],
            check=True,stdout=subprocess.DEVNULL)
        if digest(output.read_bytes())!=spec['lifetime']['map_sha256']:raise ValueError('lifetime map reconstruction differs')
    import verify_wc3_pathing_work242 as runner
    previous=runner.TESTS
    try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
    finally:runner.TESTS=previous
    report=dict(passed=True,status='verified',binary_sha256=SHA,instructions=len(spec['instructions']),
                **runtime(bundle['cases'],spec),lifetime=lifetime_runtime(bundle['lifetime_cases'],spec),engine=engine)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
