#!/usr/bin/env python3
"""Check the selected-unit point producer and both independently timed native journeys."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_public_pair_trace import canonical

PRODUCER = {'player-order-variant', 'player-point-action-begin', 'player-point-action-end',
            'player-point-attach', 'player-point-target-admit-begin',
            'player-point-target-admit-end', 'player-order-publish'}


def digest(value):
    return hashlib.sha256(json.dumps(value, separators=(',', ':')).encode()).hexdigest()


def producer(rows):
    units = [r['unit'] for r in rows if r.get('event') == 'player-point-attach']
    requests = []
    result = []
    def request(pointer):
        if pointer == '0x0': return -1
        if pointer not in requests: requests.append(pointer)
        return requests.index(pointer)
    for row in rows:
        if row.get('event') not in PRODUCER: continue
        clean = {k:v for k,v in row.items() if k not in ('ms','action','unit','order','words','requests','row')}
        if 'unit' in row: clean['unit'] = units.index(row['unit'])
        if 'requests' in row: clean['requests'] = [request(p) for p in row['requests']]
        if row['event'] == 'player-order-variant':
            clean.update(player=(row['words'][5] >> 8) & 255, flags=row['words'][6] & 65535,
                         order=row['words'][7], point=row['words'][10:12], target=row['words'][12:14])
        elif row['event'].startswith('player-point-action'): clean['order'] = row['order']
        if row.get('row') is not None:
            clean['row'] = [units.index(row['unit']), *row['row'][1:7], request(hex(row['row'][7])), row['row'][8]]
        result.append(clean)
    return result


def verify_producer(rows, expected, flags=8):
    actual = producer(rows)
    if actual != expected: raise ValueError('selected point producer/flags/member order differs')
    actions = [r for r in actual if r['event'] == 'player-point-action-begin']
    if len(actions) != 1 or (actions[0]['entry'], actions[0]['player'], actions[0]['flags'], actions[0]['order']) != (0x6b9f70,3,flags,851986):
        raise ValueError('selected point witness is not ordinary ground Move')
    attaches = [i for i,r in enumerate(actual) if r['event'] == 'player-point-attach']
    admits = [i for i,r in enumerate(actual) if r['event'] == 'player-point-target-admit-begin']
    publishes = [r for r in actual if r['event'] == 'player-order-publish']
    if len(attaches) != 2 or len(admits) != 2 or max(attaches) >= min(admits) or len(publishes) != 2:
        raise ValueError('selected point attach/admit/publication ordering differs')
    if any(r['flags'] != flags or r['fallback'] for r in publishes):
        raise ValueError('selected point publication policy differs')
    return actual


def verify_idle_shift(rows, metadata):
    if not metadata['pointInput'].get('shift'):
        raise ValueError('idle Shift input omitted its modifier')
    appended = [r for r in rows if r.get('event') == 'player-order-queued' and r['countAfter']]
    if len(appended) != 2 or any(r['countBefore'] != 0 or r['countAfter'] != 1 or
                               r['before'] != [-1,-1] for r in appended):
        raise ValueError('idle Shift did not start from empty current orders')


def verify_lifecycle(rows, case):
    metadata = [r for r in rows if r.get('event') == 'metadata']
    ending = [r for r in rows if r.get('event') == 'trace-end']
    if len(metadata) != 1 or {k:v for k,v in metadata[0].items() if k not in ('event','pid')} != case['metadata']:
        raise ValueError('selected point provenance differs')
    if len(ending) != 1 or not ending[0].get('installed') or any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
        raise ValueError('selected point observer incomplete/failed')
    flags = case.get('packet_flags', 8)
    if flags not in (8, 9): raise ValueError('unsupported selected point packet policy')
    verify_producer(rows,case['producer'],flags)
    if flags == 9: verify_idle_shift(rows,metadata[0])
    for event in ('motion-decision','velocity-commit','arrival-evaluation','task-arrival',
                  'clock-source-begin','clock-source-end','clock-advance-begin','clock-advance-end','clock-owner-begin','clock-owner-end'):
        if not sum(r.get('event') == event for r in rows) or sum(r.get('event') == event for r in rows) != ending[0]['counts'].get(event):
            raise ValueError('selected point observer count differs: '+event)
    phases = canonical(rows)
    if digest(phases) != case['phases_sha256']: raise ValueError('selected point group/pose/decision words differ')
    commits = [r for r in phases if r['event'] == 'velocity-commit']
    words = [[r['member'], *[r['after'][i] for i in (0,2,3,4,5,7)]] for r in commits]
    if words != case['engine_motion']: raise ValueError('selected point engine words differ')
    owners = [r for r in phases if r['event'].startswith('pair-group-phase')]
    if len(owners) != 4*117: raise ValueError('selected point owner extent differs')
    for i in range(0,len(owners),4):
        if [(r['event'],r['phase']) for r in owners[i:i+4]] != [('pair-group-phase-begin','decide'),('pair-group-phase-end','decide'),('pair-group-phase-begin','commit'),('pair-group-phase-end','commit')]:
            raise ValueError('selected point owner phase order differs')
    if [sum(r['member']==i for r in commits) for i in (0,1)] != [117,111]:
        raise ValueError('selected point member commit extent differs')
    arrivals = [r for r in rows if r.get('event') == 'task-arrival']
    if len(arrivals) != 2 or any(r['after']['orderHead'] != [-1,-1] for r in arrivals):
        raise ValueError('selected point natural order completion differs')
    helper = [r for r in rows if r.get('event') == 'player-input-helper']
    if len(helper) != 1 or helper[0]['sha256'] != case['metadata']['source_sha256']['wc3-ui-input.exe'] or 'down/up accepted' not in helper[0]['output']:
        raise ValueError('selected point native input helper witness differs')
    return dict(passed=True,public_selected_members=2,group_owner_passes=117,natural_arrivals=2,
                member_commits=[117,111],relative_motion_sha256=digest([[r[0],*r[2:]] for r in words]))


def render_header(fixture):
    prefix = fixture.get('engine_prefix', 'selected_point')
    out = '/* Original selected-unit Move; clocks are supplied input timing, not wall-input determinism. */\n'
    out += 'static uint32_t const ' + prefix + '_inputs[2][4] = {\n'
    for c in fixture['cases']:
        a = next(r for r in c['producer'] if r['event']=='player-point-action-begin')
        out += '    {'+', '.join('0x%08xu'%w for w in [a['clock'][0],a['counter'],*a['point']])+'},\n'
    out += '};\nstatic uint32_t const ' + prefix + '_motion[2][228][7] = {\n'
    for c in fixture['cases']:
        out += '  {\n'
        for row in c['engine_motion']: out += '    {'+', '.join('0x%08xu'%w for w in row)+'},\n'
        out += '  },\n'
    return out+'};\n'


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace',type=Path);p.add_argument('--repeat',required=True,type=Path)
    p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',required=True,type=Path)
    args=p.parse_args();fixture=json.loads(args.fixture.read_text())
    engine=ctypes.CDLL(str(args.engine_library.resolve()));configure(engine);results=[]
    for path,case in zip((args.trace,args.repeat),fixture['cases'],strict=True):
        rows=[json.loads(s) for s in path.read_text().splitlines()]
        result=verify_lifecycle(rows,case);result.update(verify_motion(rows,engine,None));results.append(result)
    if results[0]['relative_motion_sha256'] != results[1]['relative_motion_sha256']:
        raise ValueError('selected point repeated pose/velocity/facing differs')
    if args.check_engine_header and args.check_engine_header.read_text()!=render_header(fixture):
        raise ValueError('selected point engine header differs')
    result=dict(passed=True,repeated=True,public_selected_members=2,natural_arrivals=4,
                exact_decisions=sum(r['exact_decisions'] for r in results),
                exact_velocity_commits=sum(r['exact_velocity_commits'] for r in results),
                relative_motion_sha256=results[0]['relative_motion_sha256'],scope=fixture['scope'])
    for key,path in [('trace',args.trace),('repeat',args.repeat),('fixture',args.fixture),('engine_library',args.engine_library)]:
        result[key+'_sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__': main()
