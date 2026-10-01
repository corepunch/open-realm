#!/usr/bin/env python3
"""Original accelerated route lookahead and ordinary index transitions.

Synthetic preallocated routes; no stubs or shipped retail code. Cardinal
power-of-two edge lengths isolate control flow from approximate square roots.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import struct
from pathlib import Path


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EBP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--engine-library',type=Path,help='compare the production coarse selector against executed original results')
    args = parser.parse_args()
    engine=ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:engine.pathing_acc_selection.argtypes=[ctypes.POINTER(ctypes.c_uint32)]*3
    binary = args.binary.read_bytes()
    digest = hashlib.sha256(binary).hexdigest()
    if digest != 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':
        parser.error('unsupported binary; requires game.dll 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', binary, opt + offset)[0] for offset in (28, 56))
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (size + 4095) & ~4095)
    machine.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
        va, count, offset = struct.unpack_from('<III', binary, section + 12)
        if count:
            machine.mem_write(base + va, binary[offset:offset + count])

    machine.mem_map(0x10000000, 0x20000)
    machine.mem_map(0x20000000, 0x10000)
    system, output = 0x10000000, 0x10000800
    stack, stop = 0x20008000, 0x30000000

    def write(address, *values):
        machine.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def read(address, count=1):
        return list(struct.unpack('<' + 'I' * count, machine.mem_read(address, count * 4)))

    def run(entry, self, *arguments):
        write(stack, stop, *arguments)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, self)
        machine.emu_start(entry, stop, count=100000)
        if machine.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError('retail route transition exceeded instruction budget')

    path, points, marker, point, goal = [system+n for n in (0x1000,0x2000,0x3000,0x4000,0x5000)]
    machine.mem_write(0x6fd3c740,struct.pack('<4f',-1,0,1,2))
    # Run the actual scalar initializer: integer10 -> software float threshold.
    run(0x6f0040d0,0)
    threshold=struct.unpack('<f',machine.mem_read(0x6fd54194,4))[0]
    assert threshold==10
    sentinel=-128000.0078125
    machine.mem_write(0x6fd53a74,struct.pack('<f',sentinel))
    write(path+0x60,points)
    selectors=consumers=progress=acceptance=fine_advance=0
    for count, step, zigzag in itertools.product(range(13),[1,2,4,8],[False,True]):
        route=[(32+(n%2 if zigzag else n)*step,32) for n in range(13)]
        for warp in [None]+list(range(count)):
            data=list(route)
            if warp is not None: data[warp]=(sentinel,7)
            machine.mem_write(points,struct.pack('<26f',*(v for xy in data for v in xy)))
            for mode in [0,1]:
                expected=0
                hit=0
                length=0
                for index in range(count-1,0,-1):
                    if index==warp:
                        hit=1
                        expected=index-1 if mode else index+1
                        break
                    length+=step
                    if length>=10:
                        expected=index
                        break
                write(path+0x78,count)
                write(marker,0xdeadbeef)
                run(0x6f167ae0,path,marker,mode)
                assert read(marker)[0]==hit,(count,step,warp,mode)
                assert machine.reg_read(UC_X86_REG_EAX)==expected,(count,step,warp,mode)
                assert read(path+0x78)[0]==count
                if engine:
                    inputs=(ctypes.c_uint32*2)(count,mode)
                    words=(ctypes.c_uint32*26)(*struct.unpack('<26I',struct.pack('<26f',*(v for xy in data for v in xy))))
                    out=(ctypes.c_uint32*2)()
                    engine.pathing_acc_selection(inputs,words,out)
                    assert list(out)==[machine.reg_read(UC_X86_REG_EAX),read(marker)[0]]
                selectors+=1
                # Full consumer without a pending gate, or with traversal disabled.
                if mode==0:
                    for allow in ([0,1] if warp is None else [0]):
                        write(path+0x78,count)
                        write(marker,0xdeadbeef)
                        run(0x6f165d10,path,allow,marker)
                        assert machine.reg_read(UC_X86_REG_EAX)==1
                        assert read(path+0x78)[0]==expected
                        assert read(marker)[0]==0
                        consumers+=1
                    if warp is None:
                        for force,distance in itertools.product([0,1],[0,0.25,0.489,0.491,1,4]):
                            machine.mem_write(point,struct.pack('<2f',route[count][0]+distance,32))
                            write(path+0x74,7,count)
                            write(path+0x94,7,9)
                            run(0x6f165f10,path,point,force)
                            accepted=count!=0 and (force or distance<0.49)
                            assert machine.reg_read(UC_X86_REG_EAX)&0xff==0
                            assert read(path+0x74,2)==([0xffffffff,expected] if accepted else [7,count])
                            assert read(path+0x94,2)==[7,9]
                            acceptance+=1
                    if warp is None and count:
                        # Reached fine waypoint, remaining accelerated route.
                        for delay in [0,7,25]:
                            machine.mem_write(point,struct.pack('<2f',8,8))
                            machine.mem_write(goal,struct.pack('<2f',8,8))
                            write(path+0x74,0,count)
                            write(path+0x88,0x30100000)
                            write(path+0x94,delay,9)
                            run(0x6f167070,path,point,goal)
                            assert machine.reg_read(UC_X86_REG_EAX)==2
                            assert read(path+0x74,2)==[0xffffffff,expected]
                            assert read(path+0x94,2)==[delay,9]
                            assert read(path+0x88)[0]==0x30100000
                            progress+=1
                            write(path+0x40,point)
                            write(path+0x50,1)
                            write(path+0x74,0,count)
                            machine.mem_write(goal,struct.pack('<2f',100,100))
                            run(0x6f165c60,path,point,goal)
                            assert machine.reg_read(UC_X86_REG_EAX)==2
                            assert read(path+0x74,2)==[0xffffffff,expected]
                            assert read(path+0x50)[0]==1
                            assert read(path+0x94,2)==[delay,9]
                            assert read(path+0x88)[0]==0x30100000
                            assert read(goal,2)==read(point,2)
                            fine_advance+=1
    report=dict(binary_sha256=digest,scope=__doc__,passed=True,
                threshold_acc=threshold,selector_cases=selectors,engine_selector_cases=selectors if engine else 0,
                consumer_cases=consumers,fine_progress_transition_cases=progress,full_waypoint_acceptance_cases=acceptance,full_fine_transition_cases=fine_advance)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
