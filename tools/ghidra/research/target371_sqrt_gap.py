#!/usr/bin/env python3
"""Original-code square-root output gap around point arrival's .49 threshold."""
import argparse,hashlib,json,math,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import verify_wc3_pathing_numeric as numeric


def witness(binary):
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP as SP,UC_X86_REG_EIP as IP,UC_X86_REG_ECX as CX,UC_X86_REG_EDX as DX
    raw=binary.read_bytes()
    assert hashlib.sha256(raw).hexdigest()=='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
    pe=struct.unpack_from('<I',raw,60)[0];opt=pe+24
    base,size=(struct.unpack_from('<I',raw,opt+i)[0]for i in(28,56))
    machine=Uc(UC_ARCH_X86,UC_MODE_32);machine.mem_map(base,(size+4095)&~4095)
    for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
        off=opt+struct.unpack_from('<H',raw,pe+20)[0]+40*i
        rva,count,start=struct.unpack_from('<III',raw,off+12)
        if count:machine.mem_write(base+rva,raw[start:start+count])
    machine.mem_map(0x10000000,0x10000);machine.mem_map(0x20000000,0x10000)
    stack,stop,output,source=0x20008000,0x30000000,0x10000000,0x10000010
    numeric.initialize_runtime_scalars(machine,stack,stop)
    def run(word):
        machine.mem_write(source,struct.pack('<I',word));machine.mem_write(stack,struct.pack('<I',stop))
        machine.reg_write(SP,stack);machine.reg_write(CX,output);machine.reg_write(DX,source)
        machine.emu_start(base+0x71480,stop,count=20000)
        assert machine.reg_read(IP)==stop and machine.reg_read(SP)==stack+4
        assert bytes(machine.mem_read(source,4))==struct.pack('<I',word)
        return struct.unpack('<I',machine.mem_read(output,4))[0]
    # .49 lies in the output interval for input exponent -3. Within this
    # interval the replicated significand's integer square root is monotonic,
    # with exactly these reachable integer values. Pick the first input for
    # every root, rather than sample an arbitrary neighborhood of floats.
    rows=[]
    for root in range(math.isqrt(0x80000000),math.isqrt(0xffffffff)+1):
        frac=max(0,(root*root-0x80000000-255+255)//256)
        while 0x80000000+256*frac+(frac>>15)<root*root:frac+=1
        word=0x3e000000|frac
        replicated=((frac|0x800000)<<8)|(frac>>15)
        assert math.isqrt(replicated)==root
        actual=run(word);assert actual==numeric.square_root(word)
        rows.append([root,word,actual])
    lower_interval_max=run(0x3dffffff);upper_interval_min=run(0x3e800000)
    assert lower_interval_max<0x3efae148<upper_interval_min
    assert all(rows[i-1][2]<=row[2]for i,row in enumerate(rows)if i)
    assert all(row[2]!=0x3efae148 for row in rows)
    below=max(row[2]for row in rows if row[2]<0x3efae148)
    above=min(row[2]for row in rows if row[2]>0x3efae148)
    assert (below,above)==(0x3efae0f7,0x3efae1f7)
    return dict(cases=len(rows),below=below,threshold=0x3efae148,above=above,
                lower_interval_max=lower_interval_max,upper_interval_min=upper_interval_min,
                sha256=hashlib.sha256(json.dumps(rows,separators=(',',':')).encode()).hexdigest())


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    if a.report.exists():p.error('fresh report required')
    result=witness(a.binary);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
