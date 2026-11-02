#!/usr/bin/env python3
"""Execute original05b970 arrival normalization slice05bb06..05bb4f.

Supplied stack locals and a world range only; no game callbacks are replaced.
This arithmetic fixture supplements the live complete JASS/AI entry traces.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
# Authored zero/small/boundary/AI ranges plus exponent guards and unordered comparisons.
INPUTS=[0,0x80000000,0x3f800000,0x417ae147,0x417ae148,0x417ae149,0x43480000,0x43fa0000,
        0x7f7fffff,0x00800000,0x027fffff,0x02800000,0x02ffffff,0x03000000,
        0xbf800000,0xc3480000,0x7f800000,0xff800000,0x7fc00001,0xffc00001]


def header(rows):
    lines=['/* Original05bb06..05bb4f supplied arithmetic inputs; not full gameplay. */',
           'static uint32_t const entry191_range_words[][2] = {']
    lines += ['    {0x%08xu,0x%08xu},'%(r['input'],r['output'])for r in rows]
    return '\n'.join(lines+['};',''])


def execute(binary):
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_EBP,UC_X86_REG_ESP,UC_X86_REG_EIP
    data=binary.read_bytes();assert hashlib.sha256(data).hexdigest()==SHA,'unsupported game.dll'
    pe=struct.unpack_from('<I',data,60)[0];opt=pe+24
    base,size=[struct.unpack_from('<I',data,opt+n)[0]for n in (28,56)]
    m=Uc(UC_ARCH_X86,UC_MODE_32);m.mem_map(base,(size+4095)&~4095)
    m.mem_write(base,data[:struct.unpack_from('<I',data,opt+60)[0]])
    for i in range(struct.unpack_from('<H',data,pe+6)[0]):
        s=opt+struct.unpack_from('<H',data,pe+20)[0]+40*i
        va,count,offset=struct.unpack_from('<III',data,s+12)
        if count:m.mem_write(base+va,data[offset:offset+count])
    m.mem_map(0x20000000,0x10000);frame=0x20008000;source=0x20001000
    rows=[]
    for word in INPUTS:
        m.mem_write(source,struct.pack('<I',word));m.mem_write(frame+0x24,struct.pack('<I',source))
        m.reg_write(UC_X86_REG_EBP,frame);m.reg_write(UC_X86_REG_ESP,frame-0x100)
        m.emu_start(0x6f05bb06,0x6f05bb4f,count=100)
        assert m.reg_read(UC_X86_REG_EIP)==0x6f05bb4f,'slice incomplete'
        assert bytes(m.mem_read(source,4))==struct.pack('<I',word),'input mutated'
        rows.append(dict(input=word,output=struct.unpack('<I',m.mem_read(frame+0xc,4))[0]))
    return dict(binary_sha256=SHA,entry='6f05bb06',end='6f05bb4f',cases=rows)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--fixture',type=Path,required=True)
    p.add_argument('--header',type=Path,required=True);p.add_argument('--write',action='store_true');a=p.parse_args()
    result=execute(a.binary);text=header(result['cases'])
    if a.write:a.fixture.write_text(json.dumps(result,indent=2)+'\n');a.header.write_text(text)
    else:
        assert json.loads(a.fixture.read_text())==result,'fixture differs from original execution'
        assert a.header.read_text()==text,'engine fixture differs from original execution'
    a.output.write_text(json.dumps(dict(status="bounded-original-point-entry-range",passed=True,arithmetic_cases=len(result['cases']),binary_sha256=SHA),indent=2)+'\n')


if __name__=='__main__':main()
