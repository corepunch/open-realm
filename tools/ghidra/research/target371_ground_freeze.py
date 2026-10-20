#!/usr/bin/env python3
"""Freeze complete repeated Ground-command lifetimes and unhooked controls."""
import argparse,gzip,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_target371_ground as v


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reports',type=Path,required=True);p.add_argument('--output',type=Path,default=v.SPEC);a=p.parse_args()
    if a.output.exists()or a.output.with_suffix('.json.gz').exists():p.error('fresh fixture paths required')
    groups={};scenes={}
    for variant in v.trace.VARIANTS:
        names=[variant+'-observe-1',variant+'-observe-2',variant+('-control-2'if variant=='ground-rooted'else'-control-1')]
        directory=a.reports
        if variant=='ground-ai':directory=a.reports/'future-ai';names=['observe2','observe3','control1']
        if variant=='ground-ai-queue':directory=a.reports/'future-ai';names=['queue-observe1','queue-observe2','queue-control1']
        if variant=='ground-ai-birth':directory=a.reports/'future-ai';names=['birth-proven-observe1','birth-proven-observe2','birth-proven-control1']
        cases=[dict(rows=[json.loads(line)for line in(directory/(name+'.jsonl')).read_text().splitlines()],
                    preload=(directory/(name+'-preload.txt')).read_bytes().decode())for name in names]
        groups[variant]=cases
        scenes[variant]=dict(projection=v.trace.project(cases[0]['rows'],variant),
            public_markers=v.public(cases[0]['preload']),sources=[case['rows'][0]['source_sha256']for case in cases],
            map_sha256=cases[0]['rows'][0]['source_sha256']['map'])
    sources=['tools/frida/research/'+name for name in ('target371_ground_capture.py','target371_ground_observer.js',
        'target371_ground_probe.j','target371_ground_boundary_capture.py','target371_ground_boundary_observer.js',
        'target371_ground_boundary_probe.j','target371_ground_rooted_capture.py','target371_ground_rooted_probe.j','target371_make_map.py')]
    sources+=['tools/frida/research/'+name for name in ('target371_ground_ai_probe.j','target371_ground_ai_make_map.py','target371_ground_ai_capture.py','target371_ground_ai_observer.js')]
    sources+=['tools/frida/research/'+name for name in ('target371_ground_queue_observer.js','target371_ground_queue_proven_observer.js','target371_ai_stop_probe.j','target371_ai_stop_make_map.py','target371_ai_stop_capture.py')]
    sources+=['tools/ghidra/research/'+name for name in ('Target371GroundEvidence.java','Target371GroundQueueEvidence.java','target371_ground_trace.py','target371_trace.py')]
    sources+=['tools/frida/make_wc3_pathfinding_map.py','tools/ghidra/wc3_pathing_engine_probe.c']
    evidence=json.loads((a.reports/'ground-static-final-ai.json').read_text())
    queue=json.loads((a.reports/'future-ai/queue-final-green-static-guards.json').read_text())
    assert not set(evidence['instructions'])&set(queue['instructions']),'overlapping instruction inventory'
    evidence['instructions'].update(queue['instructions'])
    spec=dict(version=1,task='TARGET-01.2',game_sha256=v.SHA,engine_tests=v.TESTS,instructions=evidence['instructions'],
        pins={name:v.digest((ROOT/name).read_bytes())for name in sources},scenes=scenes,
        scope='Public Ground selected-range and FLT_MAX admission, rejection of artillery minimum range before task creation, actual equality/last-inside/first-outside canonical predicates, cold physical ownership and whole heading/velocity lifetimes. Footman and Mortar cases retain complete independent repeats and unhooked controls. Two stock etrp sources also preserve the absence of physical Move requests, near head retention and synchronous far completion; this does not generalize to immobile artillery validation. Blocked human/computer Ground repeats preserve all1559 composed records, human head retention and synchronous transport-null/AI guard replacement. Supplemental blocked queue repeats retain all1815 records, and fully flushed public birth repeats retain all639 records and102 controlled markers each, including initial Stop before guard enrollment, immediate851974 before its internal scope, Guard point notification after scope release, and saved851974 continuation. Exact canonical source lifetimes bound pointer aliases; all caller scalars and complete returnProof code remain literal. Broader ground swing/projectile/polling/stat and dynamic producer contracts retain their existing owning tasks; this evidence does not assert retail save-file parity.')
    result=v.runtime(groups,spec)
    packed=gzip.compress((json.dumps(groups,separators=(',',':'))+'\n').encode(),mtime=0);spec['bundle_sha256']=v.digest(packed)
    a.output.with_suffix('.json.gz').write_bytes(packed);a.output.write_text(json.dumps(spec,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
