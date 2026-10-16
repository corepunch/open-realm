#!/usr/bin/env python3
"""Verify captain range departure; engine scope stops before the second shared generation."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_captain_approach_trace import births, verify_capture as verify_approach, render_header
from verify_wc3_captain_shared_trace import shared_state


def membership_state(rows):
    movers=births(rows)
    units={r['unit'] for r in rows if r.get('event')=='captain-authored-follow-range'}
    publications=[(i,r) for i,r in enumerate(rows) if r.get('event')=='movement-mask-publication'
                  and r['mover'] in movers][::2]
    unit_birth={}
    for birth,(start,publication) in enumerate(publications):
        end=publications[birth+1][0] if birth+1<len(publications) else next(
            i for i,r in enumerate(rows) if r.get('event')=='captain-native-begin')
        candidates={r['unit'] for r in rows[start:end] if r.get('event')=='task-prepend' and r['unit'] in units}
        if len(candidates)!=1:raise ValueError('captain unit birth cannot be tied to original task admission')
        unit=candidates.pop()
        if unit in unit_birth:raise ValueError('captain logical member aliases another birth')
        rawcodes={r['rawcode'] for r in rows if r.get('event')=='captain-authored-follow-range' and r['unit']==unit}
        if rawcodes!={publication['rawcode']}:raise ValueError('captain logical member and mover rawcodes differ')
        unit_birth[unit]=birth
    prepared=[unit_birth[r['unit']] for r in rows if r.get('event')=='captain-prepare-begin']
    if prepared!=list(range(1,13))+[0]+list(range(1,13))+[0]:
        raise ValueError('captain logical roster order differs from physical births')
    counters=[]; callbacks=[]; reissues=[]
    captains={r['captain'] for r in rows if r.get('event')=='captain-member-reissue'}
    if len(captains)!=1:raise ValueError('captain range owner extent differs')
    for r in rows:
        event=r.get('event')
        if event not in ('captain-membership-counter','captain-membership-begin',
                         'captain-membership-end','captain-member-reissue'):continue
        if r['captain'] not in captains or r['unit'] not in unit_birth:
            raise ValueError('captain range event references a foreign owner/member')
        unit=unit_birth[r['unit']]
        if event=='captain-membership-counter':
            counters.append([r['clock'],r['counter'],unit,r['mode'],r['delta'],r['result'],r['counts'],r['countsAfter']])
        elif event=='captain-member-reissue':
            if r['target']!=r['captain']:raise ValueError('captain private reissue target differs')
            reissues.append([r['clock'],r['counter'],unit,r['order'],r['point']])
        else:
            callbacks.append([event,r['name'],r['clock'],r['counter'],unit,r['mode'],r['packet'][2],
                              r['counts'],r.get('countsAfter')])
    start=next(i for i,r in enumerate(rows) if r.get('event')=='captain-member-reissue' and r['clock'][0]==0x413fffff)
    end=next(i for i in range(start+1,len(rows)) if rows[i].get('event')=='captain-membership-end')
    stop=next(r for r in rows[start:end] if r.get('event')=='mover-stop' and r['mover']==movers[12])
    bindings=[[r['before'],r['after']] for r in rows[start:end]
              if r.get('event')=='move-group-bind' and r['mover']==movers[12]]
    arrival=next(r for r in rows[end:] if r.get('event')=='arrival-evaluation' and r['mover']==movers[12])
    return dict(counters=counters,callbacks=callbacks,reissues=reissues,
                stop={k:stop[k] for k in ('before','after','requestedBefore','requestedAfter','caller','stack')},
                bindings=bindings,arrival={k:arrival[k] for k in ('source','destination','threshold','footprint','storedRange')})


def verify_contract(f):
    if f['whole_engine_parity'] is not False or f['second_shared_generation_remains_open'] is not True or \
            f['engine_prefix_commits']!=4867 or f['engine_end_msec']!=15000:
        raise ValueError('captain departure engine scope exceeds the verified prefix')
    s=f['membership_state']; counters=s['counters']; callbacks=s['callbacks']; reissues=s['reissues']
    if len(counters)!=34 or len(callbacks)!=38 or len(reissues)!=15:
        raise ValueError('captain departure event extent differs')
    for clock,counter,unit,mode,delta,result,before,after in counters:
        expected=list(before)
        if mode not in (0,1) or delta not in (-1,1) or result!=1:
            raise ValueError('captain membership counter domain differs')
        expected[3 if mode else 4]+=delta
        if after!=expected:raise ValueError('captain inner/outer counter changed the wrong field')
    departures=[r for r in counters if r[4]==-1]
    if [[r[0][0],r[1],r[2],r[3],r[6][3:],r[7][3:]] for r in departures]!=[
            [0x403ffffc,1124,8,1,[12,13,0],[11,13,0]],
            [0x407ffffc,1157,12,0,[12,13,0],[12,12,0]],
            [0x411fffff,1357,12,1,[13,13,0],[12,13,0]],
            [0x413fffff,1424,12,0,[12,13,0],[12,12,0]]]:
        raise ValueError('captain departure identity/deadline/counter differs')
    if [r[2] for r in reissues[:13]]!=[0]+list(range(12,0,-1)) or \
            [(r[0][0],r[1],r[2]) for r in reissues[13:]]!=[(0x407ffffc,1157,12),(0x413fffff,1424,12)] or \
            any(r[3:]!=[851986,[0,0]] for r in reissues):
        raise ValueError('captain private Move reissue differs')
    stop=s['stop']
    if stop['caller']!='0x5ca8b' or stop['after'][:2]!=[0x413fffff,0] or stop['after'][4:6]!=[0,0] or \
            stop['requestedAfter']!=[0,0] or not all(r in stop['stack'] for r in ('0x9d8980','0x9d8ff4','0x9d4b51')):
        raise ValueError('captain reissue did not integrate and stop through the range callback')
    if s['bindings']!=[[[1843,2123],[-1,-1]],[[-1,-1],[-1,-1]],[[-1,-1],[-1,-1]],[[-1,-1],[1820,2170]]] or \
            s['arrival']['storedRange']!=0x409b0000 or s['arrival']['footprint']!=0x3f780000:
        raise ValueError('captain private owner generation/range differs')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',required=True,type=Path);a=p.parse_args()
    f=json.loads(a.fixture.read_text());verify_contract(f)
    approach_path=a.fixture.parent/f['approach_fixture']
    shared_path=a.fixture.parent/f['shared_fixture']
    for path,key in ((approach_path,'approach_sha256'),(shared_path,'shared_sha256')):
        if hashlib.sha256(path.read_bytes()).hexdigest()!=f[key]:raise ValueError('captain reference fixture changed')
    approach=json.loads(approach_path.read_text());shared=json.loads(shared_path.read_text())
    engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine);results=[]
    for path,case in zip(a.traces,f['cases'],strict=True):
        if hashlib.sha256(path.read_bytes()).hexdigest()!=case['trace_sha256'] or path.stat().st_size!=case['bytes']:
            raise ValueError('captain departure capture hash/extent differs')
        rows=[json.loads(l) for l in path.read_text().splitlines()]
        result=verify_approach(rows,approach,case,engine)
        if membership_state(rows)!=f['membership_state'] or shared_state(rows)!=shared['shared_state']:
            raise ValueError('captain departure literal membership/shared state differs')
        result.update(engine_prefix_commits=4867,membership_counter_changes=34,private_reissues=15)
        results.append(result)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(approach):
        raise ValueError('captain departure complete literal motion reference differs')
    report=dict(passed=True,cases=len(results),results=results,scope=f['scope'],whole_engine_parity=False,
                engine_prefix_commits=4867,engine_end_msec=15000)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':main()
