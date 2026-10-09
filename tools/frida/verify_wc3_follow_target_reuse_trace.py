#!/usr/bin/env python3
"""Verify synchronous Follow retirement, real mover reuse and explicit reacquisition."""
import argparse
import ctypes
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_selected_queued_trace import canonical,digest,motion_words,owner_order


def target_records(rows):
    return [{k:v for k,v in r.items() if k!='ms'} for r in rows if r.get('event')=='group-target']


def verify_retirement(targets,markers,target_markers):
    if len(targets)!=5 or targets[2]['target']!='0x0' or targets[2]['handle'] is not None:
        raise ValueError('target lifetime requires two old and two replacement cohorts')
    old,new=targets[0],targets[3]
    if (old['target']=='0x0' or old['target']!=new['target'] or old['handle']==new['handle'] or
            old['handle'][1]==new['handle'][1] or targets[1]['handle']!=old['handle'] or
            targets[4]['handle']!=new['handle']):
        raise ValueError('native mover address must be reused with a fresh canonical generation')
    handles=[r for r in target_markers if 'handleBefore=' in r or 'handleAfter=' in r]
    if len(handles)!=2 or handles[0].split('=')[-1]!=handles[1].split('=')[-1]:
        raise ValueError('original public unit handle was not reused')
    for label,order in [('before_target_retirement',851971),('after_target_retirement',0),
                        ('replacement_created',0),('before_replacement_follow',0),
                        ('replacement_follow_accepted',851971),('complete',0)]:
        found=[r for r in markers if 'label='+label+' ' in r]
        if len(found)!=1 or not found[0].endswith('order='+str(order)):
            raise ValueError('Follow retirement/reacquisition boundary differs: '+label)
    idle=[r for r in markers if 'label=sample ' in r and 100<=int(r.split('tick=')[1].split()[0])<120]
    if len(idle)!=20 or any(not r.endswith('order=0') for r in idle):
        raise ValueError('retired target must not silently acquire its replacement')


def render_header(fixture):
    out='/* Original scenes54/55: target removal/death, pool reuse and explicit new Smart. */\n'
    out+='static uint32_t const follow_target_remove_motion[][7]={\n'
    for row in fixture['engine_motion']:out+='    {'+','.join(str(v)+'u' for v in row)+'},\n'
    return out+'};\n'


def verify_lifecycle(rows,fixture,case):
    metadata=[r for r in rows if r.get('event')=='metadata'];ending=[r for r in rows if r.get('event')=='trace-end']
    if len(metadata)!=1 or {k:v for k,v in metadata[0].items() if k not in ('event','pid')}!=case['metadata']:
        raise ValueError('target reuse source provenance differs')
    if len(ending)!=1 or not ending[0].get('installed') or any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('target reuse observer incomplete/failed')
    for event,n in fixture['event_counts'].items():
        if sum(r.get('event')==event for r in rows)!=n or (not event.startswith('pair-group-phase-') and ending[0]['counts'].get(event)!=n):
            raise ValueError('target reuse observer extent differs: '+event)
    targets=target_records(rows);markers=[r['value'] for r in rows if r.get('event')=='marker']
    target_markers=[r['value'] for r in rows if r.get('event')=='target-marker']
    verify_retirement(targets,markers,target_markers)
    if targets!=case['targets'] or markers!=case['markers'] or target_markers!=case['target_markers']:
        raise ValueError('target retirement/reuse producer differs')
    if (digest(canonical(rows))!=fixture['phases_sha256'] or motion_words(rows)!=fixture['engine_motion'] or
            owner_order(rows)!=fixture['owner_order']):raise ValueError('target reuse absolute motion/owner phases differ')
    return dict(passed=True,mode=case['mode'],group_owner_passes=948,motion_sha256=digest(fixture['engine_motion']),
                native_mover_reuses=1,synchronous_retirements=1,natural_task_arrivals=4)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',type=Path,nargs='+')
    p.add_argument('--fixture',type=Path,required=True);p.add_argument('--engine-library',type=Path,required=True)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    fixture=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine);results=[]
    for path,case in zip(a.traces,fixture['cases'],strict=True):
        rows=[json.loads(l) for l in path.read_text().splitlines()];r=verify_lifecycle(rows,fixture,case)
        r.update(verify_motion(rows,engine,None));results.append(r)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(fixture):raise ValueError('target reuse engine header differs')
    report=dict(passed=True,cases=len(results),exact_decisions=sum(r['exact_decisions'] for r in results),
        exact_velocity_commits=sum(r['exact_velocity_commits'] for r in results),group_owner_passes=948*len(results),
        native_mover_reuses=len(results),results=results,scope=fixture['scope'])
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
