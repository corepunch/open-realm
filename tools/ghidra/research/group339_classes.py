#!/usr/bin/env python3
"""Recover Ensnare/Web inheritance and virtual overrides from the supported DLL.

No execution or mutation. Follow COL/type/hierarchy pointers rather than class
name prefixes; CBuffBar and CAbilityDatabase are not CAbility descendants.
"""
from collections import defaultdict
import hashlib,re,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from extract_wc3_ability_classes import PE,u32,RTTI
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'

def original(binary):
    data=Path(binary).read_bytes()
    if hashlib.sha256(data).hexdigest()!=SHA:raise ValueError('unsupported original DLL')
    pe=PE(data);refs=defaultdict(list)
    begin,end=pe.section(b'.rdata')
    for offset in range(begin,end-3,4):refs[u32(data,offset)].append(offset)
    rows=[]
    for match in RTTI.finditer(data):
        for reference in refs[pe.address(match.start()-8)]:
            col=reference-12
            if data[col:col+12]!=bytes(12):continue
            for pointer in refs[pe.address(col)]:
                v188=u32(data,pointer+4+0x188);v18c=u32(data,pointer+4+0x18c)
                if not (0x6f001000<=v188<0x6fa70000 and 0x6f001000<=v18c<0x6fa70000):continue
                hierarchy=pe.offset(u32(data,col+16));count=u32(data,hierarchy+8)
                if count>100:raise ValueError('unexpected RTTI depth')
                array=pe.offset(u32(data,hierarchy+12));bases=[]
                for i in range(count):
                    base=pe.offset(u32(data,array+i*4));desc=pe.offset(u32(data,base))
                    bases.append(data[desc+8:data.index(b'\0',desc+8)])
                if b'.?AVCAbility@@' not in bases:continue
                if not any(b'.?AVCBuffEnsnare@@' == b for b in bases):continue
                rows.append(dict(name=match[1].decode(),table=hex(pe.address(pointer+4)),v188=hex(v188),v18c=hex(v18c),bases=[b.decode()for b in bases]))
    return rows

if __name__=='__main__':
    import argparse,json
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('binary',type=Path)
    args=parser.parse_args();print(json.dumps(original(args.binary),indent=2))
