#!/usr/bin/env python3
"""Revalidate retail scheduling/order contracts and repeated actual engine journeys."""
import argparse
import json
from pathlib import Path
from research.e2e210_contract import ROOT,CATEGORIES,validate,scheduler
from verify_wc3_pathing_e2e_variants import verify as verify_journeys

FIXTURE=ROOT/'tools/ghidra/fixtures/retail-e2e-orders210-1.27.json'


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True);p.add_argument('--archive',type=Path,required=True)
    p.add_argument('--fixture',type=Path,default=FIXTURE)
    p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True)
    a=p.parse_args()
    if a.report.exists():p.error('report must be fresh')
    spec=json.loads(a.fixture.read_text())
    contention=scheduler(spec,a.archive)
    result=verify_journeys(a,validate,CATEGORIES,'wc3_e2e210')
    result['scheduler']=contention
    a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
