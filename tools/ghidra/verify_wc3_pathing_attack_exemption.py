#!/usr/bin/env python3
"""Original Attack exemption policy and all three physical-group consumers.

Constructed objects with original scalar initialization, registry resolution,
remaining-time virtual method and group prediction. Read-only stops at the
request-arm entry and after the notification guards, before retaliation.
These stops do not emulate scheduling or certify public producer reachability.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import struct
from pathlib import Path
from verify_wc3_pathing_numeric import bits, initialize_runtime_scalars

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX
    from wc3_shipped_crt import load_crt
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--engine-library', type=Path)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--fixture', type=Path)
    ap.add_argument('--header', type=Path)
    ap.add_argument('--expected', type=Path)
    args = ap.parse_args()
    raw = args.binary.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == SHA
    pe = struct.unpack_from('<I', raw, 60)[0]; opt = pe + 24
    base, size = (struct.unpack_from('<I', raw, opt+n)[0] for n in (28, 56))
    u = Uc(UC_ARCH_X86, UC_MODE_32); u.mem_map(base, (size+4095)&~4095)
    u.mem_write(base, raw[:struct.unpack_from('<I', raw, opt+60)[0]])
    for i in range(struct.unpack_from('<H', raw, pe+6)[0]):
        section = opt+struct.unpack_from('<H', raw, pe+20)[0]+40*i
        va, n, offset = struct.unpack_from('<III', raw, section+12)
        if n: u.mem_write(base+va, raw[offset:offset+n])
    u.mem_map(0, 0x1000); u.mem_map(0x10000000, 0x10000); u.mem_map(0x20000000, 0x10000)
    ability, unit, mover, request, clock, vt, registry, slots, packet, group, rows, paths, out = [
        0x10000000+n for n in (0,0x800,0x1000,0x1800,0x2000,0x2800,0x3000,0x3100,0x3200,0x3800,0x4000,0x5000,0x6000)]
    stack, stop = 0x20008000, 0x30000000
    def w(a, *v): u.mem_write(a, struct.pack('<%dI'%len(v), *(x&0xffffffff for x in v)))
    def r(a): return struct.unpack('<I', u.mem_read(a,4))[0]
    initialize_runtime_scalars(u, stack, stop)
    crt = load_crt(u,args.binary.parent/'msvcr120.dll'); w(0x6fa7c4fc,crt['exports']['isdigit'])
    def call(entry, this, *values):
        w(stack,stop,*values); u.reg_write(UC_X86_REG_ESP,stack); u.reg_write(UC_X86_REG_ECX,this)
        u.emu_start(entry,stop,count=100000)
        return u.reg_read(UC_X86_REG_EAX)
    for entry in (0x6f00b510,0x6f00b520,0x6f0040f0,0x6f004100): call(entry,0)
    assert r(0x6fd541a4)==bits(256) and r(0x6fd541a8)==bits(16)
    assert r(0x6fd6bf54)==bits(3) and r(0x6fd6bf58)==bits(.5)
    w(0x6fd68610,registry); w(registry+0xc,slots); w(registry+0x1c,1)
    w(slots,-2,mover); w(mover+0x18,1); w(unit+0x164+8,0,1)
    w(ability+0x30,unit); w(ability+0x3d8,vt)
    # Actual live control virtual18 = remaining, NOT elapsed.
    w(vt+0x18,0x6f060ad0); w(request+0xc,clock)
    stops=[]
    def arm(machine,address,size,data):
        stops.append(address); machine.emu_stop()
    arm_hook=u.hook_add(UC_HOOK_CODE,arm,begin=0x6f0608d0,end=0x6f0608d0)
    timer=[]
    for active, cancelled, now, remaining in itertools.product((False,True),(False,True),(0,8,299.5),
            (bits(3),0x40200001,bits(2.5),0x401fffff,bits(2),bits(.5),0)):
        w(ability+0x3e4,request if active else 0); w(request+0x10,0x10000 if cancelled else 0)
        w(clock+0x40,bits(now)); w(request+4,bits(now+struct.unpack('<f',struct.pack('<I',remaining))[0]))
        w(mover+0xd8,0); stops.clear()
        call(0x6f49bc40,ability)
        armed=bool(stops)
        assert u.reg_read(UC_X86_REG_EIP)==(0x6f0608d0 if armed else stop)
        if armed:
            sp=u.reg_read(UC_X86_REG_ESP)
            assert r(sp+8)==0xd01bd and r(r(sp+4))==bits(3)
        timer.append(dict(active=active,cancelled=cancelled,now=bits(now),deadline=r(request+4),
                          armed=armed,set_bit=bool(r(mover+0xd8)&0x1000000)))
    u.hook_del(arm_hook)
    notification=[]
    notify_hook=u.hook_add(UC_HOOK_CODE,arm,begin=0x6f49bc40,end=0x6f49bc40)
    for disabled, flags, suspension, source, packet_flags in itertools.product((0,1),(0,0x100000,0x200000),
            (0,1),(0,unit),(0,2,4)):
        w(ability+0x3c,disabled);w(unit+0x20,flags);w(unit+0x54,suspension)
        w(packet,source,0,0,packet_flags);stops.clear();call(0x6f4935e0,ability,packet)
        notification.append(dict(input=[disabled,flags,suspension,bool(source),packet_flags],accepted=bool(stops)))
    u.hook_del(notify_hook)
    consumers=[]
    w(group+0x28,rows);w(group+0x38,2)
    # Prediction uses the actual owner clock at +14; zero elapsed in these cases.
    owner=0x10007000;w(0x6fd53a48,owner);w(owner+0x14+0x40,0,0,bits(300))
    for group_flags,cooldown,first_flags,second_flags,exempt,path_flags,near in itertools.product(
            (0,4,8,0x100),(0,66),(0,0x10000,0x200000),(0,0x10000,0x40000),(0,1,2),
            (0,0x10000000,0x20000000),(False,True)):
        w(group+0x80,group_flags);w(group+0x68,cooldown)
        for i,flags in enumerate((first_flags,second_flags)):
            mv=mover+i*0x200;row=rows+i*0x2c;path=paths+i*0x100
            u.mem_write(mv,bytes(0x200));w(mv+0x14,0);w(mv+0xa8,path)
            w(mv+0xd8,0x1000000 if exempt==i+1 else 0);w(path+0x88,path_flags)
            w(row+0x14,mv,bits(0 if near else 100),0,0,0,flags)
        share=call(0x6f169b50,group); classify=call(0x6f169b00,group)
        status=call(0x6f16b120,group,out,out+4)
        consumers.append(dict(input=[group_flags,cooldown,first_flags,second_flags,exempt,path_flags,int(near)],
                              output=[share,classify,status,r(out),r(out+4)]))
    report=dict(binary_sha256=SHA,scope=__doc__,timer=timer,notification=notification,consumers=consumers)
    assert any(x['armed'] for x in timer) and any(not x['armed'] for x in timer)
    if args.engine_library:
        engine=ctypes.CDLL(str(args.engine_library.resolve()))
        engine.pathing_attack_exemption_rearm.argtypes=[ctypes.c_uint32]*3
        engine.pathing_attack_exemption_rearm.restype=ctypes.c_uint32
        for row in timer:
            actual=engine.pathing_attack_exemption_rearm(row['active'] and not row['cancelled'],row['deadline'],row['now'])
            assert bool(actual)==row['armed'],row
    if args.expected:
        frozen=json.loads(__import__('gzip').decompress(args.expected.read_bytes()))
        assert report==frozen,'original witness differs'
    if args.fixture: args.fixture.write_text(json.dumps(report,separators=(',',':'))+'\n')
    if args.header:
        text='/* Original Attack49bc40, including actual remaining virtual18. */\nstatic struct { uint32_t active, deadline, now, armed; } const retail_attack_exemption[] = {\n'
        for row in timer:
            text+='    {%du,0x%08xu,0x%08xu,%du},\n'%(row['active'] and not row['cancelled'],row['deadline'],row['now'],row['armed'])
        text+='};\nstatic struct { uint32_t flags, cooldown, arrived, exempt, near, status; } const retail_attack_group_status[] = {\n'
        for row in consumers:
            flags,cooldown,first,second,exempt,path,near=row['input']
            if flags in (0,4,0x100) and first in (0,0x10000) and second==0 and path==0:
                text+='    {0x%xu,%du,%du,%du,%du,%du},\n'%(flags,cooldown,first!=0,exempt,near,row['output'][2])
        args.header.write_text(text+'};\n')
    summary=dict(binary_sha256=SHA,passed=True,timer_cases=len(timer),notification_cases=len(notification),consumer_cases=len(consumers),
                 timer_remaining_virtual='6f060ad0',rearm_condition='3.0 - remaining >= 0.5 (elapsed threshold)')
    args.report.write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))


if __name__=='__main__': main()
