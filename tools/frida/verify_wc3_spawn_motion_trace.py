#!/usr/bin/env python3
"""Verify eight actual post-CreateUnit Move lifetimes, including reused movers."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure, words
from verify_wc3_spawn_trace import CASES, verify as verify_spawn


def journeys(rows):
    current=None
    result={c:[] for c in CASES}
    for row in rows:
        if row.get('event')=='position-marker' and row.get('value','').startswith('PATHPOSE case='):
            current=row['value'].split('=',1)[1]
        if current in result:result[current].append(row)
    return result


def digest(rows):
    selected=[]
    for case,life in journeys(rows).items():
        selected.append([case,[{k:v for k,v in r.items() if k not in ('ms','mover','fineObject')}
            for r in life if r.get('event') in ('arrival-evaluation','velocity-commit')]])
    return hashlib.sha256(json.dumps(selected,separators=(',',':')).encode()).hexdigest()


def verify(rows,engine,fixture):
    result=verify_spawn(rows,engine,fixture)
    meta=next(r for r in rows if r.get('event')=='metadata')
    end=next(r for r in rows if r.get('event')=='trace-end')
    if not meta.get('velocityEvents'):raise ValueError('spawn motion observer missing')
    for kind,n in [('arrival-input',8),('arrival-range',8),('arrival-evaluation',247),('velocity-commit',247)]:
        if sum(r.get('event')==kind for r in rows)!=n or end.get('counts',{}).get(kind)!=n:
            raise ValueError('spawn motion truncated '+kind)
    if digest(rows)!=fixture['journey_sha256']:raise ValueError('spawn motion sequence differs')
    for name,n in (('sqrt',1),('add',2),('multiply',2)):
        proc=getattr(engine,'pathing_'+name);proc.argtypes=[ctypes.c_uint32]*n;proc.restype=ctypes.c_uint32
    for case,life in journeys(rows).items():
        initial=[r for r in life if r.get('event')=='position-commit']
        ranges=[r for r in life if r.get('event')=='arrival-range']
        inputs=[r for r in life if r.get('event')=='arrival-input']
        arrival=[r for r in life if r.get('event')=='arrival-evaluation']
        commits=[r for r in life if r.get('event')=='velocity-commit']
        motion=[r for r in life if r.get('event')=='motion-decision']
        if len(initial)!=2 or len(inputs)!=1 or len(ranges)!=1 or len(commits)<2 or len(arrival)!=len(commits):
            raise ValueError('spawn motion lifetime incomplete')
        mover=initial[-1].get('mover')
        if inputs[0].get('kind')!='point' or inputs[0].get('rawcode')!=fixture['rawcode'] or inputs[0].get('worldRange')!=0:
            raise ValueError('spawn motion point command differs')
        if ranges[0].get('mover')!=mover or ranges[0].get('value')!=fixture['range'] or ranges[0].get('after')!=fixture['range']:
            raise ValueError('spawn motion range producer differs')
        pair=[r['event'] for r in life if r.get('event') in ('arrival-evaluation','velocity-commit')]
        if pair!=['arrival-evaluation','velocity-commit']*len(commits):
            raise ValueError('spawn motion arrival/commit order differs')
        sequence=[r['event'] for r in life if r.get('event') in ('arrival-evaluation','motion-decision','velocity-commit')]
        if len(motion)!=len(commits)-1 or sequence!=['arrival-evaluation','motion-decision','velocity-commit']*len(motion)+['arrival-evaluation','velocity-commit']:
            raise ValueError('spawn motion decision/commit order differs')
        previous=initial[-1]['after']
        for index,(a,c) in enumerate(zip(arrival,commits)):
            before,after=words(c.get('before'),8),words(c.get('after'),8)
            if a.get('mover')!=mover or c.get('mover')!=mover or before!=previous:
                raise ValueError('spawn motion identity/state chain differs')
            source,target=words(a.get('source'),2),words(a.get('destination'),2)
            inp=words([*source,*target,a.get('heading'),a.get('threshold'),a.get('flags')],7)
            if (a.get('storedPosition')!=before[2:4] or source!=after[2:4] or inp[4]!=before[7]
                    or inp[5]!=fixture['range'] or a.get('storedRange')!=fixture['range'] or inp[6]!=0):
                raise ValueError('spawn motion predicted arrival inputs differ')
            out=(ctypes.c_uint32*4)();engine.pathing_arrival((ctypes.c_uint32*7)(*inp),out)
            if list(out)[1:]!=[a.get('angle'),a.get('inRange'),a.get('result')]:
                raise ValueError('spawn motion C arrival differs')
            if a['result']!=int(index==len(commits)-1):raise ValueError('spawn motion natural terminal arrival missing')
            if before[6]!=fixture['speed']:raise ValueError('spawn motion authored speed differs')
            if index<len(motion):
                m=motion[index];x,y=before[4:6]
                speed=engine.pathing_sqrt(engine.pathing_add(engine.pathing_multiply(x,x),engine.pathing_multiply(y,y)))
                if (m.get('mover')!=mover or m.get('heading')!=before[7] or m.get('speed')!=speed
                        or m.get('nextHeading')!=c.get('heading') or c.get('speed')!=fixture['speed']):
                    raise ValueError('spawn motion decision producer differs')
            previous=after
        final=commits[-1]
        if (not any(w&0x7fffffff for w in final['before'][4:6]) or any(w&0x7fffffff for w in final['after'][4:6])
                or final['before'][2:4]==final['after'][2:4] or final['after'][2:4]==arrival[-1]['destination']):
            raise ValueError('spawn motion old-velocity terminal stop differs')
    result.update(journeys=8,arrival_evaluations=247,final_old_velocity_stops=8,journey_sha256=digest(rows),
        scope='Eight actual ground public spawn-to-point-Move lifetimes; exact scalar decisions, velocity/facing/clock integration, predicted arrival and final stops. Engine full original phase producer, other profiles and physical crowd policy remain separate.')
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
        if any(result[k]!=other[k] for k in ('spawn_sha256','decision_sha256','velocity_sha256','journey_sha256')):
            raise ValueError('spawn motion repeat differs')
        result['repeated']=True
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':main()
