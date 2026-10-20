#!/usr/bin/env python3
"""Execute retail49d050's swing producer with explicit boundary stand-ins.

The effective-divisor getter, remaining-time virtual and request allocator are
stand-ins. StatDivide3f15b0, scalar arithmetic and all clamp/arm decisions execute
original instructions. This is not a live scheduler or public-order parity test.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verify_wc3_pathing_numeric import bits, initialize_runtime_scalars

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def execute(binary):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EIP
    raw = binary.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == SHA
    pe = struct.unpack_from('<I', raw, 60)[0]; opt = pe + 24
    base, size = (struct.unpack_from('<I', raw, opt+n)[0] for n in (28, 56))
    u = Uc(UC_ARCH_X86, UC_MODE_32); u.mem_map(base, (size+4095)&~4095)
    u.mem_write(base, raw[:struct.unpack_from('<I', raw, opt+60)[0]])
    for i in range(struct.unpack_from('<H', raw, pe+6)[0]):
        section = opt+struct.unpack_from('<H', raw, pe+20)[0]+40*i
        va, n, offset = struct.unpack_from('<III', raw, section+12)
        if n: u.mem_write(base+va, raw[offset:offset+n])
    u.mem_map(0x10000000, 0x10000); u.mem_map(0x20000000, 0x10000)
    ability, vt, query = 0x10000000, 0x10001000, 0x10008000
    stack, stop = 0x20008000, 0x30000000
    def w(a, *v): u.mem_write(a, struct.pack('<%dI'%len(v), *v))
    def r(a): return struct.unpack('<I', u.mem_read(a,4))[0]
    def ret(pop, result=0):
        sp=u.reg_read(UC_X86_REG_ESP)
        u.reg_write(UC_X86_REG_EAX,result)
        u.reg_write(UC_X86_REG_EIP,r(sp)); u.reg_write(UC_X86_REG_ESP,sp+4+pop)
    initialize_runtime_scalars(u, stack, stop)
    assert r(0x6fcd53a4)==bits(.01) and r(0x6fcd53a8)==bits(.02)
    w(ability+0x1d8,vt); w(vt+0x18,query)
    state={}; requests=[]
    def boundary(machine,address,size,data):
        sp=u.reg_read(UC_X86_REG_ESP)
        if address==0x6f498670:
            out=r(sp+4); w(out,state['divisor']); ret(12,out)
        elif address==query:
            assert u.reg_read(UC_X86_REG_ECX)==ability+0x1d8
            out=r(sp+4); w(out,state['remaining']); ret(4,out)
        elif address==0x6f0608d0:
            requests.append([u.reg_read(UC_X86_REG_ECX)-ability,r(sp+8),r(r(sp+4))])
            assert r(sp+12)==ability and r(sp+16)==r(sp+20)==0
            ret(20)
    for address in (0x6f498670,query,0x6f0608d0):
        u.hook_add(UC_HOOK_CODE,boundary,begin=address,end=address)
    rows=[]
    for slot,backswing,divisor,remaining in itertools.product((0,1),(-1,0,.01,.47,.67,2),(.1,1,1.73,5),(0,.009,.02,.025,.5,1.1)):
        state.update(divisor=bits(divisor),remaining=bits(remaining));requests.clear()
        w(ability+0x2b8,slot); w(ability+0x190+slot*16,bits(backswing))
        w(stack,stop);u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_ECX,ability)
        u.emu_start(0x6f49d050,stop,count=10000)
        assert u.reg_read(UC_X86_REG_ESP)==stack+4
        assert requests[-1][0:2]==[0x200,0xd01b2]
        assert len(requests)==(2 if remaining<.02 else 1)
        if len(requests)==2:assert requests[0]==[0x1d8,0xd01b0,bits(.02)]
        rows.append(dict(slot=slot,backswing=bits(backswing),divisor=bits(divisor),remaining=bits(remaining),requests=list(requests)))
    return dict(binary_sha256=SHA,scope=__doc__,rows=rows)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--fixture',type=Path)
    ap.add_argument('--expected',type=Path)
    ap.add_argument('--header',type=Path)
    args=ap.parse_args();report=execute(args.binary)
    if args.expected:assert report==json.loads(args.expected.read_text()),'original swing witnesses changed'
    if args.fixture:args.fixture.write_text(json.dumps(report,indent=2)+'\n')
    if args.header:
        text='/* Original49d050; explicit getter/query/allocator boundary stand-ins. */\n'
        text+='static struct { uint32_t backswing, divisor, remaining, delay, cooldown; } const retail_attack_swing[] = {\n'
        for row in report['rows']:
            if row['slot']:continue
            cooldown=row['requests'][0][2] if len(row['requests'])==2 else row['remaining']
            text+='    {0x%08xu,0x%08xu,0x%08xu,0x%08xu,0x%08xu},\n'%(row['backswing'],row['divisor'],row['remaining'],row['requests'][-1][2],cooldown)
        args.header.write_text(text+'};\n')
    result=dict(passed=True,status='retail-attack-swing-producer',binary_sha256=SHA,cases=len(report['rows']),scope=__doc__)
    args.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
