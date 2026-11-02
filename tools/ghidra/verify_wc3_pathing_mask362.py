#!/usr/bin/env python3
"""Verify the complete amphibious pursuit and target producer/mask audit."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

import verify_wc3_pathing_target356 as reference
from verify_wc3_pathing_target_normalize218 import original_bytes
from retail_capture_source import matches_capture_source

ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-mask362-1.27.json'
TESTS=['wc3_move_profiles.*','wc3_movement.shared189*','wc3_movement.scheduler190*','wc3_movement.capscope273*','wc3_movement.crowd361*','wc3_movement.mask362*','wc3_movement.follow186*','wc3_movement.follow187*',
       'wc3_movement.target*','wc3_movement.attack306*','wc3_movement.structure279*',
       'wc3_movement.follow278*','wc3_movement.spell185*','wc3_movement.flight280*',
       'wc3_movement.delayed164*','wc3_movement.refresh274*',
       'wc3_pathfinding.authored_water_and_amphibious_masks_reach_move_queries']


def fixture_rows(header):
    match=re.search(r'mask362_amph\[\]\[23\]=\{(.*?)\n\};',header,re.S)
    if not match:raise ValueError('missing amphibious production fixture')
    rows=[[int(v) for v in row.split(',')] for row in re.findall(r'\{([0-9,]+)\}',match[1])]
    if len(rows)!=617 or any(len(row)!=23 for row in rows):
        raise ValueError('complete617-visit amphibious owner stream required')
    return rows


def validate(spec):
    if (spec.get('version')!=1 or spec.get('task')!='TARGET-02.1' or
            spec.get('game_sha256')!=reference.SHA or spec.get('engine_tests')!=TESTS):
        raise ValueError('wrong mask contract')
    if len(spec.get('instructions',{}))!=1434:
        raise ValueError('complete producer/query/gate instruction inventory required')
    profile=spec.get('authored_profile',{})
    if ({key:profile.get(key) for key in ('unit','movetp','collision','speed','turnRate','propWin')}
            != dict(unit='nmyr',movetp='amph',collision=48,speed=270,turnRate=.5,propWin=60)):
        raise ValueError('original complete Myrmidon movement profile required')
    for path,expected in spec['pins'].items():
        if not matches_capture_source(ROOT,path,expected):raise ValueError('source changed '+path)
    base=json.loads(reference.SPEC.read_text())
    if hashlib.sha256(reference.SPEC.read_bytes()).hexdigest()!=spec['target356_spec_sha256']:
        raise ValueError('original/fresh target inventory changed')
    bundle=reference.validate(base)
    witnesses=spec.get('geometry_witnesses',[])
    expected=[dict(counter=c,radius=0x3fc00000,reach=0x413d8000,persistent=p)
              for c,p in [(5108,0),(5357,1)]]
    if (len(witnesses)!=2 or any(w['rows']!=expected for w in witnesses) or
            [w['capture_sha256'] for w in witnesses]!=[c['sha256'] for c in bundle['captures'][:2]]):
        raise ValueError('original/fresh amphibious radius and reach witnesses required')
    rows=fixture_rows((ROOT/'games/warcraft-3/game/tests/fixtures/retail_mask362.h').read_text())
    if rows!=bundle['source_runs'][0]['scenes']['6']['rows']:
        raise ValueError('amphibious production rows differ from original words')
    return bundle


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--report',type=Path,required=True)
    ap.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    ap.add_argument('--data',type=Path,default=ROOT/'build/tests');args=ap.parse_args()
    if args.report.exists():ap.error('fresh report required')
    spec=json.loads(SPEC.read_text());validate(spec);original_bytes(args.binary,spec['instructions'])
    gate_report=args.report.with_name(args.report.stem+'-original-gate.json')
    if gate_report.exists():ap.error('fresh original gate report required')
    subprocess.run([sys.executable,str(ROOT/'tools/ghidra/verify_wc3_pathing_replan.py'),
                    '--binary',str(args.binary),'--report',str(gate_report)],
                   check=True,stdout=subprocess.DEVNULL)
    gate=json.loads(gate_report.read_text())
    if not gate.get('passed') or gate.get('cases')!=28880:
        raise ValueError('complete original destination gate oracle required')
    import verify_wc3_pathing_work242 as runner
    prior=runner.TESTS
    try:
        runner.TESTS=TESTS;engine=runner.run_engine(args.test_binary,args.data,args.report)
    finally:runner.TESTS=prior
    report=dict(passed=True,status='verified',binary_sha256=reference.SHA,instructions=len(spec['instructions']),
                captures=3,scenes=9,native_rows=5017,amphibious_engine_rows=617,
                gate_cases=gate['cases'],engine=engine)
    args.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':main()
