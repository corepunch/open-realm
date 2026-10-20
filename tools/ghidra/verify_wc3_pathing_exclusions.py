#!/usr/bin/env python3
"""Reproduce MAP-04 request scopes and recheck their complete retail captures.

Engine regressions consume all45 original counter/window and hierarchy stages,
including aliases, null roles and already-held exclusions. Public scope lifetime
and command composition are separate from the supplied spatial oracle inputs.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

HERE=Path(__file__).resolve().parent
FIXTURES=HERE/'fixtures'
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
CONSUMER_SHA='f3393e78cf64f85fcc8551d5cf9e3b604f9d78f42fb095ac2151b13fcfaa800e'
FROZEN={
    'MAP-04.1-expected.json':'4a95b59fdd95872f464a33fbd21eca0f6eed60ee78f286b844aa83049b6475b0',
    'MAP-04.2-expected-oracle.json':'d8c0c978c29d0bd893fe2d8882e45f6e7adfb29c8d93fa992b9c9ca356fc1438',
    'MAP-04.2-expected.json':'94e1fd49e08eb9249d3ccc8f50ea57bd844d2f6fc0aa4c56c63f9f9c16fa3c18',
}


def run(script,*arguments):
    subprocess.run([sys.executable,str(script),*map(str,arguments)],check=True,stdout=subprocess.DEVNULL)


def restore_inputs(root):
    bundle=json.loads(gzip.decompress((FIXTURES/'retail-exclusion-inputs-1.27.json.gz').read_bytes()))
    if set(bundle['files'])!=set(bundle['sha256']):raise ValueError('capture hash inventory differs')
    for name,value in bundle['files'].items():
        path=Path(name)
        if path.is_absolute() or '..' in path.parts:raise ValueError('unsafe input filename')
        raw=value.encode()
        if hashlib.sha256(raw).hexdigest()!=bundle['sha256'][name]:raise ValueError('input changed: '+name)
        output=root/path;output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(raw)
    return bundle


def verify(binary):
    for name,digest in FROZEN.items():
        if hashlib.sha256((FIXTURES/'research'/name).read_bytes()).hexdigest()!=digest:
            raise ValueError('frozen payload differs: '+name)
    if hashlib.sha256(binary.read_bytes()).hexdigest()!=SHA:raise ValueError('retail binary differs')
    mapping=(HERE/'MapPathfinding.java').read_text()
    if hashlib.sha256((FIXTURES/'retail-exclusion-consumers-1.27.json').read_bytes()).hexdigest()!=CONSUMER_SHA:
        raise ValueError('fine consumer fixture differs')
    for name in ('retail-exclusion-scope-ghidra-1.27.json','retail-exclusion-consumer-ghidra-1.27.json'):
        readback=json.loads((FIXTURES/name).read_text())
        if readback['unsaved_changes'] or len(readback['functions'])!=3:
            raise ValueError('incomplete saved Ghidra evidence')
        for row in readback['functions']:
            if row['comment'] not in mapping:
                raise ValueError('saved Ghidra note missing from mapper: '+row['address'])
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory);bundle=restore_inputs(root)
        oracle=root/'oracle.json'
        stages=root/'engine-stages.json'
        consumers=root/'engine-consumers.json'
        run(HERE/'research/verify_map04_1_nested_exclusions.py','--binary',binary,'--report',oracle,
            '--reference',FIXTURES/'research/MAP-04.1-expected.json',
            '--edit-reference',FIXTURES/'research/MAP-04.2-expected-oracle.json','--engine-fixture',stages,
            '--consumer-fixture',consumers)
        result=json.loads(oracle.read_text())
        if stages.read_bytes()!=gzip.decompress((FIXTURES/'retail-exclusion-stages-1.27.json.gz').read_bytes()):
            raise ValueError('complete native stage export differs')
        header=root/'retail_exclusion_stages.h'
        run(HERE/'research/export_exclusion_stages.py','--stages',stages,
            '--frozen',FIXTURES/'research/MAP-04.1-expected.json','--header',header)
        if header.read_bytes()!=(HERE.parents[1]/'games/warcraft-3/game/tests/retail_exclusion_stages.h').read_bytes():
            raise ValueError('engine stage fixtures differ')
        result['engine_stage_cases']=len(json.loads(stages.read_text())['cases'])
        if consumers.read_bytes()!=(FIXTURES/'retail-exclusion-consumers-1.27.json').read_bytes():
            raise ValueError('complete native consumer export differs')
        header=root/'retail_exclusion_consumers.h'
        run(HERE/'research/export_exclusion_consumers.py','--fixture',consumers,'--header',header)
        if header.read_bytes()!=(HERE.parents[1]/'games/warcraft-3/game/tests/retail_exclusion_consumers.h').read_bytes():
            raise ValueError('engine consumer fixtures differ')
        result['consumer_scope_cases']=len(json.loads(consumers.read_text())['cases'])
        # Decode every scope again. Comparing the complete fresh report also
        # checks instruction/function coverage, resolved calls and unwind maps.
        static=root/'fresh-static.json'
        run(HERE/'research/verify_map04_2_scope_exits.py','--binary',binary,'--report',static,
            '--functions',root/'functions.json','--cache',root/'objdump.asm')
        if json.loads(static.read_text())!=json.loads((root/'scope-exits-MAP-04.2.json').read_text()):
            raise ValueError('fresh complete scope inventory differs')
        analyses=[];markers=[]
        for name in ('target-overlap-observe-2','target-edit-observe-1','target-edit-observe-2'):
            capture=root/'captures'/name;analysis=root/(name+'-analysis.json')
            arguments=['--capture',capture,'--report',analysis]
            if name=='target-overlap-observe-2':
                arguments+=['--reference',root/'target-overlap81-repeat.jsonl']
            run(HERE.parent/'frida/research/verify_map04_2_scope_capture.py',*arguments)
            report=json.loads(analysis.read_text());summary=report['summary']
            if not summary['complete'] or summary['lifo_violations'] or summary['foreign_writers_inside_request_scopes']:
                raise ValueError('incomplete or unbalanced live scope: '+name)
            if summary['fine_requests']!=summary['fine_restored'] or summary['coarse_requests']!=summary['coarse_order_ok']:
                raise ValueError('live restoration differs: '+name)
            rows=[json.loads(line) for line in (capture/'capture.jsonl').read_text().splitlines() if line.strip()]
            markers.append([r['value'] for r in rows if r.get('event')=='marker'])
            analyses.append(analysis)
        if markers[1]!=markers[2]:raise ValueError('complete edit repeat markers differ')
        # Keep the failed observer attempt explicitly rejected, never a witness.
        failed=root/'captures/target-overlap-observe-1';failure=root/'failed-analysis.json'
        run(HERE.parent/'frida/research/verify_map04_2_scope_capture.py','--capture',failed,'--report',failure)
        if json.loads(failure.read_text())['summary']['complete']:raise ValueError('failed capture accepted')
        rebuilt=root/'combined.json'
        run(HERE/'research/freeze_map04_2_expected.py','--oracle',FIXTURES/'research/MAP-04.2-expected-oracle.json',
            '--static',root/'scope-exits-MAP-04.2.json','--live',*analyses,'--output',rebuilt)
        if rebuilt.read_bytes()!=(FIXTURES/'research/MAP-04.2-expected.json').read_bytes():
            raise ValueError('complete combined frozen evidence differs')
        result.update(static_scopes=12,live_captures=3,failed_captures=1,live_fine_requests=9,
            live_coarse_requests=27,repeat_markers=len(markers[1]),input_files=len(bundle['files']),
            request_exits=8,combined_sha256=FROZEN['MAP-04.2-expected.json'])
        return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    arguments=parser.parse_args()
    result=verify(arguments.binary.resolve())
    arguments.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
