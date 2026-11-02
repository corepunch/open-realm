#!/usr/bin/env python3
"""Verify Phoenix fatal admission and saved egg/rebirth ownership."""
import argparse,base64,gzip,json,subprocess,tempfile
from pathlib import Path
from verify_wc3_pathing_ethereal357 import ROOT,SHA,digest,stream
from verify_wc3_pathing_target_normalize218 import original_bytes
from retail_capture_source import matches_capture_source
SPEC=ROOT/'tools/ghidra/fixtures/retail-phoenix365-1.27.json'
BUNDLE=ROOT/'tools/ghidra/fixtures/retail-phoenix365-inputs-1.27.json.gz'
TESTS=['wc3_unit.phoenix365*','wc3_unit.alternate364*','wc3_unit.ethereal357*','wc3_unit.submerge360*','wc3_repulsion_policy.*','wc3_save.rejects_prior_save_versions',
       'wc3_save.rejects_layout_mismatch_before_selecting_map',
       'wc3_save.current_format_uses_current_entity_layout','wc3_save.rejects_mismatched_entity_layout']
def runtime(bundle,spec):
    stock=stream(bundle['stock'],spec['stock'],'P365',602,3,False)
    life=stream(bundle['life'],spec['life'],'P365',602,3,False)
    return dict(captures=6,scenes=6,public_markers=stock['public_markers']+life['public_markers'],
                native_events=stock['native_events']+life['native_events'])
def validate(spec):
    if(spec.get('version')!=1 or spec.get('task')!='SEP-01.2'or spec.get('game_sha256')!=SHA or
       len(spec.get('instructions',{}))!=1021 or len(spec['stock']['events'])!=809 or
       len(spec['life']['events'])!=809 or spec.get('engine_tests')!=TESTS):
        raise ValueError('incomplete Phoenix contract inventory')
    for path,sha in spec['pins'].items():
        if not matches_capture_source(ROOT,path,sha):raise ValueError('changed source '+path)
    if digest(BUNDLE.read_bytes())!=spec['bundle_sha256']:raise ValueError('changed frozen inputs')
    bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));runtime(bundle,spec)
    if digest(base64.b64decode(bundle['map_input']))!=spec['base_sha256']:raise ValueError('changed map input')
    return bundle

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
    if a.report.exists():p.error('fresh report required')
    spec=json.loads(SPEC.read_text());bundle=validate(spec);original_bytes(a.binary,spec['instructions'])
    with tempfile.TemporaryDirectory(prefix='phoenix365-')as tmp:
        base=Path(tmp)/'base.w3m';base.write_bytes(base64.b64decode(bundle['map_input']))
        for name,probe in [('stock','phoenix365_probe.j'),('life','phoenix365_life_probe.j')]:
            output=Path(tmp)/(name+'.w3m')
            subprocess.run([__import__('sys').executable,str(ROOT/'tools/frida/research/phoenix365_make_map.py'),
                '--base',str(base),'--output',str(output),'--mpqtool',str(ROOT/'build/bin/mpqtool'),
                '--probe',str(ROOT/'tools/frida/research'/probe),'--empty-ability-units','hphx','hpxe'],
                check=True,stdout=subprocess.DEVNULL)
            if digest(output.read_bytes())!=spec[name]['map_sha256']:raise ValueError('map reconstruction differs')
    import verify_wc3_pathing_work242 as runner
    previous=runner.TESTS
    try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
    finally:runner.TESTS=previous
    report=dict(passed=True,status='verified',binary_sha256=SHA,instructions=len(spec['instructions']),
                **runtime(bundle,spec),engine=engine)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
