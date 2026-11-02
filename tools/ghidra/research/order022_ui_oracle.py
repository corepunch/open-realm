#!/usr/bin/env python3
"""Execute original6b8cc0 UI fallback and6bccc0 working-selection predicate.

Canonical-head resolution and primary-subgroup membership are explicit query
boundaries. The selector itself and its jump table execute from the SHA-pinned
PE; the selection accumulator runs without replacement callees.
"""
import argparse
import itertools
import json
from pathlib import Path
from supportgoal_ramp_oracle import OriginalRamp

COMMANDS=(0,851971,851972,851973,851983,851986,851990,851993,852131,852600,0xffffffff)


def original(binary):
    from unicorn import UC_HOOK_CODE
    e=OriginalRamp(binary);u,x=e.u,e.X
    order,selection,accumulator=e.output,e.vertices,e.output+0x100
    head_state=primary=0
    def query(machine,address,size,data):
        if address not in (0x6f061320,0x6f2908c0):return
        sp=u.reg_read(x.UC_X86_REG_ESP)
        import struct
        result=order if address==0x6f061320 and head_state==2 else primary if address==0x6f2908c0 else 0
        ret=struct.unpack('<I',u.mem_read(sp,4))[0]
        u.reg_write(x.UC_X86_REG_EAX,result)
        u.reg_write(x.UC_X86_REG_ESP,sp+4+(4 if address==0x6f2908c0 else 0))
        u.reg_write(x.UC_X86_REG_EIP,ret)
    u.hook_add(UC_HOOK_CODE,query)
    rows=[]
    for command,head_state,flags,working,preserve,instant,subgroup,all_working,primary in itertools.product(
            COMMANDS,range(3),(0,4,0x40,0x44,0x140),range(2),range(2),range(2),range(2),range(2),range(2)):
        e.write(e.terrain+0x19c,*([0xffffffff,0xffffffff] if head_state==0 else [1,2]))
        e.write(order+0x20,flags);e.write(e.terrain+0x20,0x40000000 if working else 0)
        u.reg_write(x.UC_X86_REG_EDX,command)
        result=e.call(0x6f6b8cc0,[preserve,instant,subgroup,selection,all_working])
        rows.append([command,head_state,flags,working,preserve,instant,subgroup,all_working,primary,result])
    accumulation=[]
    for count in range(13):
        for mask in range(1<<count):
            e.write(accumulator,1);returns=[]
            for i in range(count):
                e.write(e.terrain+0x20,0x40000000 if mask&(1<<i) else 0)
                u.reg_write(x.UC_X86_REG_EDX,accumulator)
                returns.append(e.call(0x6f6bccc0,[]))
            import struct
            final=struct.unpack('<I',u.mem_read(accumulator,4))[0]
            if final!=int(mask==(1<<count)-1) or returns!=[int(bool(mask&(1<<i))) for i in range(count)]:
                raise ValueError('original selection accumulator differs')
            accumulation.append([count,mask,final])
    return dict(selector=rows,accumulator=accumulation,
                limits='Head resolver and subgroup query are input boundaries, not substituted selector logic.')


def compare_engine(data, root):
    import ctypes, subprocess, tempfile, hashlib
    header=root/'games/warcraft-3/game/skills/s_order_controls.h'
    counts={}
    with tempfile.TemporaryDirectory(prefix='order022-selector-') as temp:
        directory=Path(temp);source=directory/'selector.c'
        source.write_text('#include "games/warcraft-3/game/skills/s_order_controls.h"\n'
            'int selector(unsigned command,unsigned head,unsigned flags,unsigned work,unsigned preserve,'
            'unsigned instant,unsigned subgroup,unsigned all_working,unsigned primary) {'
            'return S_PlayerOrderFallback(command,head==2,!!(flags&0x40),!!work,!!preserve,!!instant,'
            '!!subgroup,!!all_working,!!primary); }\n')
        for optimization in ('-O0','-O2'):
            output=directory/(optimization+'.so')
            subprocess.run(['cc','-std=c11','-shared','-fPIC',optimization,'-Wall','-Wextra','-Werror',
                            '-I',str(root),str(source),'-o',str(output)],check=True)
            library=ctypes.CDLL(str(output));function=library.selector
            function.argtypes=[ctypes.c_uint]*9;function.restype=ctypes.c_int
            for row in data['selector']:
                actual=function(*row[:-1])
                if actual!=row[-1]:raise ValueError('C/native selector mismatch '+repr(row)+' actual='+str(actual))
            counts[optimization]=len(data['selector'])
    return dict(cases=counts,header_sha256=hashlib.sha256(header.read_bytes()).hexdigest())


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True);parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--compare-engine',action='store_true')
    args=parser.parse_args()
    if args.report.exists():parser.error('fresh report required')
    data=original(args.binary)
    if args.compare_engine:data['engine_selector']=compare_engine(data,Path(__file__).resolve().parents[3])
    args.report.write_text(json.dumps(data,indent=2)+'\n')
    print('original selector',len(data['selector']),'selection masks',len(data['accumulator']))
