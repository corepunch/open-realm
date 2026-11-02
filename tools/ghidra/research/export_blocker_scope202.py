#!/usr/bin/env python3
"""Copy observed original166140 collection/resolution boundaries into C."""
import argparse
import itertools
import json
from pathlib import Path


def header(fixture):
    kinds={'clear':0,'terrain':1,'peer':2,'self':3}
    rows=fixture['cases']
    if {(r['kind'],r['outer'],r['profile']) for r in rows}!=set(itertools.product(kinds,(0,1,7),range(4))) or len(rows)!=48:
        raise ValueError('incomplete original scope matrix')
    result=[]
    for row in rows:
        if [s[0] for s in row['stages']]!=[0x166265,0x168360,0x1662dd,0x1662f1]:
            raise ValueError('unexpected instruction boundaries')
        values=[kinds[row['kind']],row['outer'],row['profile'],row['result'],row['count'],
                row['self_delay'],row['peer_delay'],int(row['self_blocker']),int(row['peer_blocker'])]
        result.append('    {'+','.join(map(str,values))+',{'+','.join(str(s[1]) for s in row['stages'])+'}}')
    return ('/* Original166140 ->168360, complete calls; supplied geometry and outer counters.\n'
            ' * Exported by export_blocker_scope202.py, no expected-policy model. */\n'
            'typedef struct { unsigned kind,outer,profile,clear,count,self_delay,peer_delay,self_blocker,peer_blocker,stages[4]; } retailBlockerScope202_t;\n'
            'static retailBlockerScope202_t const retail_blocker_scope202[48]={\n'+',\n'.join(result)+'\n};\n')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--header',type=Path,required=True)
    args=parser.parse_args()
    args.header.write_text(header(json.loads(args.fixture.read_text())))


if __name__=='__main__':main()
