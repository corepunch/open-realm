#!/usr/bin/env python3
"""Check completed larger Captain rosters, twelve-row batches and public controls."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from research.verify_captain_speed160_live import DLL, normalize as speeds


def normalize(rows):
    units, shared, requests = {}, {}, {}
    def identity(table, key):
        key = tuple(key) if isinstance(key, list) else key
        if key not in table:
            table[key] = len(table)
        return table[key]
    result = []
    for row in rows:
        if row.get('event') == 'roster':
            result.append(dict(event='roster', name=row['name'],
                               unit=identity(units,row['unit']), counter=row['counter'],
                               counts=row['captain']['counts']))
        elif row.get('event') == 'prepared-member':
            count,index=row['count'],row['index']
            if not 0 <= count < 12 or row['afterCount'] != (count+1)%12 or row['afterIndex'] != index+(count+1)//12:
                raise ValueError('twelve-row preparation transition differs')
            if row['policy'] != 1 or row['bindShared'] != 1 or row['target'] != '0x0':
                raise ValueError('uncovered preparation policy')
            result.append(dict(event='prepared',unit=identity(units,row['unit']),
                               shared=identity(shared,row['shared']),request=identity(requests,row['identity']),
                               counter=row['counter'],point=row['point'],index=index,count=count,
                               afterIndex=row['afterIndex'],afterCount=row['afterCount']))
        elif row.get('event') == 'captain-call' and row['name'] == 'shared-point-publication':
            result.append(dict(event='publication',counter=row['counter'],
                               before=row['before']['counts'],after=row['after']['counts']))
    return dict(events=result,speeds=speeds(rows))


def check_rows(rows,mode,scene):
    meta=rows[0] if rows else {}
    if (meta.get('event')!='metadata' or meta.get('task')!='payoff161' or
        meta.get('mode')!=mode or meta.get('owned') is not True or meta.get('sha256')!=DLL or
        meta.get('source_sha256')!=scene['sources']):
        raise ValueError('capture provenance differs')
    if any(r.get('event') in ('trace-failed','error') or r.get('type')=='error' for r in rows):
        raise ValueError('failed capture')
    public=[r for r in rows if r.get('event')=='preload-file']
    if len(public)!=1 or public[0].get('complete') is not True or public[0].get('markers')!=len(scene['markers']):
        raise ValueError('incomplete public scene')
    if mode=='observe':
        ends=[r for r in rows if r.get('event')=='trace-end']
        if len(ends)!=1 or ends[0].get('installed') is not True:
            raise ValueError('observer incomplete')
        if [r['value'] for r in rows if r.get('event')=='marker']!=scene['markers']:
            raise ValueError('public timeline differs')
        if normalize(rows)!=scene['normalized']:
            raise ValueError('roster or preparation timeline differs')
    elif any(r.get('event') in ('trace-end','module','marker','roster','prepared-member','captain-call','captain-speed') for r in rows):
        raise ValueError('instrumented control')


def verify(root,name,mode,scene):
    path=root/name
    raw=path.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=scene['captures'][name]:
        raise ValueError('capture pin differs')
    rows=[json.loads(s) for s in raw.splitlines()]
    check_rows(rows,mode,scene)
    preload=path.with_name(path.stem+'-preload.txt').read_bytes()
    footer=next(r for r in rows if r.get('event')=='preload-file')
    if hashlib.sha256(preload).hexdigest()!=footer['sha256']:
        raise ValueError('preload pin differs')
    if re.findall(r'call Preload\( "(RSH [^"\r\n]*)" \)',preload.decode())!=scene['markers']:
        raise ValueError('public control differs')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expected',type=Path,required=True)
    parser.add_argument('--captures',type=Path,required=True)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    frozen=json.loads(args.expected.read_text())
    for source,digest in frozen['repository_sources'].items():
        if hashlib.sha256((ROOT/source).read_bytes()).hexdigest()!=digest:
            raise ValueError('repository source pin differs')
    prepared=markers=0
    for scene in frozen['scenes'].values():
        if len(scene['repeats'])!=2 or len(set(scene['repeats']))!=2:
            raise ValueError('two distinct repeats required')
        for name in scene['repeats']:
            verify(args.captures,name,'observe',scene)
        verify(args.captures,scene['control'],'control',scene)
        prepared+=sum(r['event']=='prepared' for r in scene['normalized']['events'])
        markers+=len(scene['markers'])
    report=dict(passed=True,scenes=len(frozen['scenes']),repeats=2,prepared_members=prepared,public_markers=markers)
    if args.output:
        args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
