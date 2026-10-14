#!/usr/bin/env python3
"""Execute original Mover_StepRoute arrival and scheduler cancellation.

No replaced routines or executable bytes. Controlled queued cache states cover
all player rows and admission buckets, ordinary/forced arrival, empty/single/
multiple fine points, disabled routes and a retained delay. Public production
and task event lifetimes are covered by the companion Frida evidence.
"""
import argparse
import hashlib
import itertools
import json
import struct
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verify_wc3_pathing_numeric import bits, initialize_runtime_scalars

TARGET = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EDX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--fixture', type=Path)
    args = parser.parse_args()
    binary = args.binary.read_bytes()
    if hashlib.sha256(binary).hexdigest() != TARGET:
        parser.error('requires original game.dll 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', binary, opt + n)[0] for n in (28, 56))
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(base, (size + 4095) & ~4095)
    uc.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe + 20)[0] + i * 40
        va, count, offset = struct.unpack_from('<III', binary, section + 12)
        if count:
            uc.mem_write(base + va, binary[offset:offset + count])
    uc.mem_map(0x10000000, 0x20000)
    uc.mem_map(0x20000000, 0x10000)
    stack, stop = 0x20008000, 0x30000000
    mover, path = 0x10001000, 0x10002000
    source, destination, speed, heading, arrived, held, changed = range(0x10003000, 0x10003070, 16)

    def write(address, *words):
        uc.mem_write(address, struct.pack('<' + 'I' * len(words), *(w & 0xffffffff for w in words)))

    def read(address, count=1):
        return list(struct.unpack('<' + 'I' * count, uc.mem_read(address, count * 4)))

    def run(entry, self, arguments, edx=0):
        write(stack, stop, *arguments)
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, self)
        uc.reg_write(UC_X86_REG_EDX, edx)
        uc.emu_start(entry, stop, count=100000)
        assert uc.reg_read(UC_X86_REG_EIP) == stop, 'original caller instruction budget exceeded'
        assert uc.reg_read(UC_X86_REG_ESP) == stack + 4 * (len(arguments) + 1)

    startup = initialize_runtime_scalars(uc, stack, stop)
    calls = {'advance': 0, 'heading': 0, 'update': 0}
    def observe(machine, address, size, userdata):
        calls[{0x6f165ae0:'advance', 0x6f16f630:'heading', 0x6f170880:'update'}[address]] += 1
    for entry in (0x6f165ae0, 0x6f16f630, 0x6f170880):
        uc.hook_add(UC_HOOK_CODE, observe, begin=entry, end=entry)
    rows=[]
    # Each bucket contains before -> tested path -> after. Only the middle
    # node is retired; destination, cache words and charged work are retained.
    previous,following=0x10004000,0x10004100
    fine_data,coarse_data=0x10005000,0x10006000
    for player,kind,forced,count,disabled,delay in itertools.product(
            range(16),range(4),(False,True),(0,1,5),(False,True),(0,4)):
        uc.mem_write(mover,bytes(0x100));uc.mem_write(path,bytes(0x100))
        uc.mem_write(previous,bytes(0x100));uc.mem_write(following,bytes(0x100))
        uc.mem_write(0x6fd53a90,bytes(16*0x70))
        bucket=0x6fd53a90+player*0x70+kind*0x1c
        # Ordinary limit400 selects bucket2; larger limit selects bucket0/1.
        write(0x6fd53ac8+player*0x70,400)
        write(bucket+8,1101,7,3,previous,following)
        write(previous+0x8c,-1,path);write(path+0x8c,previous,following)
        write(following+0x8c,path,-1)
        flags=player<<16 | (0x02000000 if kind==3 else 0) | (0x04000000 if kind==1 else 0)
        flags|=0x100000 if disabled else 0
        write(path+0x84,700|((400 if kind in (2,3) else 5000)<<16))
        write(path+0x88,flags);write(path+0x94,delay,3)
        write(path+0x40,fine_data);write(path+0x4c,5,count)
        write(path+0x60,coarse_data);write(path+0x6c,2,2,(0 if count==1 else count-2) if count else -1,0)
        fine_words=[bits(v) for i in range(5) for v in (12+i,17-i)]
        coarse_words=[bits(v) for v in (8,4,4,4)]
        write(fine_data,*fine_words);write(coarse_data,*coarse_words)
        target=[bits(16 if forced else 8),bits(8)]
        for offset in (0x1c,0x24,0x2c):write(path+offset,*target)
        write(mover+4,1);write(mover+0xa8,path);write(mover+0xd8,0x10000 if forced else 0)
        write(mover+0xb0,bits(.49),bits(.25),bits(.5),bits(.25))
        write(source,bits(8),bits(8));write(destination,*target)
        write(speed,bits(2));write(heading,0);write(arrived,0);write(held,0);write(changed,0)
        before=read(path,0x100//4)
        run(0x6f16fbd0,mover,[source,destination,speed,heading,arrived,held,changed,0,0])
        after=read(path,0x100//4)
        expected=before.copy();expected[0x88//4]&=~0x02000000
        expected[0x8c//4]=expected[0x90//4]=0
        assert after==expected,(player,kind,forced,count,disabled,delay)
        assert read(fine_data,10)==fine_words and read(coarse_data,4)==coarse_words
        assert read(bucket+8,5)==[1101,7,2,previous,following]
        assert read(previous+0x8c,2)==[0xffffffff,following]
        assert read(following+0x8c,2)==[previous,0xffffffff]
        assert read(mover+0xd8)==[0]
        assert read(arrived)==[1] and read(speed)==[0] and read(heading)==[0]
        assert read(destination,2)==target and read(changed)==[0]
        rows.append(dict(player=player,bucket=kind,forced=forced,count=count,
                         disabled=disabled,delay=delay,path=after,
                         bucket_state=read(bucket,7),outputs=[read(a)[0] for a in (speed,heading,arrived,held,changed)]))
    assert calls==dict.fromkeys(calls,0),calls
    payload=dict(version=1,binary_sha256=TARGET,entry='6f16fbd0',rows=rows)
    if args.fixture:args.fixture.write_text(json.dumps(payload,separators=(',',':'))+'\n')
    if args.reference:assert payload==json.loads(args.reference.read_text())
    report=dict(passed=True,cases=len(rows),calls=calls,binary_sha256=TARGET,
                sequence_sha256=hashlib.sha256(json.dumps(rows,separators=(',',':')).encode()).hexdigest())
    args.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':main()
