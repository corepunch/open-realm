#!/usr/bin/env python3
"""Execute retail16b5c0 through its speed/visibility/distance policy.

Real registry lookup, owner clock, prediction and scalar arithmetic; read-only
stop at the final16fe20 commit entry records its speed argument. Constructed
states include deliberately forced flag combinations and adjacent boundaries.
This is policy evidence, not public producer or complete motion evidence.
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
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX
    from wc3_shipped_crt import load_crt
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--engine-library', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--fixture', type=Path)
    ap.add_argument('--header', type=Path)
    args = ap.parse_args()
    binary = args.binary.read_bytes()
    assert hashlib.sha256(binary).hexdigest() == SHA
    pe = struct.unpack_from('<I', binary, 60)[0]; opt = pe + 24
    base, size = [struct.unpack_from('<I', binary, opt + n)[0] for n in (28, 56)]
    uc = Uc(UC_ARCH_X86, UC_MODE_32); uc.mem_map(base, (size + 4095) & ~4095)
    uc.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe + 20)[0] + i * 40
        va, count, offset = struct.unpack_from('<III', binary, section + 12)
        if count: uc.mem_write(base + va, binary[offset:offset + count])
    uc.mem_map(0x10000000, 0x10000); uc.mem_map(0x20000000, 0x10000)
    group, member, mover, target, owner, registry, slots, dest, cap = [0x10000000+n for n in (0,0x200,0x400,0x800,0xc00,0x1400,0x1500,0x1600,0x1700)]
    stack, stop = 0x20008000, 0x30000000
    def w(a, *v): uc.mem_write(a, struct.pack('<'+'I'*len(v), *(x&0xffffffff for x in v)))
    def r(a): return struct.unpack('<I', uc.mem_read(a,4))[0]
    initialize_runtime_scalars(uc, stack, stop)
    crt=load_crt(uc,args.binary.parent/'msvcr120.dll'); w(0x6fa7c4fc,crt['exports']['isdigit'])
    for entry in (0x6f004160,0x6f004170):
        w(stack,stop); uc.reg_write(UC_X86_REG_ESP,stack); uc.emu_start(entry,stop,count=200000)
        assert uc.reg_read(UC_X86_REG_EIP)==stop
    assert r(0x6fd541b0)==bits(4) and r(0x6fd541b4)==0x3f733334
    w(0x6fd53a48,owner); w(0x6fd68610,registry)
    w(registry+0xc,slots); w(registry+0x1c,1); w(slots,-2,target); w(target+0x18,1)
    w(member+0x14,mover); w(group+0x40,0,1)
    result=[]
    def final(machine,address,size,data):
        sp=machine.reg_read(UC_X86_REG_ESP)
        result.append(r(r(sp+4))); machine.emu_stop()
    uc.hook_add(UC_HOOK_CODE,final,begin=0x6f16fe20,end=0x6f16fe20)
    engine=ctypes.CDLL(str(args.engine_library.resolve()))
    engine.pathing_group_commit_speed.argtypes=[ctypes.POINTER(ctypes.c_uint32)]
    engine.pathing_group_commit_speed.restype=ctypes.c_uint32
    rows=[]; adjusted=0
    # Distances bracket the exact range+4 boundary; oblique sources and elapsed
    # exercise the native predictor, rather than treating pose as committed.
    for flags, unseen, present, requested, limit, vmax, velocity, distance, elapsed in itertools.product(
            (0,0x800,0x1801), (0,1), (False,True), (0,4.6875,10.9375),
            (4.6875,10.9375,struct.unpack('<f',struct.pack('<I',0x7f7fffff))[0]),
            (4.6875,8.4375), ((0,0),(4.6875,0),(0,-4.6875)),
            (bits(0),bits(10),0x4174ffff,0x41750000,0x41750001), (0,.03125)):
        w(group+0x80,flags); w(group+0x6c,unseen); w(group+0x44,1 if present else 2)
        w(member+0x20,bits(requested)); w(cap,bits(limit)); w(mover+0xb0,bits(11.3125))
        w(mover+0x14,0); w(mover+0x70,0,0); w(mover+0x78,0,0,bits(1),bits(-2))
        w(owner+0x14+0x40,bits(elapsed),0,bits(300))
        w(target+0x80,*map(bits,velocity),bits(vmax))
        # Predictor source = (elapsed,-2*elapsed); arithmetic uses dy=0.
        dx=struct.unpack('<f',struct.pack('<I',distance))[0]
        w(dest,bits(elapsed+dx),bits(-2*elapsed))
        inputs=[flags,unseen,int(present),bits(requested),bits(limit),bits(vmax),*map(bits,velocity),
                bits(elapsed),bits(-2*elapsed),r(dest),r(dest+4),bits(11.3125)]
        result.clear(); w(stack,stop,member,dest,cap); uc.reg_write(UC_X86_REG_ESP,stack); uc.reg_write(UC_X86_REG_ECX,group)
        uc.emu_start(0x6f16b5c0,stop,count=20000)
        assert len(result)==1 and uc.reg_read(UC_X86_REG_EIP)==0x6f16fe20
        actual=engine.pathing_group_commit_speed((ctypes.c_uint32*13)(*inputs))
        assert actual==result[0], (inputs,hex(actual),hex(result[0]))
        adjusted+=result[0]!=bits(min(requested,limit))
        rows.append(dict(input=inputs,output=result[0]))
    assert adjusted>0, "oracle must exercise the adjustment branch"
    report=dict(binary_sha256=SHA,passed=True,cases=len(rows),adjusted=adjusted,scope=__doc__)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    if args.fixture: args.fixture.write_text(json.dumps(dict(binary_sha256=SHA,cases=rows,scope=__doc__),separators=(',',':'))+'\n')
    if args.header:
        text='/* Original16b5c0 policy, generated by verify_wc3_pathing_group_speed.py.\n * Constructed inputs; no public-producer or complete motion claim. */\nstatic struct { uint32_t input[13], output; } const retail_group_target_speed[] = {\n'
        for row in rows:
            text+='    {{'+','.join('0x%08xu'%v for v in row['input'])+'},0x%08xu},\n'%row['output']
        args.header.write_text(text+'};\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__': main()
