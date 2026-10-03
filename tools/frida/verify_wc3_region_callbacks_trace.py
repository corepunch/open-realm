#!/usr/bin/env python3
"""Certify original public region callbacks, forced writes and removal during Move."""
import argparse
import collections
import ctypes
import hashlib
import json
import re
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary
from verify_wc3_selected_queued_trace import canonical, digest, motion_words, owner_order
from verify_wc3_target_overlap_trace import overlap_lifecycle


def producers(rows):
    events={'speed-native','speed-cap-change','speed-publication','mover-stop',
            'position-query','position-commit','position-native','expression-timer-start'}
    volatile={'ms','mover','unit','ability','bridge','handle','handler'}
    return [{k:v for k,v in r.items() if k not in volatile} for r in rows if r.get('event') in events]


def boundary_states(rows):
    state=world=None;primary=[0,0,0x43960000];result=[]
    for r in rows:
        if r.get('event')=='clock-advance-end' and r['domain']==20:primary=r['after'][:3]
        if r.get('event')=='position-query':
            state=[r['after'][i]for i in(0,2,3,4,5,7)];world=r['output'];primary=r['clock']
        if r.get('event')=='marker' and 'label=sample 'not in r['value']:
            dead=('label=callback_removed 'in r['value'] or 'label=region_enter_after x=0.000'in r['value'] or 'label=complete x=0.000'in r['value'])
            result.append(dict(marker=r['value'],alive=not dead,state=state if not dead else None,world=world if not dead else [0,0,0],primary=primary))
    return result


def chains(rows):
    result=[]
    for r in rows:
        if r.get('event')!='cell-links':continue
        if r['truncated'] or (r['x'],r['y'])!=(21,22):raise ValueError('complete region cell chain required')
        values=[]
        for obj in r['records']:
            if obj['kind']not in(0,1) or obj['category']not in(0x010000ca,0,202):raise ValueError('unclassified region occupant')
            values.append({k:obj[k]for k in('kind','rectangle','category','flags')})
        result.append(dict(marker=r['marker'],records=values))
    return result


def verify_contract(s):
    marks=[re.fullmatch(r'PATHTRACE tick=(\d+) label=(\w+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)',x)for x in s['markers']]
    if len(marks)!=318 or any(m is None for m in marks) or [int(m[1])for m in marks if m[2]=='sample']!=list(range(1,301)):
        raise ValueError('complete public region timer lifetime required')
    expected=[(45,'region_enter_before'),(45,'region_enter_after'),(54,'region_leave'),
              (135,'region_enter_before'),(135,'callback_teleport'),(135,'callback_new_move'),
              (135,'region_enter_after'),(135,'region_leave'),(234,'region_enter_before'),
              (234,'callback_removed'),(234,'region_enter_after')]
    if [(int(m[1]),m[2])for m in marks if m[2].startswith(('region_','callback_'))]!=expected:
        raise ValueError('region event and reentrant public order differs')
    if len(s['motion'])!=629 or len(s['states'])!=18 or len(s['chains'])!=17 or len(s['destinations'])!=4:
        raise ValueError('region callback state/owner lifetime incomplete')
    for r in s['states']:
        if r['world'][2]:raise ValueError('flat support height differs')
        if 'label=callback_teleport 'in r['marker']:
            if r['state'][1:5]!=[float_word(42),float_word(9.5),0,0] or not r['marker'].endswith('order=0'):
                raise ValueError('callback teleport must Stop and publish its new exact pose')
        if not r['alive'] and (r['state'] is not None or r['world']!=[0,0,0]):raise ValueError('removed callback must not expose old pose')
    dead=next(r for r in s['states']if 'label=region_enter_before 'in r['marker']and 'tick=234 'in r['marker'])
    if any(r[1]>dead['state'][0]for r in s['motion']):raise ValueError('removed mover cannot commit after callback')
    if digest(s['motion'])!=s['motion_sha256']:raise ValueError('region motion words changed')


def float_word(value):return ctypes.c_uint32.from_buffer_copy(ctypes.c_float(value)).value


def geometry_controls(binary):
    """Execute original05fcf0 through actual scalar floor; observe its record boundary."""
    import struct
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_ECX
    data=binary.read_bytes()
    if hashlib.sha256(data).hexdigest()!='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':raise ValueError('original region binary differs')
    pe=struct.unpack_from('<I',data,60)[0];opt=pe+24
    base,size=(struct.unpack_from('<I',data,opt+n)[0]for n in(28,56))
    uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(base,(size+4095)&~4095)
    uc.mem_write(base,data[:struct.unpack_from('<I',data,opt+60)[0]])
    for i in range(struct.unpack_from('<H',data,pe+6)[0]):
        sec=opt+struct.unpack_from('<H',data,pe+20)[0]+40*i
        va,count,offset=struct.unpack_from('<III',data,sec+12)
        if count:uc.mem_write(base+va,data[offset:offset+count])
    uc.mem_map(0x10000000,0x10000);uc.mem_map(0x20000000,0x10000);uc.mem_map(0x30000000,0x1000)
    stack,stop,region,object_,owner,rectangle=0x20008000,0x30000000,0x10000100,0x10000200,0x10000300,0x10000400
    def write(address,*words):uc.mem_write(address,struct.pack('<'+'I'*len(words),*words))
    def read(address):return struct.unpack('<I',uc.mem_read(address,4))[0]
    for entry in(0x6f001dd0,0x6f001a80,0x6f001b80):
        write(stack,stop);uc.reg_write(UC_X86_REG_ESP,stack);uc.emu_start(entry,stop,count=10000)
    write(0x6fd3c82c,owner);write(region+0x20,object_);write(object_+0x2c,owner)
    observations=[]
    def observe(machine,address,size,unused):
        sp=machine.reg_read(UC_X86_REG_ESP)
        if read(sp+8)!=object_ or machine.reg_read(UC_X86_REG_ECX)!=owner:raise ValueError('region rectangle record owner differs')
        observations.append(dict(bounds=list(struct.unpack('<iiii',machine.mem_read(read(sp+4),16))),record=read(sp+12)))
        machine.reg_write(UC_X86_REG_ESP,sp+16);machine.reg_write(UC_X86_REG_EIP,read(sp))
    uc.hook_add(UC_HOOK_CODE,observe,begin=0x6f14d960,end=0x6f14d960)
    result=[]
    for origin in((0.,0.),(-2048.,512.),(32.5,-64.25)):
        for box in((0.,0.,32.,64.),(-.125,-32.125,31.999,0.),(640.,672.,704.,736.)):
            world=[float_word(box[i]+origin[i%2])for i in range(4)]
            write(owner+0x6c,*map(float_word,origin))
            write(rectangle,world[1],world[0],world[3],world[2])
            for enabled in(0,1):
                write(stack,stop,rectangle,enabled);uc.reg_write(UC_X86_REG_ESP,stack);uc.reg_write(UC_X86_REG_ECX,region)
                uc.emu_start(0x6f05fcf0,stop,count=100000)
                if uc.reg_read(UC_X86_REG_ESP)!=stack+12 or uc.reg_read(UC_X86_REG_EIP)!=stop:raise ValueError('region rectangle ABI differs')
                result.append(dict(origin=list(map(float_word,origin)),rectangle=world,enabled=enabled,**observations[-1]))
    if len(observations)!=len(result):raise ValueError('region rectangle did not reach actual record boundary')
    return result


def render_header(s):
    out='/* Literal original public region callback journey; lazy links remain verifier-only. */\n'
    out+='static uint32_t const region85_motion[][7]={\n'+''.join('    {'+','.join(str(v)+'u'for v in r)+'},\n'for r in s['motion'])+'};\n'
    out+='static struct { cstring_t marker; uint32_t state[6],world[3],primary[3]; bool alive,occupied; } const region85_states[]={\n'
    for r in s['states']:
        c=next((c for c in s['chains']if c['marker']==r['marker']),None)
        objects=[] if c is None else [o for o in c['records']if o['category']==0x010000ca and o['kind']==1]
        occupied=any(o['rectangle'][0]<=22<o['rectangle'][2] and o['rectangle'][1]<=21<o['rectangle'][3]for o in objects)
        out+='    {'+json.dumps(r['marker'])+',{'+','.join(str(v)+'u'for v in(r['state'] or [0]*6))+'},{'+','.join(str(v)+'u'for v in r['world'])+'},{'+','.join(str(v)+'u'for v in r['primary'])+'},'+str(r['alive']).lower()+','+str(occupied).lower()+'},\n'
    out+='};\nstatic cstring_t const region85_markers[]={\n'+''.join('    '+json.dumps(m)+',\n'for m in s['markers'])+'};\n'
    out+='static struct { uint32_t origin[2],rectangle[4]; int32_t bounds[4]; } const region85_geometry[]={\n'
    for c in s['geometry_controls']:
        out+='    {{'+','.join(str(v)+'u'for v in c['origin'])+'},{'+','.join(str(v)+'u'for v in c['rectangle'])+'},{'+','.join(str(v)for v in c['bounds'])+'}},\n'
    out+='};\n'
    return out


def verify(rows,s,case,engine):
    verify_contract(s)
    meta=[r for r in rows if r.get('event')=='metadata'];ends=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or {k:v for k,v in meta[0].items()if k not in('event','pid')}!=case['metadata']:
        raise ValueError('region callback capture provenance differs')
    if len(ends)!=1 or not ends[0].get('installed') or any(r.get('event')=='trace-failed' or r.get('type')=='error'for r in rows):
        raise ValueError('region callback observer failed/incomplete')
    if dict(collections.Counter(r.get('event')for r in rows))!=case['event_counts'] or ends[0]['counts']!=case['observer_counts']:
        raise ValueError('region callback observer extents differ')
    if (motion_words(rows)!=s['motion'] or producers(rows)!=s['producers'] or boundary_states(rows)!=s['states'] or
        chains(rows)!=s['chains'] or overlap_lifecycle(rows)!=s['lifecycle'] or owner_order(rows)!=s['owner_order'] or
        digest(canonical(rows))!=s['phases_sha256'] or [r['value']for r in rows if r.get('event')=='marker']!=s['markers'] or
        [r['destination']for r in rows if r.get('event')=='point-task']!=s['destinations']):
        raise ValueError('region callback producer/owner/occupancy/motion differs')
    loaded=[r for r in rows if r.get('event')=='map-load-complete']
    if len(loaded)!=1 or (loaded[0]['width'],loaded[0]['height'])!=(64,64) or any(loaded[0]['cells']) or digest(loaded[0]['hierarchy'])!=s['hierarchy_sha256']:
        raise ValueError('original empty map initialization differs')
    result=verify_motion(rows,engine,None);result.update(verify_primary(rows,engine,s))
    result.update(passed=True,boundary_states=18,cell_chains=17,point_orders=4,motion_sha256=s['motion_sha256'])
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',required=True,type=Path);p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',required=True,type=Path);a=p.parse_args()
    s=json.loads(a.fixture.read_text());controls=geometry_controls(a.binary)
    if controls!=s['geometry_controls']:raise ValueError('original region rectangle controls differ')
    engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine)
    engine.pathing_heading_error.argtypes=[ctypes.c_uint32]*3;engine.pathing_heading_error.restype=ctypes.c_uint32
    results=[]
    for path,case in zip(a.traces,s['cases'],strict=True):
        r=verify([json.loads(l)for l in path.read_text().splitlines()],s,case,engine)
        r['trace_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();results.append(r)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(s):raise ValueError('region callback C header differs')
    report=dict(passed=True,cases=len(results),results=results,scope=s['scope'],original_geometry_controls=len(controls))
    for key in('exact_velocity_commits','exact_decisions','owner_callbacks','boundary_states','cell_chains','point_orders'):
        report[key]=sum(r[key]for r in results)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
