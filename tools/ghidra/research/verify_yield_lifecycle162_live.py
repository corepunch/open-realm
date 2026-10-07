#!/usr/bin/env python3
"""Verify archived public yield/identity witnesses and complete frozen owner windows."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/frida/research'))
from route03_expected import digest,markers_from_preload,stream,visits

DLL='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def validate_rows(rows,mode):
    metadata=[r for r in rows if r.get('event')=='metadata']
    if len(metadata)!=1 or rows[0]!=metadata[0]:raise ValueError('one leading metadata record required')
    meta=metadata[0]
    if meta.get('sha256')!=DLL or meta.get('owned') is not True or meta.get('mode')!=mode or meta.get('task')!='ROUTE-05.1':
        raise ValueError('capture provenance differs')
    if any(r.get('type')=='error' or r.get('event') in ('error','trace-failed') for r in rows):raise ValueError('failed capture')
    footer=[r for r in rows if r.get('event')=='preload-file']
    if len(footer)!=1 or footer[0].get('complete') is not True:raise ValueError('incomplete public scene')
    markers=[r['value'] for r in rows if r.get('event')=='marker']
    ends=[r for r in rows if r.get('event')=='trace-end']
    if mode=='observe':
        if len(ends)!=1 or not all(ends[0].get(k) is True for k in ('installed','finished')):raise ValueError('incomplete observer')
        if len(markers)!=footer[0]['markers']:raise ValueError('marker count differs')
    elif any(r.get('event') not in ('metadata','loading-key','preload-file','owned-process-already-exited') for r in rows):
        raise ValueError('instrumented control')
    return markers,footer[0]


def verify_capture(root,name,spec):
    path=root/name;raw=path.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=spec['sha256']:raise ValueError('capture pin differs: '+name)
    rows=[json.loads(line) for line in raw.splitlines()]
    markers,footer=validate_rows(rows,spec['mode'])
    meta={k:v for k,v in rows[0].items() if k not in ('event','pid')}
    if meta!=spec['metadata']:raise ValueError('metadata pin differs')
    preload=path.with_name(path.stem+'-preload.txt')
    if hashlib.sha256(preload.read_bytes()).hexdigest()!=footer['sha256']:raise ValueError('public preload pin differs')
    public=markers_from_preload(preload)
    if len(public)!=footer['markers'] or (spec['mode']=='observe' and public!=markers):raise ValueError('public timeline differs')
    if spec['mode']=='observe':
        items,_=stream(path)
        if digest(items)!=spec['normalized_digest']:raise ValueError('complete normalized stream differs')
        proc=subprocess.run([sys.executable,str(ROOT/'tools/frida/research/route05_1_verify.py'),
                             '--kind',spec['kind'],str(path)],capture_output=True,text=True)
        if proc.returncode or json.loads(proc.stdout).get('ok') is not True:raise ValueError('yield lifetime differs: '+proc.stdout)
    return public


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--expected',type=Path,required=True)
    ap.add_argument('--captures',type=Path,required=True)
    ap.add_argument('--output',type=Path)
    args=ap.parse_args();frozen=json.loads(args.expected.read_text())
    for name,pin in frozen['repository_sources'].items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=pin:raise ValueError('repository source pin differs: '+name)
    original=json.loads(gzip.decompress((ROOT/frozen['original_fixture']).read_bytes()))
    if original['binary_sha256']!=DLL:raise ValueError('original binary differs')
    public={name:verify_capture(args.captures,name,spec) for name,spec in frozen['captures'].items()}
    records=0
    for case in original['cases'].values():
        path=args.captures/case['capture'];items,_=stream(path)
        if visits(items,*case['window'])!=case['visits']:raise ValueError('frozen owner window differs')
        records+=len(case['visits'])
    controls=0
    for observed,control in frozen['controls']:
        if public[observed]!=public[control]:raise ValueError('observer-free control differs')
        controls+=1
    report=dict(passed=True,captures=sum(s['mode']=='observe' for s in frozen['captures'].values()),
                controls=controls,owner_records=records,policy_cases=len(original['cases']))
    if args.output:args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
