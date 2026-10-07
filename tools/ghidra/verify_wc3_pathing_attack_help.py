#!/usr/bin/env python3
"""Original ally-alert callback through real owner, diplomacy and registry.

Constructed objects; read-only stop at accepted Attack exemption entry. Real
49bb50/695200/695600/68bd60/1e7910/1e8150/1e2b60/056a40/054530 and packet
constructor run unchanged. No fabricated player getter, alliance or callbacks.
Public query/radius/cooldown reachability is separate live evidence.
"""
import argparse
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
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--fixture', type=Path)
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
    source,victim,game=0x10007c00,0x10008000,0x10008800
    w(0x6fd687a8,game);w(game+0x54,4)
    for i in range(4):
        player=0x10009000+i*0x100;diplomacy=0x1000a000+i*0x100
        w(game+0x58+i*4,player);w(player+0x38,diplomacy)
    w(vt+0xec,0x6f685da0);w(vt+0xfc,0x6f4935e0)
    w(ability,vt);w(unit,vt);w(unit+0x58,1);w(victim,vt);w(victim+0x58,0)
    w(source,vt);w(source+0x58,3)
    w(registry+0x1c,3)
    for i,(player,offset) in enumerate(((0,0x48),(1,0x58),(1,0x38))):
        payload=0x1000b000+i*0x100
        w(slots+i*8,-2,payload);w(payload+0x18,1)
        w(0x1000a000+player*0x100+offset+8,i,1)
    accepted=[];packets=[]
    def stop_begin(machine,address,size,data):
        accepted.append(address);machine.emu_stop()
    def notification(machine,address,size,data):
        sp=machine.reg_read(UC_X86_REG_ESP);p=r(sp+4)
        packets.append([r(p),r(p+0xc),r(p+0x10),r(p+0x14)])
    u.hook_add(UC_HOOK_CODE,stop_begin,begin=0x6f49bc40,end=0x6f49bc40)
    u.hook_add(UC_HOOK_CODE,notification,begin=0x6f4935e0,end=0x6f4935e0)
    cases=[]
    for request_on,response_on,passive,engaged,disabled,flags,suspended in itertools.product(
            (False,True),(False,True),(False,True),(0,1),(0,1),(0,0x100000,0x200000),(0,1)):
        w(0x1000b000+0x78,2 if request_on else 0)
        w(0x1000b100+0x78,1 if response_on else 0)
        w(0x1000b200+0x78,8 if passive else 0)
        w(ability+0x220,engaged);w(ability+0x3c,disabled);w(unit+0x5c,flags);w(unit+0x54,suspended)
        accepted.clear();packets.clear();call(0x6f49bb50,ability,source,victim)
        expected=request_on and response_on and not passive and not engaged and not disabled and not flags and not suspended
        assert bool(accepted)==expected,(request_on,response_on,passive,engaged,disabled,flags,suspended,accepted)
        notified=request_on and response_on and not passive and not engaged
        assert bool(packets)==notified
        if packets:assert packets==[[source,4,0,0x10]],packets
        cases.append(dict(input=[int(request_on),int(response_on),int(passive),engaged,disabled,flags,suspended],
                          notified=notified,accepted=expected))
    report=dict(binary_sha256=SHA,passed=True,cases=len(cases),accepted=sum(c['accepted'] for c in cases),
                notified=sum(c['notified'] for c in cases),scope=__doc__,rows=cases)
    if args.expected:
        import gzip
        assert report==json.loads(gzip.decompress(args.expected.read_bytes())),'frozen original ally callback differs'
    if args.fixture:
        import gzip
        args.fixture.write_bytes(gzip.compress((json.dumps(report,separators=(',',':'))+'\n').encode(),mtime=0))
    args.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='rows'},indent=2))

if __name__=='__main__':main()
