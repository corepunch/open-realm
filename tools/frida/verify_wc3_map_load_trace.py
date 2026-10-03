#!/usr/bin/env python3
"""Authenticate complete WPM deserialization and initial path-map hierarchies."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def expand(runs, size):
    if any(type(n) is not int or n<=0 or type(v) is not int or not 0<=v<=255 for n,v in runs):
        raise ValueError('invalid numerical runs')
    if sum(n for n,v in runs)!=size:
        raise ValueError('numerical run extent differs')
    return [v for n,v in runs for _ in range(n)]


def wpm_flags(byte):
    result=0
    for bit,flag in [(2,0x13),(4,5),(8,9),(32,32),(64,65)]:
        if byte&bit:result|=flag
    if byte&128 or byte&0x42==0x42:result|=0x81
    return result


def verify_load(rows, fixture):
    if fixture['version']!=1 or fixture['whole_retail_pathfinder'] is not False:
        raise ValueError('file-load scope differs')
    if [i for i,r in enumerate(rows) if r.get('event')=='trace-end']!=[len(rows)-1] or \
            not rows[-1].get('installed') or any(r.get('event') in ('error','trace-failed') for r in rows):
        raise ValueError('file-load observer failed or did not finish')
    loads=[r for r in rows if r.get('event')=='map-load-complete']
    maps=[r for r in rows if r.get('event')=='maps']
    if len(loads)!=1 or len(maps)!=1 or rows[-1]['counts'].get('map-load-complete')!=1:
        raise ValueError('complete native loader/constructor missing or duplicated')
    initial=[{k:v for k,v in m.items() if k!='pointer'} for m in maps[0]['maps']]
    if initial!=fixture['initial_maps']:
        raise ValueError('initial dimensions or scales differ')
    load=loads[0];geometry=fixture['geometry']
    for key in ('filename','inputBounds','worldBounds','width','height','constants'):
        if load[key]!=geometry[key]:raise ValueError('file-load field differs: '+key)
    size=geometry['width']*geometry['height']
    expected=expand(geometry['cell_runs'],size)
    if load['cells']!=expected:raise ValueError('decoded fine cells differ')
    wpm=fixture['wpm'];raw=bytes(expand(wpm['runs'],size))
    payload=struct.pack('<4I',*wpm['header'])+raw
    if wpm['header']!=[0x5733504d,0,geometry['width'],geometry['height']] or \
            len(payload)!=wpm['bytes'] or hashlib.sha256(payload).hexdigest()!=wpm['sha256']:
        raise ValueError('original WPM bytes or extent differ')
    if [wpm_flags(v) for v in raw]!=expected:
        raise ValueError('native deserialization differs from original WPM')
    if len(load['hierarchy'])!=4:raise ValueError('hierarchy incomplete')
    classes=0
    for actual,expected in zip(load['hierarchy'],geometry['hierarchy'],strict=True):
        for key in ('level','width','height'):
            if actual[key]!=expected[key]:raise ValueError('hierarchy dimensions differ')
        values=expand(expected['runs'],expected['width']*expected['height'])
        if actual['values']!=[[(v>>s)&3 for s in (6,4,2,0)] for v in values]:
            raise ValueError('initial hierarchy classes differ')
        classes+=len(values)*4
    return dict(file_loads=1,fine_cells=size,hierarchy_classes=classes)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces',nargs=2,type=Path)
    p.add_argument('--fixture',required=True,type=Path)
    p.add_argument('--report',required=True,type=Path)
    args=p.parse_args();fixture=json.loads(args.fixture.read_text());results=[]
    for path,expected in zip(args.traces,fixture['captures'],strict=True):
        if hashlib.sha256(path.read_bytes()).hexdigest()!=expected['sha256'] or path.stat().st_size!=expected['bytes']:
            raise ValueError('capture hash or extent differs')
        rows=[json.loads(s) for s in path.read_text().splitlines()]
        metadata=[{k:v for k,v in r.items() if k not in ('event','pid')} for r in rows if r.get('event')=='metadata']
        if metadata!=[expected['metadata']]:raise ValueError('file/source/binary provenance differs')
        results.append(verify_load(rows,fixture))
    report=dict(passed=True,cases=2,file_loads=2,fine_cells=sum(r['fine_cells'] for r in results),
                hierarchy_classes=sum(r['hierarchy_classes'] for r in results),results=results,
                whole_retail_pathfinder=False,scope=fixture['scope'])
    args.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':main()
