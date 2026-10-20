#!/usr/bin/env python3
"""Verify forced-position Stop, scalar placement and repeated public order retirement."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re
from verify_wc3_arrival_trace import configure, words
from verify_wc3_motion_trace import verify as verify_motion

KINDS = ('position-marker', 'position-native', 'position-query', 'position-commit',
         'forced-position-stop-begin', 'forced-position-stop-end')
CASES = ('moving_same', 'moving_fractional', 'idle_same', 'patrol_fractional')


def normalized(rows):
    return [{k:v for k,v in row.items() if k not in ('ms','unit','mover','handle')}
            for row in rows if row.get('event') in KINDS]


def digest(rows):
    return hashlib.sha256(json.dumps(normalized(rows),separators=(',',':')).encode()).hexdigest()


def verify(rows, engine, fixture):
    meta = [r for r in rows if r.get('event') == 'metadata']
    end = [r for r in rows if r.get('event') == 'trace-end']
    if any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
        raise ValueError('forced position observer error')
    if len(meta) != 1 or meta[0].get('sha256') != fixture['binary_sha256'] or meta[0].get('source_sha256') != fixture['source_sha256']:
        raise ValueError('forced position source/target differs')
    if not all(meta[0].get(k) for k in ('owned','motionEvents','velocityEvents','taskEvents')) or len(end) != 1 or not end[0].get('installed'):
        raise ValueError('forced position observer options/completion missing')
    for kind,count in (('position-native',28),('position-query',60),('position-commit',4),
                       ('forced-position-stop-begin',4),('forced-position-stop-end',4)):
        if sum(r.get('event') == kind for r in rows) != count or end[0].get('counts',{}).get(kind) != count:
            raise ValueError('forced position missing/truncated '+kind)
    if digest(rows) != fixture['position_sha256']:
        raise ValueError('forced position sequence differs')
    marks = [re.fullmatch(r'PATHTRACE tick=(\d+) label=(\w+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)',r.get('value',''))
             for r in rows if r.get('event') == 'marker']
    if any(m is None for m in marks): raise ValueError('forced position malformed sample')
    labels = [m[2] for m in marks]
    if any(labels.count(k) != 1 for k in ('start_forced_position','order_accepted','forced_move_reissued','forced_patrol_reissued','complete')):
        raise ValueError('forced position admission/completion differs')
    if any(k.endswith('rejected') for k in labels) or [int(m[1]) for m in marks if m[2] == 'sample'] != list(range(1,301)):
        raise ValueError('forced position samples/admission incomplete')
    active = None; seen = []
    for r in rows:
        if r.get('event') not in KINDS: continue
        if r['event'] == 'position-marker':
            m = re.fullmatch(r'PATHPOSE (case|done)=([a-z0-9_]+)',r.get('value',''))
            if not m: raise ValueError('forced position malformed case marker')
            if m[1] == 'case':
                if active is not None: raise ValueError('forced position nested case')
                active=m[2]; seen.append(active)
            else:
                if active != m[2]: raise ValueError('forced position case completion differs')
                active=None
        elif r.get('case') != active or active is None:
            raise ValueError('forced position event outside case')
    if active is not None or seen != list(CASES): raise ValueError('forced position producer order differs')
    natives = [r for r in rows if r.get('event') == 'position-native']
    actors = {(r.get('unit'),r.get('handle'),r.get('rawcode')) for r in natives}
    if len(actors) != 1 or next(iter(actors))[0] in (None,'0x0') or next(iter(actors))[2] != fixture['rawcode']:
        raise ValueError('forced position resolved actor differs')
    sources = [r for r in rows if r.get('event') in ('position-query','position-commit')]
    if len({r.get('mover') for r in sources}) != 1 or any(r.get('unit') != next(iter(actors))[0] for r in sources):
        raise ValueError('forced position bridge actor differs')
    engine.pathing_position_bridge.argtypes = [ctypes.POINTER(ctypes.c_uint32)]*2
    for r in sources:
        before,after = words(r.get('before'),8),words(r.get('after'),8)
        clock,origin = words(r.get('clock'),3),words(r.get('origin'),2)
        point = words(r.get('input') if r['event'] == 'position-commit' else r.get('output'),3)
        inp = before+clock+origin+point[:2]; arg=(ctypes.c_uint32*15)(*inp); out=(ctypes.c_uint32*12)()
        engine.pathing_position_bridge(arg,out)
        if list(arg) != inp: raise ValueError('forced position C changed input')
        if r['event'] == 'position-query':
            if before != after or list(out)[10:12] != point[:2] or point[2] != 0:
                raise ValueError('forced position predicted query differs')
        elif list(out)[:8] != after or before[4:] != after[4:] or r.get('notify') != 1:
            raise ValueError('forced position scalar placement differs')
    for case in CASES:
        calls = [r for r in natives if r['case'] == case]
        if [r['name'] for r in calls] != ['GetUnitX','GetUnitY','GetUnitX','GetUnitY','SetUnitPosition','GetUnitX','GetUnitY']:
            raise ValueError('forced position native order differs')
        start = [r for r in rows if r.get('event') == 'forced-position-stop-begin' and r['case'] == case]
        stop = [r for r in rows if r.get('event') == 'forced-position-stop-end' and r['case'] == case]
        commits = [r for r in sources if r['event'] == 'position-commit' and r['case'] == case]
        if len(start) != 1 or len(stop) != 1 or len(commits) != 1 or start[0].get('flags') != 1 or stop[0].get('flags') != 1:
            raise ValueError('forced position Stop traversal differs')
        native,commit = calls[4],commits[0]
        queries = [r for r in sources if r['event'] == 'position-query' and r['case'] == case and r.get('native') in ('GetUnitX','GetUnitY')]
        getters = [r for r in calls if r['name'] in ('GetUnitX','GetUnitY')]
        if len(queries) != 6 or any(r['native'] != c['name'] or r['output'][0 if c['name'] == 'GetUnitX' else 1] != c.get('output') for r,c in zip(queries,getters)):
            raise ValueError('forced position public getter differs')
        if not rows.index(start[0]) < rows.index(stop[0]) < rows.index(commit) < rows.index(native):
            raise ValueError('forced position Stop/placement event order differs')
        if native['before'] != start[0]['before'] or start[0]['before'] != stop[0]['before'] or stop[0]['after']['pose'] != commit['before'] or native['after']['pose'] != commit['after'] or words(native.get('input'),2) != commit['input'][:2]:
            raise ValueError('forced position Stop/placement state order differs')
        before,after = words(start[0]['before']['pose'],8),words(stop[0]['after']['pose'],8)
        for state in (stop[0]['after'],native['after']):
            if any(state.get(k) != [-1,-1] for k in ('taskHead','orderHead','group')) or state['pose'][4:6] != [0,0]:
                raise ValueError('forced position retained task/order/group/velocity')
        if case != 'idle_same' and any(start[0]['before'][k] == [-1,-1] for k in ('taskHead','orderHead','group')):
            raise ValueError('forced position active source missing')
        if before[6:] != after[6:]: raise ValueError('forced position Stop changed cap/facing')
        state=(ctypes.c_uint32*11)(*before[2:6],*before[:2],*commit['clock'],0,0)
        engine.pathing_integrate(state)
        if list(state)[:2] != after[2:4] or list(state)[4:6] != after[:2]:
            raise ValueError('forced position Stop old-velocity integration differs')
        m = next(m for m in marks if m[2] == 'forced_after_'+case)
        if int(m[5]) != 0: raise ValueError('forced position public current order retained')
        samples = [m for m in marks if m[2] == 'sample' and int(m[1]) > int(next(m for m in marks if m[2] == 'forced_after_patrol_fractional')[1])]
        if case == 'patrol_fractional' and any(m[5] != '0' or m.group(3,4) != next(m for m in marks if m[2]=='forced_after_patrol_fractional').group(3,4) for m in samples):
            raise ValueError('forced position continued moving after placement')
    result = verify_motion(rows,engine,None)
    result.update(binary_sha256=fixture['binary_sha256'],public_position_calls=28,position_queries=60,
                  position_commits=4,stop_integrations=4,retired_orders=4,position_sha256=digest(rows),
                  scope='Repeated ordinary Move/Patrol/idle SetUnitPosition Stop then scalar placement. Public blocked/overlapping placement and other forced writers remain separate.')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path); parser.add_argument('--repeat',type=Path)
    parser.add_argument('--fixture',type=Path,required=True); parser.add_argument('--engine-library',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args(); engine=ctypes.CDLL(str(args.engine_library.resolve())); configure(engine)
    fixture=json.loads(args.fixture.read_text()); read=lambda p:[json.loads(l) for l in p.read_text().splitlines()]
    result=verify(read(args.trace),engine,fixture)
    if args.repeat:
        other=verify(read(args.repeat),engine,fixture)
        if any(result[k] != other[k] for k in ('position_sha256','decision_sha256','velocity_sha256')):
            raise ValueError('forced position repeat differs')
        result['repeated']=True
    args.report.write_text(json.dumps(result,indent=2)+'\n'); print(json.dumps(result,indent=2))

if __name__=='__main__': main()
