#!/usr/bin/env python3
"""Complete live fine requests: four classes, same-cell setup and terrain edits.

Read-only captures verify caller inputs/results and production search words.
Owner velocities, physical source recovery and full trajectories are separate.
"""
import argparse
import ctypes
import hashlib
import json
import math
import re
import struct
from pathlib import Path


def normalize(rows):
    selected=[]
    for r in rows:
        if r.get('event')=='terrain-native':selected.append({k:r[k]for k in ('event','x','y','pathingType','passable')})
        elif r.get('event')=='fine-result':selected.append({k:r[k]for k in ('event','source','goal','limit','radius','result','words','work','nodes')})
    return selected


def same_cell_motion(rows,reference=None):
    """Four public lifetimes, including retained buffers and arrival commits.

    Producer metadata/completion is checked by verify() before this supplement.
    Addresses differ across processes; retain words and encounter order instead.
    """
    cases=[dict(requests=[],routes=[],arrivals=[],motion=[])for _ in range(4)]
    case=-1
    for r in rows:
        if r['event']=='marker':
            match=re.search(r'case=(\d+)',r['value'])
            if match:case=int(match[1])
        if not 0<=case<4:continue
        c=cases[case];event=r['event']
        if event=='fine-result':c['requests'].append({k:r[k]for k in ('source','goal','limit','radius','result','words','work','nodes')})
        elif event=='route':c['routes'].append({k:r[k]for k in ('kind','footprint','result','count','points','truncated','indices','flags')})
        elif event=='arrival-evaluation':c['arrivals'].append({k:r[k]for k in ('source','destination','heading','threshold','footprint','storedRange','storedPosition','result','inRange','angle')})
        elif event=='velocity-commit':c['motion'].append([0,r['clock'][0],*r['after'][2:6],r['after'][7]])
    for cls,c in enumerate(cases):
        if len(c['requests'])!=1 or len(c['routes'])!=3 or len(c['motion'])!=7 or len(c['arrivals'])!=7:
            raise ValueError('single-point lifetime/request count differs')
        q=c['requests'][0]
        if q['radius']!=.25+.5*cls or q['work'] or q['nodes'] or q['result']!=1 or q['words']!=[q['goal']]:
            raise ValueError('single-point public setup differs')
        if [r['kind']for r in c['routes']]!=['acc','acc','fine'] or any(r['count']!=1 or r['truncated']or r['result']!=1 for r in c['routes']):
            raise ValueError('single-point retained route differs')
        if c['routes'][-1]['indices']!=[0,0] or c['motion'][-1][4:6]!=[0,0]:
            raise ValueError('single-point index/arrival differs')
        # Initial source8.03125/9.03125 is farther than .49 from the goal.
        source=struct.unpack('<2f',struct.pack('<2I',*q['source']))
        goal=struct.unpack('<2f',struct.pack('<2I',*q['goal']))
        if sum((a-b)**2 for a,b in zip(source,goal))<=.49**2:
            raise ValueError('single-point far-source producer missing')
    result=dict(cases=cases)
    if reference is not None and result!=reference:
        raise ValueError('single-point original route/motion history differs')
    return result


def verify(rows,fixture,capture,engine=None):
    meta=[r for r in rows if r.get('event')=='metadata'];ends=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or len(ends)!=1 or not ends[0].get('installed') or any(r.get('event') in ('trace-failed','error') for r in rows):
        raise ValueError('incomplete fine-result capture')
    actual={k:v for k,v in meta[0].items() if k not in ('event','pid')}
    if actual!=capture['metadata'] or not actual.get('fineResultEvents'):raise ValueError('fine-result producer provenance differs')
    markers=[r['value']for r in rows if r.get('event')=='marker' and 'label=complete case=12 ' in r.get('value','')]
    if len(markers)!=1:raise ValueError('fine-result producer did not complete')
    selected=normalize(rows)
    expected=[fixture['catalog'][i]for i in fixture['sequence']]
    if selected!=expected:raise ValueError('original fine-result words/history differ')
    requests=[r for r in selected if r['event']=='fine-result']
    fast=[r for r in requests if r['nodes']==0]
    if len(fast)!=4 or [r['radius']for r in fast]!=[.25,.75,1.25,1.75] or any(r['work'] or r['result']!=1 or len(r['words'])!=1 for r in fast):
        raise ValueError('same-cell four-class setup differs')
    if engine:
        class Objects(ctypes.Structure):_fields_=[('cells',ctypes.POINTER(ctypes.c_uint8)),('objects',ctypes.POINTER(ctypes.c_uint32))]
        engine.pathing_fine_result_reset()
        engine.pathing_fine_result_words.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(Objects),ctypes.POINTER(ctypes.c_uint32)]
        grid=(ctypes.c_uint8*4096)()
        for r in selected:
            if r['event']=='terrain-native':
                if r['pathingType']!=1:raise ValueError('unexpected live terrain policy')
                x,y=math.floor(r['x']/32),math.floor(r['y']/32)
                if not 0<=x<64 or not 0<=y<64:raise ValueError('terrain edit outside supplied map')
                grid[y*64+x]=0 if r['passable'] else 2;continue
            source=struct.unpack('<2f',struct.pack('<2I',*r['source']));goal=struct.unpack('<2f',struct.pack('<2I',*r['goal']))
            cls=3 if r['radius']>=1.5 else 2 if r['radius']>=1 else 1 if r['radius']>=.5 else 0
            q=(ctypes.c_uint32*16)(64,64,*(math.floor(v)for v in (*source,*goal)),r['limit'],cls,0x02000002,0,0,0xffffffff,*r['source'],*r['goal'])
            out=(ctypes.c_uint32*(7+2*32768))();engine.pathing_fine_result_words(q,ctypes.byref(Objects(grid,None)),out)
            words=[v for p in r['words']for v in p]
            if list(out[:4])!=[r['result'],r['work'],r['nodes'],len(r['words'])] or list(out[7:7+len(words)])!=words:
                raise ValueError('production C fine request differs')
    return len(requests)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture',type=Path,nargs=2,required=True);p.add_argument('--fixture',type=Path,required=True);p.add_argument('--engine-library',type=Path);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--same-cell-reference',type=Path,help='compare four full public single-point lifetimes, retained routes and motion')
    a=p.parse_args();fixture=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()))if a.engine_library else None
    counts=[];same_cell=[]
    for path,capture in zip(a.capture,fixture['captures']):
        raw=path.read_bytes()
        if len(raw)!=capture['bytes'] or hashlib.sha256(raw).hexdigest()!=capture['sha256']:raise ValueError('fine-result capture hash/length differs')
        rows=[json.loads(line)for line in raw.splitlines()if line.strip()]
        counts.append(verify(rows,fixture,capture,engine))
        if a.same_cell_reference:
            result=same_cell_motion(rows,json.loads(a.same_cell_reference.read_text()))
            same_cell.append(result)
    report=dict(passed=True,cases=2,fine_requests_each=counts[0],engine_exact_requests=sum(counts)if engine else 0,whole_retail_pathfinder=False)
    if same_cell:report.update(same_cell_lifetimes=8,same_cell_motion_each=28)
    a.report.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
