#!/usr/bin/env python3
"""Replay public ground CreateUnit admission, sentinel commits and exact repeats."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re
from verify_wc3_arrival_trace import configure, words
from verify_wc3_motion_trace import verify as verify_motion

KINDS=('position-marker','position-native','position-query','position-commit','placement-search-end')
CASES=tuple('spawn_'+str(i) for i in range(8))


def normalized(rows):
    return [{k:v for k,v in r.items() if k not in ('ms','unit','mover','handle')}
            for r in rows if r.get('event') in KINDS]


def digest(rows):
    return hashlib.sha256(json.dumps(normalized(rows),separators=(',',':')).encode()).hexdigest()


def verify(rows,engine,fixture):
    metas=[r for r in rows if r.get('event')=='metadata'];ends=[r for r in rows if r.get('event')=='trace-end']
    if any(r.get('event') in ('error','trace-failed') or r.get('type')=='error' for r in rows):
        raise ValueError('spawn observer failed')
    if len(metas)!=1 or metas[0].get('sha256')!=fixture['binary_sha256'] or metas[0].get('source_sha256')!=fixture['source_sha256']:
        raise ValueError('spawn target/source differs')
    if (not all(metas[0].get(k) for k in ('owned','motionEvents','profileEvents')) or
            len(ends)!=1 or not ends[0].get('installed')):raise ValueError('spawn incomplete observer')
    for kind,count in [('position-native',56),('position-query',104),('position-commit',16),
                       ('placement-search-begin',8),('placement-search-end',8),('motion-decision',239)]:
        if sum(r.get('event')==kind for r in rows)!=count or ends[0].get('counts',{}).get(kind)!=count:
            raise ValueError('spawn truncated '+kind)
    if digest(rows)!=fixture['spawn_sha256']:raise ValueError('spawn sequence differs')
    samples=[r['value'] for r in rows if r.get('event')=='marker' and 'label=sample ' in r.get('value','')]
    if [int(re.search(r'tick=(\d+)',r)[1]) for r in samples]!=list(range(1,301)):
        raise ValueError('spawn public samples incomplete')
    marks=[r['value'] for r in rows if r.get('event')=='marker']
    if (sum('tick=300 label=complete ' in r for r in marks)!=1 or
            sum('label=start_spawn_admission ' in r for r in marks)!=1 or
            any('rejected' in r for r in marks)):raise ValueError('spawn admission/completion differs')
    if [r['value'] for r in rows if r.get('event')=='position-marker']!=[
            mark for c in CASES for mark in ('PATHPOSE case='+c,'PATHPOSE done='+c)]:
        raise ValueError('spawn brackets differ')
    engine.pathing_position_bridge.argtypes=[ctypes.POINTER(ctypes.c_uint32)]*2
    engine.pathing_spawn_position.argtypes=[ctypes.POINTER(ctypes.c_uint32)]*2
    engine.pathing_world_grid.argtypes=[ctypes.POINTER(ctypes.c_uint32)]*2
    engine.pathing_native_pose.argtypes=[ctypes.POINTER(ctypes.c_uint32)]*2
    engine.pathing_fine_placement.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint8),ctypes.POINTER(ctypes.c_uint32)]
    sources=set()
    for case in CASES:
        calls=[r for r in rows if r.get('event')=='position-native' and r.get('case')==case]
        searches=[r for r in rows if r.get('event')=='placement-search-end' and r.get('case')==case]
        commits=[r for r in rows if r.get('event')=='position-commit' and r.get('case')==case]
        if ([r.get('name') for r in calls]!=['CreateUnit','GetUnitX','GetUnitY','GetUnitX','GetUnitY','GetUnitX','GetUnitY']
                or len(searches)!=1 or len(commits)!=2):raise ValueError('spawn native/placement order differs')
        create=calls[0];handle=create.get('output');actor=calls[1].get('unit')
        if (not handle or actor in (None,'0x0') or any(r.get('rawcode')!=fixture['rawcode'] for r in calls)
                or calls[1].get('handle')!=handle or calls[2].get('handle')!=handle
                or calls[1].get('unit')!=calls[2].get('unit')):raise ValueError('spawn created actor differs')
        sources.add((calls[3].get('unit'),calls[3].get('handle')))
        if any((r.get('unit'),r.get('handle'))!=(calls[3].get('unit'),calls[3].get('handle')) for r in calls[3:]):
            raise ValueError('spawn source actor differs')
        search=searches[0]
        first,second=commits
        coord=(ctypes.c_uint32*6)()
        engine.pathing_world_grid((ctypes.c_uint32*6)(*words(create.get('input'),3)[:2],*words(first.get('origin'),2),0x42000000,0x42000000),coord)
        if list(coord)[:2]!=search.get('before'):raise ValueError('spawn requested fine conversion differs')
        if (search.get('policy')!=2 or search.get('radius')!=0x3f780000 or search.get('mask')!=0x02000002
                or search.get('limit')!=32 or search.get('callback')!='0x654060' or search.get('context')!=6
                or search.get('integerResult')!=0 or search.get('result')!=1
                or search.get('savedMode')!=search.get('restoredMode')
                or search.get('rect')!=[search['before'][1],search['before'][0]]*2):
            raise ValueError('spawn actual admission arguments differ')
        # The only local obstacle is the stationary source's class1 half-open
        # fine rectangle. Use equivalent supplied cells to replay admission.
        cells=(ctypes.c_uint8*(256*128))()
        for y in (64,65):
            for x in (162,163):cells[y*256+x]=2
        visits=search.get('visits')
        if not visits or [v.get('result') for v in visits]!=[0]*(len(visits)-1)+[1]:
            raise ValueError('spawn first accepted candidate differs')
        for v in visits:
            legal=all(0<=x<256 and 0<=y<128 and not cells[y*256+x]
                for y in range(v['point'][1]-1,v['point'][1]+1) for x in range(v['point'][0]-1,v['point'][0]+1))
            if v.get('mask')!=search['mask'] or v.get('cls')!=1 or legal!=bool(v['result']):
                raise ValueError('spawn footprint visit differs')
        out=(ctypes.c_uint32*3)()
        engine.pathing_fine_placement((ctypes.c_uint32*8)(256,128,*words(search['before'],2),32,1,search['mask'],0),cells,out)
        if list(out)!=[1,*search['after']]:raise ValueError('spawn C endpoint differs')
        engine.pathing_world_grid((ctypes.c_uint32*6)(*search['after'],0,0,0x3f800000,0x3f800000),coord)
        if visits[-1]['point']!=list(coord)[2:4]:raise ValueError('spawn final candidate differs')
        point=(ctypes.c_uint32*4)()
        engine.pathing_native_pose((ctypes.c_uint32*7)(*search['after'],0,0,*first['origin'],0),point)
        if list(point)[2:]!=first['input'][:2]:raise ValueError('spawn admitted world projection differs')
        if (first.get('notify')!=0 or second.get('notify')!=1 or first['before'][2:6]!=[0xc7fa0040]*2+[0,0]
                or first['before'][6]!=0x7f7fffff or first['after'][2:6]!=second['before'][2:6]
                or first['after'][2:6]!=second['after'][2:6]):raise ValueError('spawn initialization chain differs')
        pose=(ctypes.c_uint32*4)()
        engine.pathing_spawn_position((ctypes.c_uint32*4)(*words(first['origin'],2),*words(first['input'],3)[:2]),pose)
        if list(pose)!=[*second['after'][2:4],*[r['output'] for r in calls[1:3]]]:
            raise ValueError('spawn C initial/public pose differs')
        if second['input'][:2]!=[r['output'] for r in calls[1:3]]:
            raise ValueError('spawn canonical second write differs')
        queries=[r for r in rows if r.get('event')=='position-query' and r.get('case')==case
            and r.get('native') in ('GetUnitX','GetUnitY')]
        if len(queries)!=6 or any(r.get('native')!=c['name'] or r.get('unit')!=c.get('unit')
                or r['output'][0 if c['name']=='GetUnitX' else 1]!=c.get('output') for r,c in zip(queries,calls[1:])):
            raise ValueError('spawn public getter bridge differs')
        if not rows.index(search)<rows.index(first)<rows.index(second)<rows.index(create)<rows.index(calls[1]):
            raise ValueError('spawn admission/write/publication order differs')
    if len(sources)!=1:raise ValueError('spawn stationary source lifetime differs')
    for r in rows:
        if r.get('event') not in ('position-query','position-commit'):continue
        before,after=words(r.get('before'),8),words(r.get('after'),8)
        clock,origin=words(r.get('clock'),3),words(r.get('origin'),2)
        point=words(r.get('input') if r['event']=='position-commit' else r.get('output'),3)
        out=(ctypes.c_uint32*12)();inp=before+clock+origin+point[:2]
        engine.pathing_position_bridge((ctypes.c_uint32*15)(*inp),out)
        if r['event']=='position-query':
            if before!=after or list(out)[10:12]!=point[:2] or point[2]!=0:raise ValueError('spawn predicted getter differs')
        elif list(out)[:8]!=after or point[2]!=0:raise ValueError('spawn scalar commit differs')
    result=verify_motion(rows,engine,None)
    result.update(binary_sha256=fixture['binary_sha256'],source_sha256=fixture['source_sha256'],public_create_calls=8,position_queries=104,
        position_commits=16,placement_searches=8,spawn_sha256=digest(rows),
        scope='Ordinary in-map ground CreateUnit overlap admission and initial scalar commits. Loc forwards in engine tests; original Loc/other actor forms, terrain levels, bridges, outside-map and whole engine cadence remain separate.')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path);parser.add_argument('--repeat',type=Path)
    parser.add_argument('--fixture',type=Path,required=True);parser.add_argument('--engine-library',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True);args=parser.parse_args()
    engine=ctypes.CDLL(str(args.engine_library.resolve()));configure(engine)
    fixture=json.loads(args.fixture.read_text());read=lambda p:[json.loads(s) for s in p.read_text().splitlines()]
    result=verify(read(args.trace),engine,fixture)
    if args.repeat:
        other=verify(read(args.repeat),engine,fixture)
        if any(result[k]!=other[k] for k in ('spawn_sha256','decision_sha256')):raise ValueError('spawn repeat differs')
        result['repeated']=True
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':main()
