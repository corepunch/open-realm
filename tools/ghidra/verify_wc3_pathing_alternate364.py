#!/usr/bin/env python3
"""Verify immediate publication when a morph attaches to its alternate endpoint."""
import argparse,base64,gzip,json,subprocess,sys,tempfile
from pathlib import Path
from verify_wc3_pathing_ethereal357 import ROOT,SHA,digest,stream
from verify_wc3_pathing_target_normalize218 import original_bytes
from retail_capture_source import matches_capture_source
SPEC=ROOT/'tools/ghidra/fixtures/retail-alternate364-1.27.json'
BUNDLE=ROOT/'tools/ghidra/fixtures/retail-alternate364-inputs-1.27.json.gz'
TESTS=['wc3_unit.alternate364*','wc3_unit.ethereal357*','wc3_unit.submerge360*','wc3_repulsion_policy.*',
       'wc3_save.rejects_prior_save_versions','wc3_save.rejects_layout_mismatch_before_selecting_map',
       'wc3_save.current_format_uses_current_entity_layout','wc3_save.rejects_mismatched_entity_layout']
def runtime(bundle,spec):
    first=stream(bundle['cases'],spec,'A364',422,5,False)
    life=stream(bundle['lifetime_cases'],spec['lifetime'],'L364',417,5,False)
    return dict(captures=6,scenes=10,public_markers=839,native_events=first['native_events']+life['native_events'])
def validate(spec):
    if(spec.get('version')!=2 or spec.get('task')!='SEP-01.2'or spec.get('game_sha256')!=SHA or
       len(spec.get('instructions',{}))!=614 or len(spec.get('events',[]))!=854 or len(spec.get('lifetime',{}).get('events',[]))!=849 or spec.get('engine_tests')!=TESTS):
        raise ValueError('incomplete alternate attachment contract')
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
    with tempfile.TemporaryDirectory(prefix='alternate364-')as tmp:
        base=Path(tmp)/'base.w3m';base.write_bytes(base64.b64decode(bundle['map_input']));output=Path(tmp)/'map.w3m'
        subprocess.run([sys.executable,str(ROOT/'tools/frida/research/alternate364_make_map.py'),
            '--base',str(base),'--output',str(output),'--mpqtool',str(ROOT/'build/bin/mpqtool'),
            '--empty-ability-units','ospm','ospw','nmyr','nmys','nnrg','nnrs','nsnp','nsbs'],
            check=True,stdout=subprocess.DEVNULL)
        if digest(output.read_bytes())!=spec['map_sha256']:raise ValueError('map reconstruction differs')
        output=Path(tmp)/'lifetime.w3m'
        subprocess.run([sys.executable,str(ROOT/'tools/frida/research/alternate364_lifetime_make_map.py'),
            '--base',str(base),'--output',str(output),'--mpqtool',str(ROOT/'build/bin/mpqtool'),
            '--empty-ability-units','ospm','ospw','nmyr','nmys','nnrg','nnrs','nsnp','nsbs'],
            check=True,stdout=subprocess.DEVNULL)
        if digest(output.read_bytes())!=spec['lifetime']['map_sha256']:raise ValueError('lifetime map reconstruction differs')
    import verify_wc3_pathing_work242 as runner
    previous=runner.TESTS
    try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
    finally:runner.TESTS=previous
    report=dict(passed=True,status='verified',binary_sha256=SHA,instructions=len(spec['instructions']),
                **runtime(bundle,spec),engine=engine)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
