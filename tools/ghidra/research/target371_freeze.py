#!/usr/bin/env python3
"""Freeze complete repeated point-command lifetimes and unhooked controls."""
import argparse,gzip,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_target371 as v


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reports',type=Path,required=True);a=p.parse_args()
    if v.SPEC.exists()or v.SPEC.with_suffix('.json.gz').exists():p.error('fresh fixture paths required')
    groups={};scenes={}
    for variant in v.trace.GOALS:
        names=[variant+'-observe-1',variant+'-observe-2',variant+'-control-3']
        cases=[dict(rows=[json.loads(line)for line in(a.reports/(name+'.jsonl')).read_text().splitlines()],
                    preload=(a.reports/(name+'-preload.txt')).read_bytes().decode())for name in names]
        groups[variant]=cases
        scenes[variant]=dict(projection=v.trace.project(cases[0]['rows'],variant),
            public_markers=v.public(cases[0]['preload']),sources=[case['rows'][0]['source_sha256']for case in cases],
            map_sha256=cases[0]['rows'][0]['source_sha256']['map'])
    sources=['tools/frida/research/'+name for name in ('target371_capture.py','target371_terrain_capture.py',
        'target371_observer.js','target371_make_map.py','target371_probe.j','target371_terrain_probe.j')]
    sources+=['tools/ghidra/research/'+name for name in ('Target371Evidence.java','target371_trace.py','target371_sqrt_gap.py')]
    sources+=['tools/frida/make_wc3_pathfinding_map.py','tools/ghidra/wc3_pathing_engine_probe.c']
    evidence=json.loads((a.reports/'command-and-distance-audit.json').read_text())
    spec=dict(version=1,task='TARGET-01.2',game_sha256=v.SHA,engine_tests=v.TESTS,instructions=evidence['instructions'],
        pins={name:v.digest((ROOT/name).read_bytes())for name in sources},scenes=scenes,
        scope='Public point Attack/Move synchronous range-zero admission and raw-click preservation, empty/Farm/static blocked goals, actual quantized arrival boundary, natural force/cannot-path/stop and complete heading/velocity call lifetimes. Production arithmetic is checked against every observed call. AttackGround selected-range/FLT_MAX producer and complete target/ability producer inventory remain in the existing TARGET-01.2 leaf; this evidence does not assert full leaf closure or retail save parity.')
    result=v.runtime(groups,spec)
    packed=gzip.compress((json.dumps(groups,separators=(',',':'))+'\n').encode(),mtime=0);spec['bundle_sha256']=v.digest(packed)
    v.SPEC.with_suffix('.json.gz').write_bytes(packed);v.SPEC.write_text(json.dumps(spec,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
