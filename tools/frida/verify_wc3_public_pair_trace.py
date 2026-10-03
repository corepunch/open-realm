#!/usr/bin/env python3
"""Verify a public two-unit group through its natural decision/commit lifetime."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary

PHASES = ('pair-group-phase-begin', 'pair-group-phase-end')


def canonical(rows):
    movers = []
    for row in rows:
        if row.get('event') == 'pair-group-phase-begin':
            for member in row['members']:
                if member['mover'] not in movers: movers.append(member['mover'])
    result = []
    for row in rows:
        event = row.get('event')
        if event in PHASES:
            clean = {k: v for k, v in row.items() if k not in ('ms', 'group', 'identity', 'members')}
            clean['members'] = [dict(member=movers.index(m['mover']),
                row=[*m['row'][:5], movers.index(m['mover']), *m['row'][6:]],
                pose=m['pose'], moverFlags=m['moverFlags']) for m in row['members']]
            result.append(clean)
        elif event in ('motion-decision', 'velocity-commit', 'arrival-evaluation'):
            clean = {k: v for k, v in row.items() if k not in ('ms', 'mover', 'fineObject')}
            clean['member'] = movers.index(row['mover'])
            result.append(clean)
    return result


def verify(rows, engine, fixture, group_check=None):
    metadata = [r for r in rows if r.get('event') == 'metadata']
    ending = [r for r in rows if r.get('event') == 'trace-end']
    if len(metadata) != 1 or {k:v for k,v in metadata[0].items() if k not in ('event','pid')} != fixture['metadata']:
        raise ValueError('public pair provenance differs')
    if len(ending) != 1 or not ending[0].get('installed') or any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
        raise ValueError('public pair capture incomplete/failed')
    for event in ('velocity-commit','motion-decision','arrival-evaluation','clock-source-begin','clock-source-end',
                  'clock-advance-begin','clock-advance-end','clock-owner-begin','clock-owner-end'):
        if sum(r.get('event') == event for r in rows) != ending[0].get('counts',{}).get(event):
            raise ValueError('public pair observer count differs: '+event)
    motion = verify_motion(rows, engine, None)
    clocks = verify_primary(rows, engine, fixture)
    motion.update(clocks)
    motion.update((group_check or verify_pair)(rows, fixture))
    return motion


def verify_pair(rows, fixture):
    return verify_group(rows,fixture,dict(commits=[60,55],passes=60,marker='pair-marker',prefix='PATHPAIR',
        scope='Public two-member point group: complete repeated original phases and arithmetic. Whole-engine parity is a separate normal-frame regression; supplied scene geometry remains explicit.'))


def verify_group(rows, fixture, extent):
    actual = canonical(rows)
    if actual != fixture['phases']:
        raise ValueError('public pair phase/member/pose words differ')
    commits = [r for r in actual if r['event'] == 'velocity-commit']
    words = [[r['member'],r['clock'][0],*[r['after'][i] for i in (2,3,4,5,7)]] for r in commits]
    if words != fixture['engine_motion'] or [sum(r['member']==i for r in commits) for i in range(len(extent['commits']))] != extent['commits']:
        raise ValueError('public pair commit extent/engine words differ')
    # Check producer/phase ordering independently of the frozen expected sequence.
    phases = [r for r in actual if r['event'] in PHASES]
    if len(phases) != 4*extent['passes']:
        raise ValueError('public pair owner phase count differs')
    for i in range(0,len(phases),4):
        batch=phases[i:i+4]
        if [(r['event'],r['phase']) for r in batch] != [(PHASES[0],'decide'),(PHASES[1],'decide'),(PHASES[0],'commit'),(PHASES[1],'commit')]:
            raise ValueError('public pair decision/commit phase order differs')
        if [(m['member'],m['pose']) for m in batch[0]['members']] != [(m['member'],m['pose']) for m in batch[1]['members']]:
            raise ValueError('public pair decision committed a pose before all decisions')
    marks=[r['value'] for r in rows if r.get('event')==extent['marker']]
    if marks != fixture['markers']: raise ValueError('public pair sampled words differ')
    if marks.count(extent['prefix']+' tick=10 accepted=1') != 1 or len(marks) != 1+300*len(extent['commits']):
        raise ValueError('public pair timer admission/sample count differs')
    for i in range(len(extent['commits'])):
        last=[s for s in marks if s.startswith(f"{extent['prefix']} tick=300 member={i} ")]
        if len(last)!=1 or not last[0].endswith(' order=0'):
            raise ValueError('public pair natural final order did not finish')
    return dict(group_owner_passes=extent['passes'],
                public_group_members=len(extent['commits']),natural_arrivals=len(extent['commits']),
                member_commits=extent['commits'],
                phase_sha256=hashlib.sha256(json.dumps(actual,separators=(',',':')).encode()).hexdigest(),
                scope=extent['scope'])


def main(group_check=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace',type=Path);p.add_argument('--repeat',type=Path)
    p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--report',required=True,type=Path);a=p.parse_args()
    fixture=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine)
    read=lambda path:[json.loads(s) for s in path.read_text().splitlines()]
    result=verify(read(a.trace),engine,fixture,group_check)
    if a.repeat:
        if verify(read(a.repeat),engine,fixture,group_check)!=result:raise ValueError('public pair repeat differs')
        result['repeated']=True
    a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
