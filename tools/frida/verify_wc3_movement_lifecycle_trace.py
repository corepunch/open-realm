#!/usr/bin/env python3
"""Certify public speed/retarget/Stop motion and committed cell-boundary state."""
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
    """Retain actual committed pose, separately from decimal predicted markers."""
    hero=next(r['mover']for r in rows if r.get('event')=='speed-cap-change')
    pose=next(r['before']for r in rows if r.get('event')=='speed-cap-change')
    result=[]
    for r in rows:
        if r.get('mover')==hero and isinstance(r.get('after'),list) and len(r['after'])==8:pose=r['after']
        if r.get('event')=='marker' and 'label=sample ' not in r['value']:
            result.append(dict(marker=r['value'],state=[pose[i]for i in(0,2,3,4,5,7)]))
    return result


def chains(rows):
    result=[]
    for r in rows:
        if r.get('event')!='cell-links':continue
        if r['truncated'] or (r['x'],r['y'])!=(20,19):raise ValueError('complete boundary cell chain required')
        values=[]
        for obj in r['records']:
            if obj['kind']not in(0,1) or obj['category']not in(0x010000ca,0x01000000):raise ValueError('unclassified boundary occupant')
            values.append({k:obj[k]for k in('kind','rectangle','category','flags')})
        result.append(dict(marker=r['marker'],records=values))
    return result


def verify_contract(s):
    marks=[re.fullmatch(r'PATHTRACE tick=(\d+) label=(\w+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)',x)for x in s['markers']]
    if len(marks)!=324 or any(m is None for m in marks) or [int(m[1])for m in marks if m[2]=='sample']!=list(range(1,301)):
        raise ValueError('complete public movement timer lifetime required')
    if not s['markers'][-1].endswith('order=0') or s['motion'][-1][4:6]!=[0,0] or len(s['motion'])!=763:
        raise ValueError('natural final arrival and literal motion required')
    if len(s['states'])!=24 or len(s['chains'])!=23 or len(s['destinations'])!=7:
        raise ValueError('complete retarget/boundary state required')
    for i,r in enumerate(s['states']):
        if 'label=after_stop ' in r['marker'] or 'label=boundary_reset ' in r['marker']:
            if r['state'][3:5]!=[0,0]:raise ValueError('Stop must publish zero velocity')
        if 'label=boundary_reset ' in r['marker']:
            if r['state'][1:3]!=[0x41a00000,0x41980000]:raise ValueError('exact cell boundary placement required')
            chain=next(x for x in s['chains']if x['marker']==r['marker'])
            actors=[x for x in chain['records']if x['category']==0x010000ca]
            if not actors or actors[0]['kind']!=1 or actors[0]['rectangle']!=[18,19,21,22] or actors[0]['flags']:
                raise ValueError('stationary boundary occupancy must remain active')
    for start,end in ((75,79),(150,153),(180,184)):
        segment=[r for r in s['motion']if start*0.1<=ctypes.c_float.from_buffer_copy(ctypes.c_uint32(r[1])).value<end*0.1 and r[4:6]==[0,0]]
        if len(segment)<2 or len({tuple(r[2:4])for r in segment})!=1 or len({r[6]for r in segment})!=len(segment):
            raise ValueError('stationary turns must retain position while changing facing')
    natives=[r for r in s['producers']if r['event']=='speed-native']
    if [(r['name'],r.get('input'),r.get('output'))for r in natives]!=[(name,inp,out)for value in(150.,250.,400.,0.,150.,200.,0.,300.,150.)
        for name,inp,out in(('SetUnitMoveSpeed',float_word(value),None),('GetUnitMoveSpeed',None,float_word(value or 150.)))]:
        raise ValueError('public speed clamp and setter/getter timeline differs')
    if digest(s['motion'])!=s['motion_sha256']:raise ValueError('motion words changed')


def format_controls(binary):
    """Execute original0701d0; delegate only its integer sprintf import."""
    import struct
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EAX
    data=binary.read_bytes()
    if hashlib.sha256(data).hexdigest()!='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':raise ValueError('original formatter binary differs')
    pe=struct.unpack_from('<I',data,60)[0];opt=pe+24
    base,size=(struct.unpack_from('<I',data,opt+n)[0]for n in(28,56))
    uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(base,(size+4095)&~4095)
    uc.mem_write(base,data[:struct.unpack_from('<I',data,opt+60)[0]])
    for i in range(struct.unpack_from('<H',data,pe+6)[0]):
        sec=opt+struct.unpack_from('<H',data,pe+20)[0]+40*i
        va,count,offset=struct.unpack_from('<III',data,sec+12)
        if count:uc.mem_write(base+va,data[offset:offset+count])
    uc.mem_map(0x10000000,0x10000);uc.mem_map(0x20000000,0x10000);uc.mem_map(0x30000000,0x1000)
    stack,stop,stub,scalar,output=0x20008000,0x30000000,0x30000100,0x10000100,0x10000200
    def write(address,*words):uc.mem_write(address,struct.pack('<'+'I'*len(words),*words))
    def read(address):return struct.unpack('<I',uc.mem_read(address,4))[0]
    def string(address):return bytes(uc.mem_read(address,256)).split(b'\0',1)[0].decode('ascii')
    # Execute the real registered scalar startup; do not synthesize a different rounding environment.
    for entry in(0x6f001dd0,0x6f001a80,0x6f001b80):
        write(stack,stop);uc.reg_write(UC_X86_REG_ESP,stack);uc.emu_start(entry,stop,count=10000)
    write(0x6fa7c500,stub)
    def sprintf(machine,address,size,unused):
        sp=machine.reg_read(UC_X86_REG_ESP);dest,fmt=read(sp+4),string(read(sp+8))
        if fmt=='%d':value=str(ctypes.c_int32(read(sp+12)).value)
        elif fmt=='%*d.%0*d':
            width,whole,precision,fraction=(ctypes.c_int32(read(sp+n)).value for n in(12,16,20,24))
            value=f'{whole:{width}d}.{fraction:0{precision}d}'
        else:raise ValueError('unexpected original integer formatter: '+fmt)
        encoded=value.encode('ascii');machine.mem_write(dest,encoded+b'\0')
        machine.reg_write(UC_X86_REG_EAX,len(encoded));machine.reg_write(UC_X86_REG_ESP,sp+4);machine.reg_write(UC_X86_REG_EIP,read(sp))
    uc.hook_add(UC_HOOK_CODE,sprintf,begin=stub,end=stub)
    words=[0,0x80000000,1,0x80000001,0x007fffff,0x00800000,0x4effffff,0x4f000000,
           0x7f7ffffe,0x7f7fffff,0xff7ffffe,0xff7fffff,0x7f800000,0xff800000,0x7fc00000,0xffc00000]
    for value in(0.0004999,0.0005,0.0005001,0.9994999,0.9995,0.9995001,1.2345,431.4385,999999.9375):
        words.extend((float_word(value),float_word(-value)))
    result=[]
    for word in words:
        write(scalar,word);uc.mem_write(output,b'X'*128);write(stack,stop,0,3)
        uc.reg_write(UC_X86_REG_ESP,stack);uc.reg_write(UC_X86_REG_ECX,scalar);uc.reg_write(UC_X86_REG_EDX,output)
        uc.emu_start(0x6f0701d0,stop,count=100000)
        if uc.reg_read(UC_X86_REG_EIP)!=stop or uc.reg_read(UC_X86_REG_ESP)!=stack+12 or read(scalar)!=word:
            raise ValueError('original formatter ABI/input differs')
        result.append(dict(word=word,text=string(output)))
    return result


def float_word(value):return ctypes.c_uint32.from_buffer_copy(ctypes.c_float(value)).value


def render_header(s):
    out='#ifndef BZ_RETAIL_MOVEMENT_LIFECYCLE_H\n#define BZ_RETAIL_MOVEMENT_LIFECYCLE_H\n/* Literal original complete speed/heading/Stop/boundary journey. */\n'
    out+='static uint32_t const lifecycle84_motion[][7]={\n'+''.join('    {'+','.join(str(v)+'u'for v in r)+'},\n'for r in s['motion'])+'};\n'
    out+='static struct { cstring_t marker; uint32_t state[6]; bool occupied; } const lifecycle84_states[]={\n'
    for r in s['states']:
        c=next((c for c in s['chains']if c['marker']==r['marker']),None)
        objects=[] if c is None else [o for o in c['records']if o['category']==0x010000ca]
        occupied=bool(objects and objects[0]['kind']==1)
        out+='    {'+json.dumps(r['marker'])+',{'+','.join(str(v)+'u'for v in r['state'])+'},'+str(occupied).lower()+'},\n'
    out+='};\nstatic cstring_t const lifecycle84_markers[]={\n'+''.join('    '+json.dumps(m)+',\n'for m in s['markers'])+'};\n'
    out+='static struct { uint32_t word; cstring_t text; } const lifecycle84_format[]={\n'
    return out+''.join('    {'+str(c['word'])+'u,'+json.dumps(c['text'])+'},\n'for c in s['format_controls'])+'};\n#endif\n'


def verify(rows,s,case,engine):
    verify_contract(s)
    meta=[r for r in rows if r.get('event')=='metadata'];ends=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or {k:v for k,v in meta[0].items()if k not in('event','pid')}!=case['metadata']:
        raise ValueError('movement lifecycle capture provenance differs')
    if len(ends)!=1 or not ends[0].get('installed') or any(r.get('event')=='trace-failed' or r.get('type')=='error'for r in rows):
        raise ValueError('movement lifecycle observer failed/incomplete')
    if dict(collections.Counter(r.get('event')for r in rows))!=case['event_counts'] or ends[0]['counts']!=case['observer_counts']:
        raise ValueError('movement lifecycle observer extents differ')
    if (motion_words(rows)!=s['motion'] or producers(rows)!=s['producers'] or boundary_states(rows)!=s['states'] or
        chains(rows)!=s['chains'] or overlap_lifecycle(rows)!=s['lifecycle'] or owner_order(rows)!=s['owner_order'] or
        digest(canonical(rows))!=s['phases_sha256'] or [r['value']for r in rows if r.get('event')=='marker']!=s['markers'] or
        [r['destination']for r in rows if r.get('event')=='point-task']!=s['destinations']):
        raise ValueError('movement lifecycle producer/owner/occupancy/motion differs')
    loaded=[r for r in rows if r.get('event')=='map-load-complete']
    if len(loaded)!=1 or (loaded[0]['width'],loaded[0]['height'])!=(64,64) or any(loaded[0]['cells']) or digest(loaded[0]['hierarchy'])!=s['hierarchy_sha256']:
        raise ValueError('original empty map initialization differs')
    result=verify_motion(rows,engine,None);result.update(verify_primary(rows,engine,s))
    result.update(passed=True,boundary_states=24,cell_chains=23,point_orders=7,motion_sha256=s['motion_sha256'])
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--binary',required=True,type=Path);p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',required=True,type=Path);a=p.parse_args()
    s=json.loads(a.fixture.read_text());controls=format_controls(a.binary)
    if controls!=s['format_controls']:raise ValueError('original default formatter controls differ')
    engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine)
    engine.pathing_heading_error.argtypes=[ctypes.c_uint32]*3;engine.pathing_heading_error.restype=ctypes.c_uint32
    results=[]
    for path,case in zip(a.traces,s['cases'],strict=True):
        r=verify([json.loads(l)for l in path.read_text().splitlines()],s,case,engine)
        r['trace_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();results.append(r)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(s):raise ValueError('movement lifecycle C header differs')
    report=dict(passed=True,cases=len(results),results=results,scope=s['scope'],original_format_controls=len(controls))
    for key in('exact_velocity_commits','exact_decisions','owner_callbacks','boundary_states','cell_chains','point_orders'):
        report[key]=sum(r[key]for r in results)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
