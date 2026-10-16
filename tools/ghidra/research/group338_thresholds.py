#!/usr/bin/env python3
"""Execute original Town ranking threshold blocks with supplied scalar inputs.

These are isolated branch tests, not whole-function or producer witnesses.
No original instructions or callees are replaced. Runtime constants are
initialized by the original routines; live ranking calls are recorded separately.
"""
import json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from verify_wc3_pathing_numeric import initialize_runtime_scalars
from research.foot03_spatial_harness_copy import Emu

def bits(value):return struct.unpack('<I',struct.pack('<f',value))[0]
def original(binary):
    e=Emu(binary);X=e.X
    initialize_runtime_scalars(e.uc,e.stack,e.stop)
    for address in (0x6fd3c74c,0x6fd3c7b8,0x6fd3c7c8,0x6fd3c7d8):
        entry=e.r(0x6fa7cdb8+address-0x6fd3c740)
        e.w(address,0xdeadbeef)
        assert e.call(entry)==address and e.esp_after==4
    assert [e.f(p)for p in (0x6fd3c7b8,0x6fd3c7c8,0x6fd3c7d8)]==[100,200,300]
    frame=e.fixture(0x200)+0x100
    def run(start,stop):
        e.uc.reg_write(X.UC_X86_REG_EBP,frame)
        for reg in (X.UC_X86_REG_EAX,X.UC_X86_REG_EBX,X.UC_X86_REG_EDI):e.uc.reg_write(reg,0)
        e.uc.emu_start(start,stop,count=100)
        assert e.uc.reg_read(X.UC_X86_REG_EIP)==stop
        return [e.uc.reg_read(reg)for reg in (X.UC_X86_REG_EBX,X.UC_X86_REG_EDI)]
    health=[];multipliers=[]
    for word in sorted({bits(x)+delta for x in (100,200,300)for delta in (-1,0,1)}):
        e.w(frame+0x10,word);low,high=run(0x6f49e011,0x6f49e04d)
        assert low==0;health.append(dict(word=word,high=high))
    for mobile in (False,True):
        for word in sorted({bits(x)+delta for x in (.2,.4,.6,1,1.5,2)for delta in (-1,0,1)}):
            e.w(frame-0x28,word);e.w(frame-0x2c,int(mobile))
            low,high=run(0x6f49dede,0x6f49df5c)
            multipliers.append(dict(mobile=mobile,word=word,low=low,high=high))
    assert not e.log,'unexpected import'
    return dict(health=health,multipliers=multipliers)

def header(rows):
    result='/* Frozen original Town ranking branch outputs; supplied scalar inputs. */\n'
    result+='static struct { uint32_t word,high; } const group338_health[]={\n'
    result+=''.join(' {0x%08xu,0x%08xu},\n'%(r['word'],r['high'])for r in rows['health'])+'};\n'
    result+='static struct { bool mobile; uint32_t word,low,high; } const group338_multipliers[]={\n'
    result+=''.join(' {%s,0x%08xu,0x%08xu,0x%08xu},\n'%('true'if r['mobile']else'false',r['word'],r['low'],r['high'])for r in rows['multipliers'])+'};\n'
    return result

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--header',type=Path,required=True)
    args=parser.parse_args();rows=original(args.binary)
    args.output.write_text(json.dumps(rows,indent=2)+'\n');args.header.write_text(header(rows))
    print('original branches:',len(rows['health']),len(rows['multipliers']))
