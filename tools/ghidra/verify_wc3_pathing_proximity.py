#!/usr/bin/env python3
"""Replay complete repeated retail separation bodies and preserve the engine fixture.

Original pair slices/tails run on read-only live inputs. Engine tests separately
execute complete updates, including actual proximity enumeration and admission.
Native storage counters/forced stamp repair are outside the effective index.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import sys
import tempfile

HERE=Path(__file__).resolve().parent
FIXTURES=HERE/'fixtures'
EXPECTED_SHA='e237fe7b55654d0f037630394d3732157d632f1aa2a41c085ddb01ac954aaeca'


def restore_inputs(root):
    bundle=json.loads(gzip.decompress((FIXTURES/'retail-proximity-inputs-1.27.json.gz').read_bytes()))
    if set(bundle['files'])!=set(bundle['sha256']):raise ValueError('incomplete capture hash inventory')
    for name,text in bundle['files'].items():
        path=Path(name)
        if path.is_absolute() or '..' in path.parts:raise ValueError('unsafe input path')
        raw=text.encode()
        if hashlib.sha256(raw).hexdigest()!=bundle['sha256'][name]:raise ValueError('input differs: '+name)
        target=root/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(raw)
    return bundle


def validate_capture(cap):
    ends=[r for r in cap.rows if r.get('event')=='trace-end']
    if not cap.complete or len(ends)!=1 or not ends[0].get('installed') or ends[0].get('caps'):
        raise ValueError('incomplete live capture')
    if any(r.get('event') in ('error','observer-error','script-error','trace-failed') for r in cap.rows):
        raise ValueError('live capture error')
    if len(cap.preload)!=3543 or len(cap.updates)!=5601:raise ValueError('truncated live operation')


def check_fixture(expected,root):
    source=(HERE.parents[1]/'games/warcraft-3/game/tests/retail_repulsion_triad.h').read_text()
    visits=source.split('triad_visits[]={',1)[1].split('};',1)[0]
    actual=[tuple(int(v.rstrip('u'),0) for v in row.split(',')) for row in re.findall(r'\{([^{}]+)\}',visits)]
    rows=[row for group in expected['groups'] for row in group['sequence']]
    desired=[(row['i'],*row['posAfter'],*row['vecAfter'],row['wordAfter'],*row['occAfter']) for row in rows]
    if actual!=desired:raise ValueError('complete engine visit fixture differs')
    units=source.split('triad_units[]={',1)[1].split('};',1)[0]
    actual_units=[tuple(map(int,re.findall(r'\d+',row))) for row in re.findall(r'\{([^{}]+)\}',units)]
    sys.path.insert(0,str(HERE.parent/'frida/research'))
    from sep_research_map import UNITS
    desired_units=[]
    for unit in json.loads((root/'map.json').read_text())['roster'][:27]:
        first=next((r for r in rows if r['i']==unit['i']),None)
        pos=first['posBefore'] if first else [struct.unpack('<I',struct.pack('<f',unit[a]/32))[0] for a in ('x','y')]
        data=UNITS[unit['code']]
        desired_units.append((*pos,unit['owner'],data.get('urpp',0),data.get('urpr',0),data.get('urpo',0),int(data.get('ucol',8))))
    if actual_units!=desired_units:raise ValueError('engine authored inputs differ')
    offsets=[0]
    for group in expected['groups']:offsets.append(offsets[-1]+len(group['sequence']))
    actual_offsets=list(map(int,re.findall(r'\d+',source.split('triad_offsets[]={',1)[1].split('}',1)[0])))
    if offsets!=actual_offsets:raise ValueError('engine visit boundaries differ')
    pair_source=source.split('triad_pairs[][14]={',1)[1].split('};',1)[0]
    actual_pairs=[tuple(int(v.rstrip('u'),0) for v in row.split(',')) for row in re.findall(r'\{([^{}]+)\}',pair_source)]
    pairs=[(row['i'],p['j'],*p['source'],*p['candidate'],*p['vecBefore'],*p['vecAfter'],*p['ownerBefore'],*p['ownerAfter'])
           for row in rows for p in row.get('neighbors',[])]
    if actual_pairs!=pairs:raise ValueError('individual engine pair fixture differs')
    offsets=[0]
    for group in expected['groups']:offsets.append(offsets[-1]+sum(len(r.get('neighbors',[])) for r in group['sequence']))
    actual_offsets=list(map(int,re.findall(r'\d+',source.split('triad_pair_offsets[]={',1)[1].split('}',1)[0])))
    if offsets!=actual_offsets:raise ValueError('pair boundaries differ')
    indices={(row['i'],row['visit']):i for i,row in enumerate(rows)}
    replay=json.loads((root/'replay-first.json').read_text())['rows']
    schedule=[indices[(r['i'],r['visit'])] for r in replay if (r['i'],r['visit']) in indices]
    actual_schedule=list(map(int,re.findall(r'\d+',source.split('triad_schedule[]={',1)[1].split('}',1)[0])))
    if actual_schedule!=schedule:raise ValueError('global engine visit order differs')
    return len(rows)


def verify(binary,extra=None):
    sys.path.insert(0,str(HERE/'research'))
    sys.path.insert(0,str(HERE.parent/'frida/research'))
    from sep_research_oracle import Oracle
    spec=importlib.util.spec_from_file_location('triad_replay',HERE/'research/verify_SEP-02.2_replay.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    from sep_research_analyze import Capture,preload_rows
    from sep_research_expected import norm
    raw=gzip.decompress((FIXTURES/'research/SEP-02.2-expected.json.gz').read_bytes())
    if hashlib.sha256(raw).hexdigest()!=EXPECTED_SHA:raise ValueError('changed frozen triad contract')
    expected=json.loads(raw)
    oracle=Oracle(binary)
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory);bundle=restore_inputs(root)
        captures=[Capture(root/f'triad-observe-{i}',root/'map.json') for i in (1,2)]
        sequences=[];stats=[];verified_rows=[]
        for i,cap in enumerate(captures):
            validate_capture(cap)
            rows,counts=module.replay(cap,oracle)
            rows=json.loads(json.dumps(rows))
            if any(not row['match'] for row in rows):raise ValueError('original pair/tail replay mismatch')
            old=json.loads((root/('replay-first.json' if not i else 'replay-repeat.json')).read_text())
            if rows!=old['rows'] or dict(counts)!=old['stats']:raise ValueError('complete original replay differs')
            groups=[[norm(row) for row in rows if row['i'] in group['units']] for group in expected['groups']]
            if groups!=[g['sequence'] for g in expected['groups']]:raise ValueError('full normalized sequence differs')
            sequences.append(groups);stats.append(dict(counts));verified_rows.append(rows)
        if sequences[0]!=sequences[1]:raise ValueError('repeated word sequence differs')
        control=preload_rows((root/'triad-control-1/preload.txt').read_text())
        if len(control)!=3543 or any(cap.preload!=control for cap in captures):raise ValueError('observer-free control differs')
        provenance=json.loads((root/'triad-control-1/provenance.json').read_text())
        if provenance['mode']!='control' or provenance['observer_sha256'] is not None or not provenance['preload_complete']:
            raise ValueError('invalid observer-free provenance')
        visits=check_fixture(expected,root)
        additional=extra(root,captures,verified_rows) if extra else {}
        return dict(status='verified',differences=[],binary_sha256=oracle.sha,cases=visits,
            live_updates=5601,live_bodies=stats[0]['body'],neighbor_contributions=stats[0]['pairs'],
            live_captures=2,control_markers=len(control),input_files=len(bundle['files']),groups=len(expected['groups']),**additional)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True);parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();result=verify(args.binary.resolve())
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
