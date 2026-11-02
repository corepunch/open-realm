#!/usr/bin/env python3
"""Verify all frozen Burrow366 maps and run the shared engine regressions once."""
import argparse,base64,json,subprocess,tempfile
from pathlib import Path
import wc3_burrow366 as base
import wc3_burrow366_sources as sources
import wc3_burrow366_passives as passives
import wc3_burrow366_phases as phases
from verify_wc3_pathing_target_normalize218 import original_bytes
GROUPS=[(base,'burrow366_make_map.py'),(sources,'burrow366_sources_make_map.py'),(passives,'burrow366_passives_make_map.py'),(phases,'burrow366_phases_make_map.py')]
def native(binary):
    guards={};counts=dict(captures=0,scenes=0,public_markers=0,native_events=0);details=[]
    for owner,builder in GROUPS:
        spec=json.loads(owner.SPEC.read_text());bundle=owner.validate(spec);guards.update(spec['instructions'])
        result=owner.runtime(bundle,spec);details.append(dict(fixture=owner.SPEC.name,**result))
        for key in counts:counts[key]+=result[key]
        with tempfile.TemporaryDirectory(prefix='burrow366-all-')as tmp:
            path=Path(tmp);original=path/'base.w3m';original.write_bytes(base64.b64decode(bundle['map_input']));output=path/'probe.w3m'
            subprocess.run([__import__('sys').executable,str(base.ROOT/'tools/frida/research'/builder),'--base',str(original),'--output',str(output),'--mpqtool',str(base.ROOT/'build/bin/mpqtool'),'--empty-ability-units','ucry','ucrm'],check=True,stdout=subprocess.DEVNULL)
            if base.digest(output.read_bytes())!=spec['map_sha256']:raise ValueError('map reconstruction differs: '+builder)
    original_bytes(binary,guards)
    return dict(binary_sha256=base.SHA,instructions=len(guards),maps=len(GROUPS),**counts,evidence=details)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--test-binary',type=Path,default=base.ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=base.ROOT/'build/tests');a=p.parse_args()
    if a.report.exists():p.error('fresh report required')
    result=native(a.binary)
    import verify_wc3_pathing_work242 as runner
    previous=runner.TESTS
    try:runner.TESTS=base.TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
    finally:runner.TESTS=previous
    report=dict(passed=True,status='verified',**result,engine=engine);a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
