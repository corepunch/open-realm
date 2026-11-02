#!/usr/bin/env python3
"""Execute original170080's complete held-record and endpoint-mode lifecycle.

Prediction, footprint verdict, placement verdict and position publication are
controlled boundaries. This proves scope ordering, not placement geometry or
notification reentrancy. Public Stop captures remain a separate contract.
"""
import argparse
import hashlib
import itertools
import json
import struct
from pathlib import Path

SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def validate_report(report):
    if not report['passed'] or report['binary_sha256']!=SHA or report['case_count']!=48:
        raise ValueError('incomplete recovery evidence')
    keys=[(r['exit'],r['cls'],r['outer'],r['absent'])for r in report['cases']]
    if len(keys)!=48 or set(keys)!=set(itertools.product(('clear','admitted','exhausted'),range(4),range(2),range(2))):
        raise ValueError('recovery exit matrix differs')
    for row in report['cases']:
        held=row['outer']+(not row['absent'])
        kinds=['footprint']+([]if row['exit']=='clear'else['placement'])+(['commit']if row['exit']=='admitted'else[])
        if [r[0]for r in row['trace']]!=kinds or any(r[1]!=held for r in row['trace']):
            raise ValueError('recovery hold does not span publication')
        if row['final_counter']!=row['outer']or row['final_mode']!=7:
            raise ValueError('recovery scope leaked')
    return report


def verify(binary):
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_ECX,UC_X86_REG_EAX
    raw=binary.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=SHA:raise ValueError('unsupported original executable')
    pe=struct.unpack_from('<I',raw,60)[0];opt=pe+24
    base,size=(struct.unpack_from('<I',raw,opt+o)[0]for o in(28,56))
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
    for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
        s=opt+struct.unpack_from('<H',raw,pe+20)[0]+40*i
        va,n,off=struct.unpack_from('<III',raw,s+12)
        if n:u.mem_write(base+va,raw[off:off+n])
    u.mem_map(0,4096);u.mem_map(0x10000000,0x10000);u.mem_map(0x20000000,0x10000)
    mover,record,system=0x10000000,0x10001000,0x10002000
    stack,stop=0x20008000,0x30000000
    def write(p,*words):u.mem_write(p,struct.pack('<'+'I'*len(words),*(w&0xffffffff for w in words)))
    def read(p,n=1):return list(struct.unpack('<'+'I'*n,u.mem_read(p,n*4)))
    def ret(purge,value=0):
        sp=u.reg_read(UC_X86_REG_ESP);u.reg_write(UC_X86_REG_EAX,value)
        u.reg_write(UC_X86_REG_EIP,read(sp)[0]);u.reg_write(UC_X86_REG_ESP,sp+4+purge)
    for entry in(0x6f001dd0,0x6f001a80,0x6f001b80):
        write(stack,stop);u.reg_write(UC_X86_REG_ESP,stack);u.emu_start(entry,stop,count=1000)
        if u.reg_read(UC_X86_REG_ESP)!=stack+4:raise ValueError('initializer ABI differs')
    trace=[];policy={}
    source=list(struct.unpack('<II',struct.pack('<ff',20.25,20.75)))
    admitted=list(struct.unpack('<II',struct.pack('<ff',19.5,20.5)))
    def hook(uc,address,size,data):
        sp=uc.reg_read(UC_X86_REG_ESP)
        if address==0x6f15aff0:ret(0,system)
        elif address==0x6f161040:
            out=read(sp+4)[0];write(out,0);ret(4,out)
        elif address==0x6f05bdd0:
            out=read(sp+4)[0];write(out,*source);ret(8,out)
        elif address==0x6f149370:
            trace.append(['footprint',read(record+0x40)[0],read(system+0xd4)[0],read(sp+12)[0]])
            ret(12,int(policy['exit']=='clear'))
        elif address==0x6f14a1e0:
            trace.append(['placement',read(record+0x40)[0],read(system+0xd4)[0],read(sp+24)[0]])
            if policy['exit']=='admitted':write(read(sp+4)[0],*admitted)
            ret(36,int(policy['exit']=='admitted'))
        elif address==0x6f05c820:
            point=read(sp+4)[0]
            trace.append(['commit',read(record+0x40)[0],read(system+0xd4)[0],*read(point,2)])
            # Publish new bounds through the controlled position boundary while
            # retaining the original record identity and its held counter.
            write(record+0x20,19,20,20,21);ret(8)
    u.hook_add(UC_HOOK_CODE,hook)
    rows=[]
    for exit,cls,outer,absent in itertools.product(('clear','admitted','exhausted'),range(4),range(2),range(2)):
        policy['exit']=exit;trace.clear()
        write(mover+0x90,struct.unpack('<I',struct.pack('<f',.25+cls*.5))[0])
        write(mover+0x98,0 if absent else record);write(record+0x40,outer)
        write(record+0x20,20,20,21,21);write(system+0xd4,7);write(0,0)
        write(stack,stop,0x02000002,5,0,0,2)
        u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_ECX,mover)
        u.emu_start(0x6f170080,stop,count=10000)
        held=outer+(0 if absent else 1)
        expected=[['footprint',held,1,cls]]
        if exit!='clear':expected.append(['placement',held,7,5])
        if exit=='admitted':expected.append(['commit',held,7,*admitted])
        if trace!=expected:raise ValueError('recovery scope trace differs: '+repr((exit,cls,outer,absent,trace,expected)))
        if u.reg_read(UC_X86_REG_EIP)!=stop or u.reg_read(UC_X86_REG_ESP)!=stack+24:
            raise ValueError('recovery RET14 ABI differs')
        if read(record+0x40)!=[outer]or read(system+0xd4)!=[7]or read(0)!=[0]:
            raise ValueError('captured record/mode/SEH restoration differs')
        result=u.reg_read(UC_X86_REG_EAX)
        if result!=int(exit!='clear'):raise ValueError('blocked-source return differs')
        rows.append(dict(exit=exit,cls=cls,outer=outer,absent=absent,trace=trace.copy(),
                         result=result,final_counter=outer,final_mode=7,bounds=read(record+0x20,4)))
    return dict(passed=True,binary_sha256=SHA,cases=rows,case_count=len(rows),
                scope='Complete original170080, controlled prediction/footprint/placement/publication boundaries; four footprints, null self and outer counter. All reachable result exits restore held identity, endpoint mode and SEH.')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--fixture',type=Path);args=ap.parse_args()
    actual=validate_report(verify(args.binary))
    if args.fixture and actual!=json.loads(args.fixture.read_text()):raise ValueError('frozen recovery scope differs')
    args.output.write_text(json.dumps(actual,indent=2)+'\n')
    print(json.dumps({k:v for k,v in actual.items()if k!='cases'}))


if __name__=='__main__':main()
