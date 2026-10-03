#!/usr/bin/env python3
"""Original next-step blocker collector: real lazy-chain traversal and append.

Synthetic cells/objects, preallocated vector, no stubs or retail bytes.
Checks null static-blocker tokens, eligibility, stamps, ordering and 32 cap.
Composes next-step and fine-advance decisions, including real retry initialization.
"""
import argparse
import hashlib
import itertools
import json
import math
import random
import struct
from pathlib import Path


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EBP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
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
            raise RuntimeError('retail target lookup exceeded instruction budget')

    fine, grid, cells, links, vector, entries = [system+n for n in (0x1000,0x2000,0x3000,0x4000,0x5000,0x6000)]
    shapes=[system+0x8000+n*0x80 for n in range(40)]
    movers=[system+0xc000+n*0x40 for n in range(40)]
    write(fine+0x1c,grid)
    write(fine+0xa4,0x02000001)
    write(grid+0x28,cells)
    write(grid+0x3c,16,16)
    write(grid+0x78,links)
    write(vector+0xc,entries)
    write(vector+0x14,0,64)
    rng=random.Random(7085)
    cases=0
    for case in range(1024):
        initial=[0,1,31,32,40][case%5]
        expected=[0xdead0000+n for n in range(initial)]
        write(entries,*expected)
        write(vector+0x1c,initial)
        write(fine+0xd4,case%2)
        stamps=[0xffffffff if n%11==0 else 0 for n in range(40)]
        masks=[0x01000001 if n%5 else 0x01000002 for n in range(40)]
        flags=[0,1,0x10000000,0x20000000,0x40000000,0x80000000]
        object_flags=[flags[(n+case)%len(flags)] for n in range(40)]
        payloads=[0 if n%7==0 else movers[n] for n in range(40)]
        for n,obj in enumerate(shapes):
            write(obj+0x30,payloads[n],masks[n],stamps[n])
            write(obj+0x40,object_flags[n])
            write(movers[n]+0x10,0x60706375 if n%3 else 0)
        chain=[(rng.randrange(3),rng.randrange(40)) for _ in range(rng.randrange(81))]
        if case%16==0:
            chain=[(1,n) for n in range(40)]
            for n,obj in enumerate(shapes):
                stamps[n]=0; masks[n]=0x01000001; object_flags[n]=0
                write(obj+0x34,masks[n],stamps[n])
                write(obj+0x40,0)
        for index,(kind,obj) in enumerate(chain):
            write(links+index*8,kind<<24 | (index+1 if index+1<len(chain) else 0xffffff),shapes[obj])
        terrain=0x02000000 if case%9==0 else 0
        head=terrain | (0 if chain else 0xffffff)
        write(cells+(8*16+8)*4,head)
        write(cells+(8*16+9)*4,head)
        counter=100
        write(grid+0xb4,counter)
        # Repeated collection crosses cell stamps; duplicates are not globally removed.
        for x,y in [(8,8),(9,8),(-1,8)]:
            if x<0 or terrain:
                if len(expected)<32: expected.append(0)
            elif chain:
                counter+=1
                for kind,n in chain:
                    if kind==2 or stamps[n]==0xffffffff or not (masks[n]&0x01000000) or stamps[n]==counter:
                        continue
                    stamps[n]=counter
                    if kind==1 and not(object_flags[n]&0x8fffffff) and masks[n]&1 and len(expected)<32:
                        expected.append(payloads[n] if payloads[n] and n%3 else 0)
            run(0x6f148ad0,fine,x,y,vector)
            assert read(vector+0x1c)[0]==len(expected),(case,x)
            assert read(entries,len(expected))==expected,(case,x)
            assert read(grid+0xb4)[0]==counter,(case,x)
            assert [read(obj+0x38)[0] for obj in shapes]==stamps,(case,x)
            cases+=1
    # Compose the real next-step wrapper, collector and yielding resolver.
    # Class0 cardinal steps isolate the decision from larger footprint geometry.
    machine.mem_map(0,0x1000)  # original x86 SEH chain at FS:[0]
    machine.mem_write(0x6fd3c740,struct.pack('<4f',-1,0,1,2))
    path, peer_path, self_mover, peer, group_a, group_b, registry, slots, point, goal = [
        system+n for n in (0x10000,0x10200,0x10400,0x10600,0x10800,0x10a00,0x10c00,0x10d00,0x11000,0x11100)]
    write(0x6fd68610,registry)
    write(registry+0xc,slots)
    write(registry+0x1c,4)
    for index,obj in enumerate([self_mover,peer,group_a,group_b]):
        write(slots+index*8,-2,obj)
        write(obj+0x14,index,100+index)
    write(self_mover+0xa8,path)
    write(peer+0xa8,peer_path)
    write(peer+0x10,0x60706375)
    write(self_mover+0x9c,2,102)
    write(0x6fd53a80,0,fine,grid,self_mover)
    write(fine+0xac+0xc,entries)
    write(fine+0xac+0x14,0,64)
    machine.mem_write(point,struct.pack('<2f',8.25,8.25))
    machine.mem_write(goal,struct.pack('<2f',12.25,8.25))
    machine.mem_write(cells,struct.pack('<256I',*([0xffffff]*256)))
    composed=0
    advance_cases=0
    route_point=system+0x11200
    machine.mem_write(route_point,struct.pack('<2f',12.25,8.25))
    machine.mem_write(0x6fd54190,struct.pack('<f',144))
    write(group_a+0x38,1)
    for kind, speed, peer_speed, group, flags, prior in itertools.product(
            ['clear','terrain','self','peer','static','masked','suppressed'],
            [0,1,3],[0,1,3],['same','different','none'],[0,8],[0,7]):
        write(path+0x88,0)
        write(peer_path+0x88,0)
        write(path+0x94,prior)
        write(peer_path+0x94,0)
        write(path+0x9c,0x02000001,shapes[0])
        write(path+0xa8,1,101)
        write(peer_path+0xa8,-1,-1)
        write(peer+0x9c,2 if group=='same' else 3 if group=='different' else -1,
              102 if group=='same' else 103)
        write(group_a+0x80,flags)
        machine.mem_write(self_mover+0x80,struct.pack('<2f',speed,0))
        machine.mem_write(peer+0x80,struct.pack('<2f',peer_speed,0))
        write(shapes[0]+0x30,self_mover,0x01000001,0)
        write(shapes[0]+0x40,0)
        write(shapes[1]+0x30,0 if kind=='static' else peer,
              0x01000002 if kind=='masked' else 0x01000001,0)
        write(shapes[1]+0x40,1 if kind=='suppressed' else 0)
        write(links,0x01ffffff,shapes[0] if kind=='self' else shapes[1])
        write(cells+(8*16+9)*4,0xffffff if kind=='clear' else
              0x02ffffff if kind=='terrain' else 0)
        write(grid+0xb4,100)
        # A previous nonempty result must be cleared, not inherited.
        write(fine+0xc8,3)
        write(entries,0xdead0000,0xdead0001,0xdead0002)
        run(0x6f166140,path,point,goal)
        blocked=kind in ['terrain','peer','static']
        eligible=kind=='peer' and group!='none' and peer_speed!=0
        own=eligible and ((group=='same' and not flags&8) or speed<=peer_speed)
        other=eligible and not own
        assert machine.reg_read(UC_X86_REG_EAX)&0xff == int(not blocked),(kind,speed,peer_speed,group,flags)
        assert read(fine+0xc8)[0]==int(blocked)
        if blocked:
            assert read(entries)[0]==(peer if kind=='peer' else 0)
        assert read(path+0xa8,2)==([1,101] if own else [0xffffffff]*2)
        assert read(path+0x94)[0]==(max(prior,4) if own else prior)
        assert read(peer_path+0xa8,2)==([0,100] if other else [0xffffffff]*2)
        assert read(peer_path+0x94)[0]==(20 if other else 0)
        assert read(shapes[0]+0x40)[0]==0
        assert read(fine+0xa4)[0]==0x02000001
        assert read(0)[0]==0
        composed+=1

        # Full fine advance with an existing waypoint, including real retry init.
        write(path+0x40,route_point)
        write(path+0x50,1)
        write(path+0x74,0,0)
        write(path+0x88,0x100000)
        write(path+0x94,prior,9)
        write(path+0xa4,0,1,101)
        machine.mem_write(path+0x24,struct.pack('<2f',12.25,8.25))
        write(peer_path+0x94,0)
        write(peer_path+0xa8,-1,-1)
        write(goal,0x42a00000,0x42c80000)  # caller destination restored on retry
        write(shapes[0]+0x38,0)
        write(shapes[1]+0x38,0)
        write(grid+0xb4,100)
        run(0x6f165c60,path,point,goal)
        retry=blocked and not own
        assert machine.reg_read(UC_X86_REG_EAX)==int(blocked),(kind,group,speed,peer_speed)
        assert read(path+0x94)[0]==(4 if own else 0)
        assert read(peer_path+0x94)[0]==(20 if other else 0)
        assert read(path+0x98)[0]==(1 if retry else 0)
        assert read(path+0x74)[0]==(0xffffffff if retry else 0)
        assert read(path+0x50)[0]==(0 if retry else 1)
        assert read(path+0x88)[0]==0
        assert read(goal,2)==([0x42a00000,0x42c80000] if retry else read(route_point,2))
        assert read(shapes[0]+0x40)[0]==0
        advance_cases+=1
        machine.mem_write(goal,struct.pack('<2f',12.25,8.25))
    # Reaching final fine waypoint with final accelerated index zero enters
    # retry/target handling directly, preserving the existing retry budget.
    terminal_cases=0
    for distance, initial, flags in itertools.product(
            [0,0.125,0.25,0.489],[0,1,2,7,8,0xffffffff],[0,0x100000,0x30100000]):
        machine.mem_write(route_point,struct.pack('<2f',8.25+distance,8.25))
        write(path+0x40,route_point)
        write(path+0x50,1)
        write(path+0x74,0,0)
        write(path+0x88,flags)
        write(path+0x94,7,initial)
        write(path+0xa4,0)
        write(goal,0x42a00000,0x42c80000)
        run(0x6f165c60,path,point,goal)
        terminal=initial==1
        assert machine.reg_read(UC_X86_REG_EAX)==(4 if terminal else 1)
        assert read(path+0x98)[0]==(1 if initial in [0,1,2] else initial-1)
        assert read(path+0x94)[0]==7
        assert read(path+0x74)[0]==(0 if terminal else 0xffffffff)
        assert read(path+0x50)[0]==(1 if terminal else 0)
        assert read(path+0x88)[0]==(flags if terminal else flags&~0x100000)
        assert read(goal,2)==[0x42a00000,0x42c80000]
        terminal_cases+=1

    report=dict(binary_sha256=digest,scope=__doc__,passed=True,collector_calls=cases,sequences=1024,composed_next_step_cases=composed,fine_advance_cases=advance_cases,final_waypoint_cases=terminal_cases)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
