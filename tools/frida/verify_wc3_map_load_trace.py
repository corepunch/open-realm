#!/usr/bin/env python3
"""Authenticate complete WPM deserialization and initial path-map hierarchies."""
import argparse
import base64
import hashlib
import json
import struct
from pathlib import Path


def verify_bridge_saved(payload):
    rows=payload.get('rows',[])
    if payload.get('unsaved') is not False or len(rows)!=1 or \
            (rows[0].get('address'),rows[0].get('name'))!=('6f04c860','PathMaps_Load') or \
            not all(s in rows[0].get('comment','') for s in
                ('Payoff139/MAP-02.2','WPM alone','deck(32,20)=1b','no live bridge clears static walking')):
        raise ValueError('bridge terrain annotation not saved or incomplete')
    return len(rows)


def verify_bridge_terrain(fixture, bundle):
    """Verify WPM authority in four completed file loads plus a JASS control.

    The captured support getters and crossing markers remain available, but
    this check does not certify our support geometry or physical trajectories.
    """
    import re
    names=('observe-authored-v4-1','observe-authored-v4-2',
           'observe-blank-v4-1','observe-deckwalk-v5-1','control-authored-v4-1')
    captures={}
    public={}
    for name in names:
        for suffix in ('.jsonl','-preload.txt'):
            filename=name+suffix
            raw=base64.b64decode(bundle['files'][filename],validate=True)
            if hashlib.sha256(raw).hexdigest()!=bundle['sha256'][filename]:
                raise ValueError('bridge capture hash differs: '+filename)
            if suffix=='.jsonl':
                captures[name]=[json.loads(s) for s in raw.decode().splitlines()]
                if name!='control-authored-v4-1' and \
                        hashlib.sha256(raw).hexdigest()!=fixture['captures'][filename]:
                    raise ValueError('bridge frozen capture differs')
            else:
                public[name]=re.findall(r'call Preload\( "(MAP022 [^"\r\n]*)" \)',raw.decode())
        rows=captures[name]
        metadata=[r for r in rows if r.get('event')=='metadata']
        end=[r for r in rows if r.get('event')=='trace-end']
        preload=[r for r in rows if r.get('event')=='preload-file']
        if len(metadata)!=1 or metadata[0].get('sha256')!=fixture['binary_sha256'] or \
                any(r.get('event') in ('error','trace-failed') for r in rows) or \
                len(preload)!=1 or \
                not preload[0].get('complete') or preload[0].get('markers')!=193 or \
                len(public[name])!=193 or 'label=complete' not in public[name][-1]:
            raise ValueError('bridge observer/control incomplete')
        if metadata[0]['mode']!=('control' if name.startswith('control') else 'observe'):
            raise ValueError('bridge observer/control mode differs')
        if name.startswith('observe'):
            if len(end)!=1 or not end[0].get('installed'):
                raise ValueError('bridge observer incomplete')
            if [r['value'] for r in rows if r.get('event')=='marker']!=public[name]:
                raise ValueError('bridge observed public markers differ')
        elif end:
            raise ValueError('bridge control unexpectedly installed observer')
        if preload[0]['sha256']!=bundle['sha256'][name+'-preload.txt']:
            raise ValueError('bridge preload provenance differs')
    if public[names[0]]!=public[names[1]] or public[names[0]]!=public[names[4]]:
        raise ValueError('bridge repeat/control differs')
    cells=0
    for name,variant in zip(names[:4],('authored','authored','blank','deckwalk'),strict=True):
        rows=captures[name]
        loads=[r for r in rows if r.get('event')=='map-load-complete']
        start=[r for r in rows if r.get('event')=='cell-snapshot' and 'label=start ' in r['marker']]
        if len(loads)!=1 or len(start)!=1 or loads[0]['filename']!='war3map.wpm':
            raise ValueError('bridge load/publication snapshot incomplete')
        before,after=loads[0]['snapshot'],start[0]['snapshot']
        if (before['width'],before['height'],after['width'],after['height'])!=(64,64,64,64) or \
                len(before['words'])!=4096 or len(after['words'])!=4096 or before['linked']:
            raise ValueError('bridge map dimensions or initial memberships differ')
        top=bytes(w>>24 for w in before['words'])
        if top!=bytes(w>>24 for w in after['words']) or (variant in fixture['loader'] and
                hashlib.sha256(top).hexdigest()!=fixture['loader'][variant]['after_load']['top_byte_sha256']):
            raise ValueError('bridge changes authored terrain')
        if variant=='blank' and any(top):raise ValueError('blank WPM derives terrain restrictions')
        cells+=len(top)
    return dict(bridge_file_loads=4,bridge_terrain_cells=cells,bridge_control_markers=193)


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
