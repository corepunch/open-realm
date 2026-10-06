#!/usr/bin/env python3
"""Replay the completed adaptive handoffs with frozen, portable Ghidra inputs.

Only the harness's disassembly-input directory is relocated. Every original
producer, request and predicate executes unchanged. Research sources and their
frozen expectations stay separate from this corpus adapter.
"""
import argparse
import collections
import gzip
import hashlib
import importlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
FIXTURES=ROOT/'tools/ghidra/fixtures/research'


def replay(binary,report):
    inputs=report.parent/(report.stem+'-inputs');inputs.mkdir()
    frozen=json.loads(gzip.decompress((FIXTURES/'ACC-01.1-inputs.json.gz').read_bytes()))
    for name,content in frozen['files'].items():
        path=Path(name)
        if path.is_absolute() or '..' in path.parts:raise ValueError('unsafe evidence path')
        output=inputs/path;output.parent.mkdir(parents=True,exist_ok=True);output.write_text(content)
    sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
    import acc_research_harness
    acc_research_harness.GHIDRA_CACHE=inputs/'ghidra'
    commands=[
        ('branches','verify_acc01_1_branches','ACC-01.1-expected.json',
            ['--existing-coverage',str(inputs/'coverage')]),
        ('witnesses','verify_acc01_2_witnesses','ACC-01.2-expected.json.gz',
            ['--witness-maps',str(FIXTURES/'ACC-01.2-witness-maps.json'),
             '--inventory',str(FIXTURES/'ACC-01.1-expected.json')]),
        ('markers','verify_acc02_2_markers','ACC-02.2-expected.json',[]),
        ('costs','verify_acc04_1_costs','ACC-04.1-expected.json.gz',[])]
    observed={};argv=sys.argv
    try:
        for name,module,fixture,extra in commands:
            expected=(FIXTURES/fixture).read_bytes()
            if fixture.endswith('.gz'):expected=gzip.decompress(expected)
            expected_path=inputs/(name+'-expected.json');expected_path.write_bytes(expected)
            output=report.parent/(report.stem+'-'+name+'.json')
            sys.argv=[module,'--binary',str(binary),'--out',str(output),
                '--fixture',str(expected_path),*extra]
            importlib.import_module(module).main()
            data=json.loads(output.read_text())
            if data!=json.loads(expected):raise ValueError('full frozen expectation differs: '+name)
            observed[name]=data
    finally:sys.argv=argv
    branch=observed['branches'];witness=observed['witnesses'];marker=observed['markers'];cost=observed['costs']
    reach=collections.Counter(row['reach'] for row in branch['outcomes'])
    result=dict(binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),passed=True,
        original_results_sha256=hashlib.sha256(json.dumps(observed,sort_keys=True,separators=(',',':')).encode()).hexdigest(),
        branch_outcomes=branch['outcome_count'],reachable=sum(v for k,v in reach.items() if k.startswith('R')),
        rejected=sum(v for k,v in reach.items() if k.startswith('U')),caller_domain=reach['C-out'],
        predicate_cases=branch['predicate_cases'],requests=witness['requests'],
        witnessed_outcomes=witness['jcc_outcomes_observed'],transitions=witness['transitions_observed'],
        child_states=marker['A_child_states']['reachable_child_states'],reducer_cases=marker['B_reducer_paths']['cases'],
        object_controls=marker['F_object_control']['cases'],sqrt_samples=cost['part1_isqrt']['samples'],
        cost_requests=cost['part2_costs']['requests'],reached=cost['part2_costs']['reached'],
        budget_cases=cost['part4_budget']['cases'])
    report.write_text(json.dumps(result,indent=2)+'\n')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();args.report.parent.mkdir(parents=True,exist_ok=True)
    replay(args.binary,args.report)


if __name__=='__main__':main()
