#!/usr/bin/env python3
"""Require completed public random witnesses and exact owner-word agreement with the original oracle."""
import argparse, hashlib, json
from pathlib import Path


def check(path, fixture):
    rows=[json.loads(line) for line in path.read_text().splitlines()]
    if rows[0].get('event')!='metadata' or rows[0].get('sha256')!=fixture['binary_sha256']:
        raise ValueError('random witness binary metadata differs')
    if not any(r.get('event')=='trace-end' and r.get('installed') for r in rows):
        raise ValueError('random witness did not complete')
    if any(r.get('event') in ('error','trace-failed') for r in rows):
        raise ValueError('random witness contains an observer failure')
    markers=[r['value'] for r in rows if r.get('event')=='marker']
    if not any('label=start_random_owner ' in v for v in markers) or not any('tick=300 label=complete ' in v for v in markers):
        raise ValueError('random scenario did not reach its scheduled completion')
    calls=[r for r in rows if r.get('event')=='random-native']
    expected=[]
    for sequence in fixture['sequences']:
        seed=sequence['seed'];prior=sequence['initial']
        expected.append(dict(case=f'seed_{seed}',native='SetRandomSeed',input=[seed],after=prior))
        for i,op in enumerate(sequence['operations'][:48]):
            native='GetRandomReal' if op['kind']==2 else 'GetRandomInt'
            output=op['output'][0] if op['kind'] in (1,2) else op['state'][0]>>31
            expected.append(dict(case=f'seed_{seed}_op_{i}',native=native,input=op['input'] if op['kind'] in (1,2) else [0,1],before=prior,after=op['state'],output=output))
            prior=op['state']
    if len(calls)!=len(expected):raise ValueError(f'public random call count differs: {len(calls)}/{len(expected)}')
    for actual,wanted in zip(calls,expected):
        if any(actual.get(key)!=value for key,value in wanted.items()):
            raise ValueError(f'public owner words differ: {actual} expected {wanted}')
    return expected


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,required=True);p.add_argument('--repeat',type=Path,required=True)
    p.add_argument('--fixture',type=Path,default=Path('tools/ghidra/fixtures/retail-pathfinding-random-1.27.json'))
    p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    fixture=json.loads(a.fixture.read_text());first=check(a.capture,fixture);second=check(a.repeat,fixture)
    if first!=second:raise ValueError('normalized owner draw sequences differ')
    report=dict(passed=True,binary_sha256=fixture['binary_sha256'],public_calls=len(first),seed_calls=11,draw_queries=528,
                capture_sha256=hashlib.sha256(a.capture.read_bytes()).hexdigest(),repeat_sha256=hashlib.sha256(a.repeat.read_bytes()).hexdigest(),
                scope='Complete live public SetRandomSeed returns and48 public query calls per seed. Owner words match isolated original/C, repeated exactly. Separate per-unit state and movement-wide draw order excluded.')
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
