#!/usr/bin/env python3
"""Verify complete retail Slow/Bloodlust travel and public buff filtering.

The Move regression replays the captured spell application callbacks. Spell
windup, caster steering and cast-resource timing are separate ORDER-01.13 work.
"""
import argparse,collections,copy,ctypes,hashlib,json,re
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary
from verify_wc3_selected_queued_trace import canonical,digest,motion_words,owner_order
from verify_wc3_movement_bypasses_trace import producers,routing,boundary_states


def modifier_events(rows):
    out=[]
    for r in rows:
        e=r.get('event','')
        if not e.startswith('modifier-'):continue
        if e=='modifier-detach':
            out.append(dict(event=e,rawcode=r['words'][13],flags=r['words'][8],vtable=r['vtable'],methods=r['methods']))
        else:out.append({k:v for k,v in r.items()if k not in('ms','unit','move','buff')})
    return out


def routes(rows):
    actors=[];objects={}
    for r in rows:
        if r.get('event')=='pair-group-phase-begin':
            for m in r['members']:
                if m['mover']not in actors:actors.append(m['mover'])
        if r.get('event')=='velocity-commit':objects[r['fineObject']]=r['mover']
    result=copy.deepcopy(routing(rows))
    for r in result:
        if 'blockers'not in r:continue
        normalized=[]
        for o in r['blockers']['objects'].values():
            if not o['isMover']or o['object']not in objects or objects[o['object']]!=o['payload']:
                raise ValueError('unclassified modifier route blocker')
            normalized.append(dict(member=actors.index(o['payload']),**{k:v for k,v in o.items()if k not in('object','payload')}))
        r['blockers']['objects']=normalized
    return result


def states(rows):
    return [{k:v for k,v in s.items()if k not in('paused','mask')}for s in boundary_states(rows)]


def verify_contract(s):
    marks=[re.fullmatch(r'PATHTRACE tick=(\d+) label=(\w+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)',m)for m in s['markers']]
    if len(marks)!=317 or any(m is None for m in marks)or [int(m[1])for m in marks if m[2]=='sample']!=list(range(1,301)):
        raise ValueError('complete modifier public lifetime required')
    expected=[(0,'start_speed_modifiers'),(10,'point_move'),(35,'caster_created'),(40,'slow_requested'),(45,'modifier_applied'),(45,'remove_neither'),(45,'remove_other_polarity'),(45,'remove_physical'),(90,'slow_removed'),(100,'haste_requested'),(105,'modifier_applied'),(105,'remove_neither'),(105,'remove_other_polarity'),(105,'remove_physical'),(110,'reverse_move'),(140,'haste_removed'),(300,'complete')]
    if [(int(m[1]),m[2])for m in marks if m[2]!='sample']!=expected:raise ValueError('modifier producer order differs')
    if len(s['motion'])!=594 or collections.Counter(m[0]for m in s['motion'])!={0:590,1:4}or len(s['states'])!=17 or len(s['routes'])!=26:
        raise ValueError('modifier owner/routing lifetime incomplete')
    if digest(s['motion'])!=s['motion_sha256']:raise ValueError('committed modifier words differ')
    filters=[r for r in s['modifiers']if r['event']=='modifier-filter-end']
    args=[[0,0,1,1,0,0,0,0,1],[0,0,1,1,0,1,0,0,1],[1,0,0,0,1,1,1,0,1],[0,1,0,0,1,0,1,0,1],[0,0,1,1,0,0,0,0,1],[0,0,1,1,0,0,1,0,1],[1,0,0,0,1,1,1,0,1],[0,0,1,1,0,1,0,0,1]]
    if [r['args']for r in filters]!=args or [r['count']for r in filters]!=[0,0,0,1,0,0,0,1]:raise ValueError('native buff classification/removal differs')
    changes=[]
    for r in s['modifiers']:
        if r['event']=='modifier-speed-effective':
            value=[r['base'],r['multiplier'],r['output']]
            if not changes or changes[-1]!=value:changes.append(value)
    if changes!=[[0x43870000,0x3f800000,0x43870000],[0x43870000,0x3ecccccc,0x43160000],[0x43870000,0x3f800000,0x43870000],[0x43870000,0x3fa00000,0x43a8c000],[0x43870000,0x3f800000,0x43870000]]:
        raise ValueError('effective speed application/restoration differs')
    if len(s['buff_markers'])!=317 or s['buff_markers'][-1]!='PATHBUFF slow=0 haste=0 speed=270.000':raise ValueError('public buff query lifetime incomplete')


def render_header(s):
    u=lambda row:','.join(str(v)+'u'for v in row)
    out='/* Retail main mover only: spell application callbacks are explicit inputs.\n * Four caster commits remain verified in the native fixture, not emulated here. */\n'
    out+='static uint32_t const modifier88_motion[][7]={\n'+''.join('    {'+u(r)+'},\n'for r in s['motion']if r[0]==0)+'};\n'
    out+='static struct { cstring_t marker; uint32_t state[6],world[3],primary[3]; } const modifier88_states[]={\n'
    out+=''.join('    {'+json.dumps(r['marker'])+',{'+u(r['state'])+'},{'+u(r['world'])+'},{'+u(r['primary'])+'}},\n'for r in s['states'])+'};\n'
    for key in('markers','buff_markers'):
        out+='static cstring_t const modifier88_'+key+'[]={\n'+''.join('    '+json.dumps(m)+',\n'for m in s[key])+'};\n'
    return out


def verify(rows,s,case,engine):
    verify_contract(s)
    meta=[r for r in rows if r.get('event')=='metadata'];ends=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or {k:v for k,v in meta[0].items()if k not in('event','pid')}!=case['metadata']:raise ValueError('modifier capture provenance differs')
    if len(ends)!=1 or not ends[0].get('installed')or any(r.get('event')=='trace-failed'or r.get('type')=='error'for r in rows):raise ValueError('modifier capture failed/incomplete')
    if dict(collections.Counter(r.get('event')for r in rows))!=case['event_counts']or ends[0]['counts']!=case['observer_counts']:raise ValueError('modifier observer extents differ')
    for actual,key in [(motion_words(rows),'motion'),(states(rows),'states'),(routes(rows),'routes'),(modifier_events(rows),'modifiers'),(producers(rows),'producers'),(owner_order(rows),'owner_order')]:
        if actual!=s[key]:raise ValueError('native modifier contract differs: '+key)
    if digest(canonical(rows))!=s['phases_sha256']or [r['value']for r in rows if r.get('event')=='marker']!=s['markers']:raise ValueError('modifier public/owner order differs')
    loaded=[r for r in rows if r.get('event')=='map-load-complete']
    if len(loaded)!=1 or loaded[0]['cells']!=s['terrain']or digest(loaded[0]['hierarchy'])!=s['hierarchy_sha256']:raise ValueError('modifier authored map differs')
    result=verify_motion(rows,engine,None);result.update(verify_primary(rows,engine,s))
    result.update(passed=True,boundary_states=17,main_commits=590,caster_commits=4,removal_filters=8,motion_sha256=s['motion_sha256']);return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path);p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',required=True,type=Path);a=p.parse_args()
    s=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine)
    engine.pathing_heading_error.argtypes=[ctypes.c_uint32]*3;engine.pathing_heading_error.restype=ctypes.c_uint32
    results=[]
    for path,case in zip(a.traces,s['cases'],strict=True):
        if hashlib.sha256(path.read_bytes()).hexdigest()!=case['sha256']:raise ValueError('modifier raw capture bytes differ')
        results.append(verify([json.loads(l)for l in path.read_text().splitlines()],s,case,engine))
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(s):raise ValueError('literal modifier engine header differs')
    report=dict(passed=True,cases=len(results),results=results,scope=s['scope'])
    for key in('exact_velocity_commits','exact_decisions','owner_callbacks','boundary_states','main_commits','caster_commits','removal_filters'):report[key]=sum(r[key]for r in results)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':main()
