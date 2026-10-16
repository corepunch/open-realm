"""Execute complete Pow/log forwarding/distribution/Shadow Strike callers.

Only Shadow Strike's already independently captured EventClock_GetElapsed query
is supplied as a boundary. Arithmetic, PRNG, reductions and wrappers execute the
original guarded DLL. No VM or lifetime result is inferred from this harness.
"""
import ctypes as C
import hashlib
import json
import struct
from collections import Counter
from verify_wc3_pathing_numeric import bits, initialize_runtime_scalars
from verify_wc3_pathing_power import power, log, exp, exp_positive, corelog, reducedlog


def witness(binary, library):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_REG_ESP as SP, UC_X86_REG_EIP as IP,
        UC_X86_REG_ECX as CX, UC_X86_REG_EDX as DX, UC_X86_REG_EAX as AX,
        UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)
    data=binary.read_bytes()
    if hashlib.sha256(data).hexdigest()!='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':
        raise ValueError('wrong original DLL')
    pe=struct.unpack_from('<I',data,60)[0];opt=pe+24
    base,size=(struct.unpack_from('<I',data,opt+i)[0] for i in (28,56))
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
    for i in range(struct.unpack_from('<H',data,pe+6)[0]):
        s=opt+struct.unpack_from('<H',data,pe+20)[0]+40*i;va,n,off=struct.unpack_from('<III',data,s+12)
        if n:u.mem_write(base+va,data[off:off+n])
    u.mem_map(0x10000000,0x10000);u.mem_map(0x20000000,0x10000)
    out,left,right,rng,buff,vt=range(0x10000100,0x10000700,0x100)
    stack,stop=0x20008000,0x30000000
    def wr(at,*words):u.mem_write(at,struct.pack('<'+'I'*len(words),*(x&0xffffffff for x in words)))
    def rd(at,n=1):
        v=list(struct.unpack('<'+'I'*n,u.mem_read(at,4*n)));return v[0] if n==1 else v
    initialize_runtime_scalars(u,stack,stop)
    from wc3_shipped_crt import load_crt
    crt=load_crt(u,binary.parent/'msvcr120.dll');wr(0x6fa7c4fc,crt['exports']['isdigit'])
    engine=C.CDLL(str(library.resolve()))
    def scalar(name,a,b=0):
        v=(C.c_uint32*2)();getattr(engine,'pathing_'+name)(C.c_uint32(a),C.c_uint32(b),v)
        assert v[0]==1,(name,a,b);return v[1]
    for name in ['add','subtract','multiply','divide','sqrt']:
        getattr(engine,'pathing_'+name).restype=C.c_uint32
    def op(name,a,b=0):
        name={'sub':'subtract','mul':'multiply','div':'divide'}.get(name,name)
        return getattr(engine,'pathing_'+name)(C.c_uint32(a),C.c_uint32(b))
    helpers={0x710e0:('power',power),0x70f70:('log',log),0x70c20:('exp',exp),
             0x6fe10:('exp-quarter',exp_positive),0x6ff20:('reducedlog',reducedlog),0x6fd50:('corelog',corelog)}
    pending=[];relations=Counter();records=[];age=0;operations=0
    def observe(machine,address,size,_):
        nonlocal operations
        while pending and address==pending[-1][0]:
            ret,destination,name,a,b,expected=pending.pop()
            actual=rd(destination);assert actual==expected,(name,hex(a),hex(b),hex(actual),hex(expected))
            if name!='exp-quarter':assert scalar(name,a,b)==actual
            operations+=1
        r=address-base
        if r==0x60630:
            # Explicitly supplied clock-query boundary; full timer witnessed live.
            sp=u.reg_read(SP);dest=rd(sp+4);wr(dest,age)
            u.reg_write(AX,dest);u.reg_write(SP,sp+8);u.reg_write(IP,rd(sp));return
        if r not in helpers:return
        name,model=helpers[r];sp=u.reg_read(SP);dst=u.reg_read(CX);src=u.reg_read(DX)
        a=rd(src);bp=rd(sp+4) if name=='power' else 0;b=rd(bp) if bp else 0
        expected=model(a,b) if name=='power' else model(a)
        relations[(name,hex(rd(sp)-base),dst==src,bool(bp and dst==bp),bool(bp and src==bp))]+=1
        pending.append((rd(sp),dst,name,a,b,expected))
    u.hook_add(UC_HOOK_CODE,observe)
    saved=[UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP]
    def run(r,ecx,edx=0,args=(),purge=0):
        wr(stack,stop,*args)
        u.reg_write(SP,stack);u.reg_write(CX,ecx);u.reg_write(DX,edx)
        for i,reg in enumerate(saved):u.reg_write(reg,0xabc10000+i)
        u.emu_start(base+r,stop,count=300000)
        # Return destination may be stop, beyond code-hook range; finish checks.
        while pending and pending[-1][0]==stop:
            _,dst,name,a,b,expected=pending.pop();assert rd(dst)==expected
            if name!='exp-quarter':assert scalar(name,a,b)==expected
        assert not pending
        assert u.reg_read(IP)==stop and u.reg_read(SP)==stack+4+purge
        assert [u.reg_read(x) for x in saved]==[0xabc10000+i for i in range(4)]
    # Registered original constant initializers used by distribution callers.
    for entry in (0x1de0,0x1c30):run(entry,0)
    assert rd(0x6fd3c73c)==bits(-2) and rd(0x6fd3c74c)==bits(2)
    # Complete visual-heading producer: all16 policy rows, both turn signs.
    run(0x4200,0);run(0x4210,0)
    for policy in range(16):
        for desired in (.025,.075,.15):
            for sign in (-1,1):
                u.mem_write(buff,bytes(0x100));wr(buff+0xc8,bits(1));wr(buff+0x8c,bits(1+sign*desired));wr(buff+0xd8,policy<<8)
                run(0x1705c0,buff);records.append(['visual',policy,bits(1+sign*desired),rd(buff+0xc8),rd(buff+0xcc)])
    # Actual public wrapper owns independent locals, including finite guards.
    from verify_wc3_pathing_power import public
    for a,b in [(bits(2),bits(.5)),(bits(0),bits(0)),(bits(-2),bits(-3)),(bits(.0005),bits(-.5))]:
        wr(left,a);wr(right,b);run(0x20f990,0,args=(left,right))
        actual=u.reg_read(AX);assert actual==public(a,b)==scalar('public_power',a,b)
        records.append(['public',a,b,actual])
    # Transparent wrappers preserve the external producer's ABI relationships;
    # unlike the callers below they can forward a supplied aliased destination.
    for a,b in [(bits(2),bits(.5)),(bits(.125),bits(3)),(bits(15.1),bits(3))]:
        for dst in (out,left,right):
            wr(left,a);wr(right,b);run(0x171500,dst,left,(right,),4)
            assert rd(dst)==power(a,b);records.append(['power-thunk',a,b,dst==left,dst==right,rd(dst)])
    # The remaining equality partitions share base/exponent, then all three.
    for a in map(bits,(.125,.5,2)):
        for dst in (out,left):
            wr(left,a);run(0x171500,dst,left,(left,),4)
            assert rd(dst)==power(a,a)
            records.append(['power-thunk-shared-operands',a,dst==left,rd(dst)])
    for a in map(bits,(.125,.5,1,2,15.1)):
        for dst in (out,left):
            wr(left,a);run(0x1d1de0,dst,left);assert rd(dst)==log(a)
            records.append(['log-thunk',a,dst==left,rd(dst)])
    # Execute all fill wrappers, including zero count and cached Gaussian pair.
    # Engine PRNG + scalar functions compose the same operations independently.
    for seed in (0,1,7085,0xffffffff):
        for kind,entry in [('exp',0x1d1c10),('scaled-exp',0x1d1c50),('normal',0x1d1c90),('scaled-normal',0x1d1cd0)]:
            for count in (0,1,2,5):
                words=(C.c_uint32*2)();engine.pathing_random_seed(C.c_uint32(seed),words);wr(rng,*words)
                wr(0x6fd68780,0);wr(0x6fd6877c,0);wr(left,bits(2.25));wr(right,bits(.375));wr(out-4,0xabc00101)
                wr(out,*([0xdeadbeef]*count),0xabc00102)
                args=(rng,) if kind in ('exp','normal') else (left,rng) if kind=='scaled-exp' else (left,right,rng)
                run(entry,out,count,args,4*len(args))
                cache=0;expected=[]
                def draw():
                    v=(C.c_uint32*2)();inp=(C.c_uint32*3)(0,0,0);engine.pathing_random_query(inp,words,v);return v[0]
                for k in range(count):
                    if kind in ('exp','scaled-exp'):
                        v=scalar('log',op('sub',bits(2),0x3f800000|(draw()&0x7fffff)))^0x80000000
                        if kind=='scaled-exp':v=op('mul',v,bits(2.25))
                    elif cache:v,cache=cache,0
                    else:
                        radius=op('sub',bits(2),0x3f800000|(draw()&0x7fffff))
                        theta=op('mul',op('add',0x3f800000|(draw()&0x7fffff),bits(-1)),0x40c90fdb)
                        pair=(C.c_uint32*2)();engine.pathing_sincos(C.c_uint32(theta),pair)
                        root=op('sqrt',op('mul',bits(-2),scalar('log',radius)))
                        v=op('mul',pair[1],root);cache=op('mul',pair[0],root)
                    if kind=='scaled-normal':v=op('add',op('mul',v,bits(.375)),bits(2.25))
                    expected.append(v)
                actual=rd(out,count) if count>1 else [rd(out)] if count else []
                assert actual==expected,(kind,seed,count,actual,expected)
                assert rd(rng,2)==list(words) and rd(out-4)==0xabc00101 and rd(out+count*4)==0xabc00102
                records.append([kind,seed,count,actual])
    # Full duration-power caller, including fractional exponent (log/exp path),
    # separate outputs, near-zero duration/ratio and retained state protection.
    wr(vt+0x1c,base+0x60630);wr(buff+0x84,vt);wr(buff+0x20,0x80)
    curves=[]
    engine.pathing_decimal.restype=C.c_uint32
    for duration,exponent in [(15.1,3),(5.7,.5),(7.25,2.25),(.0005,3)]:
        dw=engine.pathing_decimal(str(duration).encode())
        for aw in (bits(0),bits(1),bits(2),bits(5),0x3f800008):
            wr(buff+0x13c,dw);wr(buff+0x134,bits(exponent));wr(buff+0x144,bits(1));age=aw
            run(0x502600,buff,args=(out,),purge=4)
            if duration<.001:expected=0
            else:
                den=scalar('power',dw,bits(exponent))
                num=scalar('power',op('sub',age,dw)&0x7fffffff,bits(exponent))
                expected=op('div',num,den)
                if (op('sub',expected,0)&0x7fffffff)<0x3a83126f:expected=0
            assert rd(out)==expected and rd(buff+0x144)==bits(1)
            curves.append([dw,bits(exponent),age,expected])
    return dict(cases=len(records)+len(curves),checked_math_operations=operations,
        relationships=[[*key,count] for key,count in sorted(relations.items())],curves=curves,
        outcomes_sha256=hashlib.sha256(json.dumps(records+curves,separators=(',',':')).encode()).hexdigest())
