#!/usr/bin/env python3
"""Replay public Stop's bounded embedded-unit recovery, including queryzero and failure."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion

KINDS=('terrain-native','position-marker','position-native','position-query','pathing-toggle',
       'movement-mask-publication','stop-recovery-begin','stop-recovery-end',
       'placement-search-begin','placement-search-end')


def digest(rows):
    normalized=[{k:v for k,v in r.items() if k not in ('ms','unit','mover','handle','recovery')}
                for r in rows if r.get('event') in KINDS]
    return hashlib.sha256(json.dumps(normalized,separators=(',',':')).encode()).hexdigest()


def verify(rows,engine,fixture):
    motion=verify_motion(rows,engine,None)
    meta=[r for r in rows if r.get('event')=='metadata']
    end=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or meta[0].get('sha256')!=fixture['binary_sha256'] or meta[0].get('source_sha256')!=fixture['source_sha256']:
        raise ValueError('Stop recovery target/source differs')
    if len(end)!=1 or not all(meta[0].get(k) for k in ('owned','motionEvents','velocityEvents','profileEvents','taskEvents')):
        raise ValueError('Stop recovery observer options/completion missing')
    if digest(rows)!=fixture['recovery_sha256']: raise ValueError('Stop recovery sequence differs')
    for kind,count in (('stop-recovery-begin',8),('stop-recovery-end',8),('placement-search-begin',3),('placement-search-end',3),('position-native',24),('position-query',24)):
        if sum(r.get('event')==kind for r in rows)!=count or end[0].get('counts',{}).get(kind)!=count:
            raise ValueError('Stop recovery missing/truncated '+kind)
    cases=tuple('stop_recovery_'+str(t) for t in (20,30,40,50))
    markers=[r['value'] for r in rows if r.get('event')=='position-marker']
    if markers!=[s for c in cases for s in ('PATHPOSE case='+c,'PATHPOSE done='+c)]:
        raise ValueError('Stop recovery script brackets differ')
    samples=[r['value'] for r in rows if r.get('event')=='marker' and 'label=sample ' in r.get('value','')]
    if [int(re.search(r'tick=(\d+)',r)[1]) for r in samples]!=list(range(1,301)):
        raise ValueError('Stop recovery public samples incomplete')
    if sum(r.get('event')=='marker' and 'label=complete ' in r.get('value','') for r in rows)!=1:
        raise ValueError('Stop recovery public completion missing')
    labels=[re.search(r'label=(\w+)',r['value'])[1] for r in rows if r.get('event')=='marker']
    if labels.count('stop_recovery_accepted')!=4 or 'stop_recovery_rejected' in labels:
        raise ValueError('Stop recovery public order rejected or duplicated')
    edits=[(r.get('x'),r.get('y'),r.get('pathingType'),r.get('passable')) for r in rows if r.get('event')=='terrain-native']
    expected=[(-2000+32*x,-560,1,0) for x in range(4)]
    expected += [(-1936+32*x,-560+32*y,1,0) for x in range(-5,6) for y in range(-5,6)]
    if edits!=expected: raise ValueError('Stop recovery terrain edits differ')
    begins=[r for r in rows if r.get('event')=='stop-recovery-begin']
    finishes=[r for r in rows if r.get('event')=='stop-recovery-end']
    if [r.get('case') for r in begins]!=[c for c in cases for _ in range(2)] or [r.get('case') for r in finishes]!=[r['case'] for r in begins]:
        raise ValueError('Stop recovery case ownership differs')
    if len({r.get('mover') for r in begins+finishes})!=1 or begins[0].get('mover') in (None,'0x0'):
        raise ValueError('Stop recovery mover differs')
    for a,b in zip(begins,finishes):
        if any(a.get(k)!=b.get(k) for k in ('mover','mask','limit','callback','context','policy','before')):
            raise ValueError('Stop recovery begin/end bracket differs')
        mask=0 if a['case']=='stop_recovery_40' else 0x02000002
        if (a['mask'],a['limit'],a['callback'],a['context'],a['policy'])!=(mask,5,'0x654060',6,2):
            raise ValueError('Stop recovery arguments differ')
        if a['before'][4:6]!=[0,0] or b['after'][4:6]!=[0,0] or a['before'][:2]+a['before'][6:]!=b['after'][:2]+b['after'][6:]:
            raise ValueError('Stop recovery changed clock/velocity/cap/facing')
    if [r.get('result') for r in finishes]!=[0,0,1,0,0,0,1,1]:
        raise ValueError('Stop recovery helper result is not blocked-source status')
    searches=[r for r in rows if r.get('event')=='placement-search-end']
    if [r['case'] for r in searches]!=[cases[1],cases[3],cases[3]]:
        raise ValueError('Stop recovery searches not confined to blocked sources')
    if [r['result'] for r in searches]!=[1,0,0]: raise ValueError('Stop recovery bounded outcomes differ')
    engine.pathing_fine_placement.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint8),ctypes.POINTER(ctypes.c_uint32)]
    engine.pathing_fine_pose_write.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint32)]
    for r in searches:
        if (r['policy'],r['mask'],r['radius'],r['limit'],r['callback'],r['context'],r['integerResult'])!=(2,0x02000002,0x3f780000,5,'0x654060',6,0):
            raise ValueError('Stop recovery placement arguments differ')
        if r['savedMode']!=r['restoredMode'] or r['rect']!=[r['before'][1],r['before'][0]]*2:
            raise ValueError('Stop recovery placement mode/rectangle differs')
        cells=(ctypes.c_uint8*(256*128))()
        if r['case']==cases[1]:
            for x in range(161,165): cells[78*256+x]=2
        else:
            for y in range(73,84):
                for x in range(158,169): cells[y*256+x]=2
        query=(ctypes.c_uint32*8)(256,128,*r['before'],5,1,r['mask'],0)
        output=(ctypes.c_uint32*3)();engine.pathing_fine_placement(query,cells,output)
        if list(output)!=[r['result'],*r['after']]: raise ValueError('Stop recovery original/C placement differs')
        if any(v['mask']!=r['mask'] or v['cls']!=1 for v in r['visits']):
            raise ValueError('Stop recovery footprint class/mask differs')
        for v in r['visits']:
            legal=all(0<=x<256 and 0<=y<128 and not cells[y*256+x]&2
                      for y in range(v['point'][1]-1,v['point'][1]+1)
                      for x in range(v['point'][0]-1,v['point'][0]+1))
            if bool(v['result'])!=legal: raise ValueError('Stop recovery cell admission differs')
    admitted=finishes[2]
    if admitted['before'][2:4]!=searches[0]['before'] or admitted['after'][2:4]!=searches[0]['after']:
        raise ValueError('Stop recovery search did not reach mover pose')
    scalar=lambda x:ctypes.c_uint32.from_buffer_copy(ctypes.c_float(x)).value
    output=(ctypes.c_uint32*4)()
    engine.pathing_fine_pose_write((ctypes.c_uint32*6)(*admitted['before'][2:4],scalar(-7168),scalar(-3072),*searches[0]['after']),output)
    if list(output)!=[*admitted['after'][2:4],scalar(-1936),scalar(-592)]:
        raise ValueError('Stop recovery fine/world commit differs')
    if any(r['before']!=r['after'] for i,r in enumerate(finishes) if i!=2):
        raise ValueError('Clear, disabled, settled or exhausted recovery moved unit')
    for r in rows:
        if r.get('event')!='position-query': continue
        state=(ctypes.c_uint32*15)(*r['before'],*r['clock'],*r['origin'],*r['output'][:2])
        out=(ctypes.c_uint32*12)(); engine.pathing_position_bridge(state,out)
        if r['before']!=r['after'] or r['output'][2]!=0 or list(out)[10:]!=r['output'][:2]:
            raise ValueError('Stop recovery public scalar query differs')
    natives=[r for r in rows if r.get('event')=='position-native']
    queries=[r for r in rows if r.get('event')=='position-query']
    if len({(r.get('unit'),r.get('handle'),r.get('rawcode')) for r in natives})!=1 or natives[0].get('rawcode')!=1751543663:
        raise ValueError('Stop recovery public unit differs')
    if any(n.get('name')!=q.get('native') or n.get('unit')!=q.get('unit') or
           n.get('output')!=q['output'][0 if n['name']=='GetUnitX' else 1] for n,q in zip(natives,queries)):
        raise ValueError('Stop recovery native getter output differs')
    actor=begins[0]['mover']
    if any(r.get('mover')!=actor for r in rows if r.get('event')=='position-query'):
        raise ValueError('Stop recovery public getter mover differs')
    motion.update(recovery_calls=8,placement_searches=3,recovered_points=1,exhausted_searches=2,
                  public_getters=24,recovery_sha256=digest(rows),binary_sha256=fixture['binary_sha256'],
                  scope='Four public Stop controls: clear/embedded/disabled/exhausted. Complete original queries, bounded search outputs and recovered native/world point match production C; full engine cadence and arbitrary actor/terrain domains remain open.')
    return motion


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('trace',type=Path)
    parser.add_argument('--repeat',type=Path);parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--engine-library',type=Path,required=True);parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();engine=ctypes.CDLL(str(args.engine_library.resolve()));configure(engine)
    read=lambda p:[json.loads(s) for s in p.read_text().splitlines()];fixture=json.loads(args.fixture.read_text())
    result=verify(read(args.trace),engine,fixture)
    if args.repeat:
        other=verify(read(args.repeat),engine,fixture)
        if any(result[k]!=other[k] for k in ('recovery_sha256','decision_sha256','velocity_sha256')):
            raise ValueError('Stop recovery repeat differs')
        result['repeated']=True
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':main()
