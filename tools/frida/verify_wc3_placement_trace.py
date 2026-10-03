#!/usr/bin/env python3
"""Verify repeated public blocked-placement candidates and production scalar writes."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re
from verify_wc3_arrival_trace import configure, words
from verify_wc3_motion_trace import verify as verify_motion

CASES = ('blocked_centre','blocked_fractional','blocked_west','blocked_east','blocked_north','blocked_large')
KINDS = ('terrain-native','position-marker','position-native','position-query','position-commit',
         'forced-position-stop-begin','forced-position-stop-end','placement-search-begin','placement-search-end')


def normalized(rows, pathing_toggle=False):
    return [{k:v for k,v in row.items() if k not in ('ms','unit','mover','handle')}
            for row in rows if row.get('event') in (KINDS+('pathing-toggle','movement-mask-publication') if pathing_toggle else KINDS)]


def digest(rows, pathing_toggle=False):
    return hashlib.sha256(json.dumps(normalized(rows,pathing_toggle),separators=(',',':')).encode()).hexdigest()


def verify(rows, engine, fixture, pathing_toggle=False):
    cases=tuple("pathing_position_"+str(t) for t in (20,30,40,50,60)) if pathing_toggle else CASES
    meta=[r for r in rows if r.get('event')=='metadata']
    end=[r for r in rows if r.get('event')=='trace-end']
    if any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('placement observer error')
    if len(meta)!=1 or meta[0].get('sha256')!=fixture['binary_sha256'] or meta[0].get('source_sha256')!=fixture['source_sha256']:
        raise ValueError('placement target/source differs')
    if not all(meta[0].get(k) for k in ('owned','motionEvents','velocityEvents','profileEvents' if pathing_toggle else 'taskEvents')) or len(end)!=1 or not end[0].get('installed'):
        raise ValueError('placement observer options/completion missing')
    if digest(rows,pathing_toggle)!=fixture['placement_sha256']: raise ValueError('placement sequence differs')
    for kind,count in (('position-native',25 if pathing_toggle else 30),('position-query',65 if pathing_toggle else 78),('position-commit',5 if pathing_toggle else 6),
                       ('forced-position-stop-begin',5 if pathing_toggle else 6),('forced-position-stop-end',5 if pathing_toggle else 6),
                       ('placement-search-begin',7),('placement-search-end',7)):
        if sum(r.get('event')==kind for r in rows)!=count or end[0].get('counts',{}).get(kind)!=count:
            raise ValueError('placement missing/truncated '+kind)
    markers=[r['value'] for r in rows if r.get('event')=='position-marker']
    if markers != [s for c in cases for s in ('PATHPOSE case='+c,'PATHPOSE done='+c)]:
        raise ValueError('placement producer brackets differ')
    edits=[(r.get('x'),r.get('y'),r.get('pathingType'),r.get('passable')) for r in rows if r.get('event')=='terrain-native']
    expected=[(-2000+32*x,-576+32*y,1,0) for radius in (1,2) for x in range(2-radius,3+radius) for y in range(2-radius,3+radius)]
    if pathing_toggle: expected=[(-2000+32*x,-560,1,0) for x in range(4)]
    if edits!=expected: raise ValueError('placement actual terrain edits differ')
    samples=[r['value'] for r in rows if r.get('event')=='marker' and 'label=sample ' in r.get('value','')]
    if [int(re.search(r'tick=(\d+)',r)[1]) for r in samples]!=list(range(1,301)):
        raise ValueError('placement public samples incomplete')
    if sum(r.get('event')=='marker' and 'label=complete ' in r.get('value','') for r in rows)!=1:
        raise ValueError('placement public completion missing')
    natives=[r for r in rows if r.get('event')=='position-native']
    actors={(r.get('unit'),r.get('handle'),r.get('rawcode')) for r in natives}
    if len(actors)!=1 or next(iter(actors))[0] in (None,'0x0') or next(iter(actors))[2]!=fixture['rawcode']:
        raise ValueError('placement public actor differs')
    native_unit=next(iter(actors))[0]
    if pathing_toggle:
        toggles=[r for r in rows if r.get('event')=='pathing-toggle']
        if [(r.get('enabled'),r.get('phase')) for r in toggles]!=[(flag,phase) for flag in (1,0,1,0,1) for phase in ('enter','leave')]:
            raise ValueError('placement lacks five public pathing toggle brackets')
        handle=next(iter(actors))[1]
        if any(r.get('handle')!=handle for r in toggles): raise ValueError('placement pathing toggle receiver differs')
        identities=set()
        for start,end in zip(toggles[::2],toggles[1::2]):
            pubs=[r for r in rows[rows.index(start)+1:rows.index(end)] if r.get('event')=='movement-mask-publication']
            mask=2 if start['enabled'] else 0
            if len(pubs)!=1 or tuple(pubs[0].get(k) for k in ('rawcode','category','queryMask','objectCategory','pathMask'))!=(fixture['rawcode'],202,mask,0x010000ca,mask|mask<<24):
                raise ValueError('placement toggle changed category or own query mask')
            identities.add((pubs[0].get('mover'),tuple(pubs[0].get('identity',[]))))
        if len(identities)!=1: raise ValueError('placement toggle changed mover identity')

    for r in rows:
        if r.get('event') not in ('position-query','position-commit'): continue
        if r.get('unit')!=native_unit: raise ValueError('placement bridge actor differs')
        point=words(r['input'] if r['event']=='position-commit' else r['output'],3)
        state=(ctypes.c_uint32*15)(*r['before'],*r['clock'],*r['origin'],*point[:2])
        out=(ctypes.c_uint32*12)(); engine.pathing_position_bridge(state,out)
        if r['event']=='position-query':
            if list(out)[10:12]!=point[:2] or point[2]!=0 or r['before']!=r['after']:
                raise ValueError('placement predicted query differs')
        elif r.get('notify')!=1 or list(out)[:8]!=r['after'] or r['after'][4:8]!=r['before'][4:8]:
            raise ValueError('placement scalar commit differs')
    for case in cases:
        calls=[r for r in natives if r['case']==case]
        if [r['name'] for r in calls]!=['GetUnitX','GetUnitY','SetUnitPosition','GetUnitX','GetUnitY']:
            raise ValueError('placement public native order differs')
        getters=[r for r in calls if r['name'].startswith('Get')]
        queries=[r for r in rows if r.get('event')=='position-query' and r.get('case')==case and r.get('native') in ('GetUnitX','GetUnitY')]
        if len(queries)!=4 or any(c['name']!=r['native'] or c['output']!=r['output'][0 if c['name']=='GetUnitX' else 1] for c,r in zip(getters,queries)):
            raise ValueError('placement public getter result differs')
        if calls[2]['after']['pose'][4:6]!=[0,0] or any(calls[2]['after'][k]!=[-1,-1] for k in ('taskHead','orderHead','group')):
            raise ValueError('placement native retained movement state')
    searches=[r for r in rows if r.get('event')=='placement-search-end']
    if [r['limit'] for r in searches]!=([32,32,5,32,32,5,32] if pathing_toggle else [32]*5+[5,32]):
        raise ValueError('placement public/embedded recovery limits differ')
    final_label='label=pathing_position_after ' if pathing_toggle else 'label=placement_after_blocked_large '
    final=next(r['value'] for r in reversed(rows) if r.get('event')=='marker' and final_label in r.get('value',''))
    final_xy=re.search(r' x=([^ ]+) y=([^ ]+) order=0$',final)
    if final_xy is None or any(re.search(r' x=([^ ]+) y=([^ ]+) order=0$',r) is None or re.search(r' x=([^ ]+) y=([^ ]+) order=0$',r).groups()!=final_xy.groups() for r in samples if int(re.search(r'tick=(\d+)',r)[1])>(60 if pathing_toggle else 50)):
        raise ValueError('placement continued moving after final write')
    for r in rows:
        if r.get('event')!='placement-search-end': continue
        mask=0 if pathing_toggle and r['case'] in ('pathing_position_30','pathing_position_50') else 0x02000002
        if r.get('unit')!=native_unit or r['policy']!=2 or r['radius']!=0x3f780000 or r['mask']!=mask or r['limit'] not in (5,32) or r['callback']!='0x654060' or r['context']!=6 or r['integerResult']!=0:
            raise ValueError('placement actual admission arguments differ')
        if r['savedMode']!=r['restoredMode'] or r['result']!=1 or r['rect']!=[r['before'][1],r['before'][0]]*2:
            raise ValueError('placement search mode/rectangle/result differs')
        visits=r['visits']
        if not visits or any(v['mask']!=r['mask'] or v['cls']!=1 for v in visits) or [v['result'] for v in visits]!=[0]*(len(visits)-1)+[1]:
            raise ValueError('placement footprint/callback admission differs')
        # Synthetic edited patch reproduces every observed cell decision. Other
        # original terrain/object cells and bridge callbacks are not inferred.
        radius=2 if r['case']=='blocked_large' else 1
        cells=(ctypes.c_uint8*(256*128))()
        for y in range(80-radius,81+radius):
            for x in range(163-radius,164+radius): cells[y*256+x]=2
        if pathing_toggle:
            cells=(ctypes.c_uint8*(256*128))()
            for x in range(161,165): cells[78*256+x]=2
        for v in visits:
            legal=all(0<=x<256 and 0<=y<128 and not cells[y*256+x]&(mask & 0xff)
                      for y in range(v['point'][1]-1,v['point'][1]+1)
                      for x in range(v['point'][0]-1,v['point'][0]+1))
            if legal!=bool(v['result']): raise ValueError('placement observed terrain cell differs')
        query=(ctypes.c_uint32*8)(256,128,*r['before'],r['limit'],1,r['mask'],0)
        out=(ctypes.c_uint32*3)();engine.pathing_fine_placement(query,cells,out)
        if list(out)!=[r['result'],*r['after']]: raise ValueError('placement original/C endpoint differs')
    for r in rows:
        if r.get('event')=='forced-position-stop-end':
            if any(r['after'][k]!=[-1,-1] for k in ('taskHead','orderHead','group')) or r['after']['pose'][4:6]!=[0,0]:
                raise ValueError('placement Stop retained order/group/velocity')
    result=verify_motion(rows,engine,None)
    result.update(public_position_calls=25 if pathing_toggle else 30,position_queries=65 if pathing_toggle else 78,position_commits=5 if pathing_toggle else 6,placement_searches=7,
                  terrain_edits=4 if pathing_toggle else 34,placement_sha256=digest(rows,pathing_toggle),binary_sha256=fixture['binary_sha256'],
                  scope='Repeated public blocked ground placement; observed terrain footprints, first accepted same-level candidate and scalar commits match C. Other map cells, bridges, outside-map clipping and dynamic overlap admission remain separate.')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path);parser.add_argument('--repeat',type=Path)
    parser.add_argument('--fixture',type=Path,required=True);parser.add_argument('--engine-library',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True);parser.add_argument('--pathing-toggle',action='store_true');args=parser.parse_args()
    engine=ctypes.CDLL(str(args.engine_library.resolve()));configure(engine)
    engine.pathing_fine_placement.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint8),ctypes.POINTER(ctypes.c_uint32)]
    read=lambda p:[json.loads(l) for l in p.read_text().splitlines()]
    fixture=json.loads(args.fixture.read_text());result=verify(read(args.trace),engine,fixture,args.pathing_toggle)
    if args.repeat:
        other=verify(read(args.repeat),engine,fixture,args.pathing_toggle)
        if any(result[k]!=other[k] for k in ('placement_sha256','decision_sha256','velocity_sha256')):
            raise ValueError('placement repeat differs')
        result['repeated']=True
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':main()
