#!/usr/bin/env python3
"""Original complete script-timer scalar getters, with external running/paused state.

Supplied objects and original vtable entries are fixture inputs, never code
stubs. Public input reachability/actual scheduling are separate live evidence.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
from pathlib import Path
import struct
from verify_wc3_pathing_numeric import initialize_runtime_scalars,subtract,bits

HASH='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'

def main():
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--engine-library',type=Path);p.add_argument('--boundaries',action='store_true');p.add_argument('--fixture',type=Path);a=p.parse_args()
    raw=a.binary.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=HASH:p.error('requires retail game.dll1.27.1.7085')
    pe=struct.unpack_from('<I',raw,0x3c)[0];opt=pe+24
    base,size=(struct.unpack_from('<I',raw,opt+n)[0]for n in (28,56))
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
    u.mem_write(base,raw[:struct.unpack_from('<I',raw,opt+60)[0]])
    for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
        section=opt+struct.unpack_from('<H',raw,pe+20)[0]+40*i
        va,n,offset=struct.unpack_from('<III',raw,section+12)
        if n:u.mem_write(base+va,raw[offset:offset+n])
    u.mem_map(0x10000000,0x10000);u.mem_map(0x20000000,0x10000)
    timer,request,clock,vt,out,stack,stop=0x10000000,0x10001000,0x10002000,0x10003000,0x10004000,0x20008000,0x30000000
    startup=initialize_runtime_scalars(u,stack,stop)
    def write(addr,*words):u.mem_write(addr,struct.pack('<%dI'%len(words),*(w&0xffffffff for w in words)))
    def read(addr):return struct.unpack('<I',u.mem_read(addr,4))[0]
    preserved=[UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP]
    # The virtual clock methods observed in live116. Unused entries remain zero.
    write(vt,0,0,0,0,0x6f0605c0,0x6f0607d0,0x6f060ad0,0x6f060630)
    cases=[];engine=ctypes.CDLL(str(a.engine_library.resolve()))if a.engine_library else None
    if engine:engine.pathing_subtract.argtypes=[ctypes.c_uint32]*2;engine.pathing_subtract.restype=ctypes.c_uint32
    words=[0,0x80000000,bits(-.1),bits(.0001),0x3dcccccd,0x3dccccce,bits(1),bits(300),bits(1000000.25)]
    for timeout,remaining,state in itertools.product(words,words,('no-request','cancelled','running')):
        u.mem_write(timer,bytes(0x100));u.mem_write(request,bytes(0x40));u.mem_write(clock,bytes(0x60))
        write(timer+0x24,vt);write(timer+0x30,0 if state=='no-request'else request)
        write(timer+0x48,timeout);write(timer+0x50,remaining)
        write(request+4,remaining,timeout,clock);write(request+0x10,0x10000 if state=='cancelled'else 0)
        write(timer+0x24+0xc,request if state=='running'else 0)
        effective_remaining=subtract(remaining,0)if state=='running'else remaining
        expected=[timeout,subtract(timeout,effective_remaining),effective_remaining]
        for index,entry in enumerate((0x6f233d50,0x6f232500,0x6f233190)):
            before=bytes(u.mem_read(timer,0x100));req=bytes(u.mem_read(request,0x40));clk=bytes(u.mem_read(clock,0x60))
            write(out-4,0x1234abcd,0xdeadbeef,0x9876abcd);write(stack,stop,out)
            u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_ECX,timer)
            for i,r in enumerate(preserved):u.reg_write(r,0x12120000+i)
            u.emu_start(entry,stop,count=50000)
            assert u.reg_read(UC_X86_REG_EIP)==stop and u.reg_read(UC_X86_REG_ESP)==stack+8
            assert u.reg_read(UC_X86_REG_EAX)==out
            assert [u.reg_read(r)for r in preserved]==[0x12120000+i for i in range(4)]
            assert read(out-4)==0x1234abcd and read(out+4)==0x9876abcd
            assert bytes(u.mem_read(timer,0x100))==before and bytes(u.mem_read(request,0x40))==req and bytes(u.mem_read(clock,0x60))==clk
            assert read(out)==expected[index],(state,index,hex(timeout),hex(remaining),hex(read(out)),hex(expected[index]))
        if engine:assert engine.pathing_subtract(timeout,effective_remaining)==expected[1]
        cases.append(dict(timeout=timeout,remaining=remaining,state=state,output=expected))
    report=dict(binary_sha256=HASH,passed=True,cases=len(cases),getter_calls=3*len(cases),engine_subtraction_cases=len(cases)if engine else 0,
        startup=startup,scope='Whole original233d50/232500/233190 including virtual running-clock methods, unsigned word transport, cancellation and no-request fallback. Supplied single-span clock/request backing; no public scheduler, pause/resume or epoch claim.',engine_scope='C ABI compares the software subtraction helper only; actual public timeout and movement parity are separate full-game regressions. No full engine elapsed/remaining getter claim.',matrix=cases)
    if a.boundaries:
        if not a.fixture:p.error('--boundaries requires the frozen public epoch fixture')
        f=json.loads(a.fixture.read_text());assert f['version']==2
        def call(entry,this=0,args=(),cleanup=0):
            write(stack,stop,*args);u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_ECX,this)
            for i,r in enumerate(preserved):u.reg_write(r,0x12120000+i)
            u.emu_start(entry,stop,count=200000)
            assert u.reg_read(UC_X86_REG_EIP)==stop and u.reg_read(UC_X86_REG_ESP)==stack+4+cleanup
            assert [u.reg_read(r)for r in preserved]==[0x12120000+i for i in range(4)]
            return u.reg_read(UC_X86_REG_EAX)
        original=bytes(u.mem_read(0x6fd3c890,12));write(0x6fd3c894,0xdeadbeef)
        assert call(0x6f002170)==0x6fd3c894 and read(0x6fd3c894)==0x42f00000
        assert bytes(u.mem_read(0x6fd3c890,4))==original[:4] and bytes(u.mem_read(0x6fd3c898,4))==original[8:]
        config,heap,periodptr=0x10006000,0x10005000,0x10007000
        u.mem_map(0,0x1000) # Native SEH head; never a code/callback substitution.
        def setup(time):
            u.mem_write(timer,bytes(0x100));u.mem_write(clock,bytes(0x60));u.mem_write(config,bytes(0x60));u.mem_write(heap,bytes(0x800))
            write(0x6fd3c82c,config);write(config+0x40,clock,clock)
            write(clock+0x10,heap);write(clock+0x1c,256,1);write(clock+0x40,time,0,bits(300))
            for i in range(128):write(request+i*0x40,request+(i+1)*0x40 if i<127 else 0)
            write(clock+0x38,request)
            write(timer+0x24,vt)
        initial={}
        for e in f['events']:
            if e['event']=='timer-getter' and e['actor']<13:initial.setdefault((e['actor'],e['name']),e['word'])
        prepared=[]
        for actor,timeout in enumerate(f['timeout_words']):
            setup(0);write(periodptr,timeout);write(timer+0x48,timeout)
            call(0x6f0602a0,timer+0x24,(0,periodptr,0x80204,0,0),20)
            q=read(timer+0x30);assert q==request+4 and read(clock+0x20)==2
            for name,entry in zip(('timeout','elapsed','remaining'),(0x6f233d50,0x6f232500,0x6f233190)):
                assert call(entry,timer,(out,),4)==out and read(out)==initial[actor,name],(actor,name,read(out),initial[actor,name])
            prepared.append(dict(timeout=timeout,segments=struct.unpack('<H',u.mem_read(timer+0x38,2))[0],residual=read(timer+0x3c),deadline=read(q+4),period=read(q+8)))
        # Whole original heap operations, including ties and unsigned serial wrap.
        import random
        rng=random.Random(0x04f830);heap_cases=0;rebase_cases=0
        for n in (1,2,3,12,63,127):
            setup(0);inputs=[]
            for i in range(n):
                q=request+i*0x40+4;deadline=bits(rng.choice((0,.0001,.1,1,120,299.99997,300,301)))
                serial=rng.randrange(0x100000000);write(q+4,deadline);write(q+0x14,serial)
                call(0x6f04f830,clock+4,(q,),4);inputs.append((deadline,serial,q))
            ordered=sorted(inputs,key=lambda x:(struct.unpack('<f',struct.pack('<I',x[0]))[0],x[1]))
            for _,_,q in ordered:assert call(0x6f04f1a0,clock+4)==q;heap_cases+=1
            assert read(clock+0x20)==1
        for time in (bits(299.99997),bits(300)):
            setup(time)
            for i,timeout in enumerate(f['timeout_words']):
                control=0x10008000+i*0x40;u.mem_write(control,bytes(0x40));write(control,vt);write(periodptr,timeout)
                call(0x6f0602a0,control,(0,periodptr,0x80204,0,0),20)
            before=[read(read(heap+4*i)+4)for i in range(1,read(clock+0x20))]
            call(0x6f052170,clock);assert read(clock+0x44)==1
            after=[read(read(heap+4*i)+4)for i in range(1,read(clock+0x20))]
            assert after==[subtract(w,bits(300))for w in before];rebase_cases+=len(before)
        report.update(segment_initializer=dict(entry='6f002170',target='6fd3c894',word=0x42f00000),
            construction_cases=len(prepared),construction=prepared,heap_cases=heap_cases,rebase_cases=rebase_cases,
            boundary_scope='Original registered120 initializer, complete0602a0 construction with preallocated external heap/free-list backing, public core getter words from native117 initial states, complete04f830/04f1a0 unsigned serial ties and052170 software deadline rebase. No stubs. Engine gameplay/getter/save parity is separately tested; general public callback catch-up remains open.')
    a.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k]for k in ('passed','cases','getter_calls','engine_subtraction_cases')}))
if __name__=='__main__':main()
