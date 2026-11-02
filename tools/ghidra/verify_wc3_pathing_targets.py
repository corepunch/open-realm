#!/usr/bin/env python3
"""Execute original fine searches with collision suppression and target identities."""
import argparse
import ctypes
import hashlib
import itertools
import json
from pathlib import Path
import struct
from verify_wc3_pathing_grid import ObjectInput


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--engine-library', type=Path)
    args = parser.parse_args()
    binary = args.binary.read_bytes(); digest = hashlib.sha256(binary).hexdigest()
    if digest != 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236': parser.error('unsupported game.dll')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]; opt = pe + 24
    base, size = (struct.unpack_from('<I', binary, opt + offset)[0] for offset in (28, 56))
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (size + 4095) & ~4095)
    machine.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
        va, count, offset = struct.unpack_from('<III', binary, section + 12)
        if count: machine.mem_write(base + va, binary[offset:offset + count])
    machine.mem_map(0x10000000, 0x200000); machine.mem_map(0x20000000, 0x10000)
    system, tilemap, cells, bitmap = 0x10000000, 0x10000200, 0x10001000, 0x10002000
    nodes, links, heap = 0x10010000, 0x10020000, 0x10030000
    route, route_data = 0x10100000, 0x10101000
    source_ptr, target_ptr, mask_ptr, radius_ptr = 0x10000400, 0x10000410, 0x10000420, 0x10000430
    stack, stop = 0x20008000, 0x30000000
    for address, value in ((0x6fd3c740, -1.), (0x6fd3c744, 0.), (0x6fd3c748, 1.)):
        machine.mem_write(address, struct.pack('<f', value))
    def write(address, *values): machine.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))
    def read(address, count=1): return list(struct.unpack('<' + 'I' * count, machine.mem_read(address, count * 4)))
    def run(entry, self, *arguments):
        write(stack, stop, *arguments); machine.reg_write(UC_X86_REG_ESP, stack); machine.reg_write(UC_X86_REG_ECX, self)
        machine.emu_start(entry, stop, count=20000000)
        if machine.reg_read(UC_X86_REG_EIP) != stop: raise RuntimeError('target search exceeded instruction budget')
        if machine.reg_read(UC_X86_REG_ESP) != stack + 4 + 4*len(arguments): raise RuntimeError('original target request stack ABI differs')
        return machine.reg_read(UC_X86_REG_EAX)
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine: engine.pathing_fine_target.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ObjectInput), ctypes.POINTER(ctypes.c_int32)]
    records = []; width = height = 24; source = (4.25, 4.75); target = (19.25, 19.75)
    for bounds, terrain, cls, mask, variant in itertools.product(
            ([12,12,13,13], [12,0,14,24]), ('open', 'target_terrain'), range(4),
            (0x02000002, 0x04000004, 0x40000040, 0x80000080),
            ('idle', 'self', 'suppressed', 'target', 'both', 'target_idle', 'unlinked_target',
             'inactive_target', 'moving_target', 'off_lane_target', 'target_behind_idle', 'idle_behind_target')):
        def obj(flags=0, category=0x010000ca, linked=True): return [*bounds, category, flags, int(linked)]
        objects = [obj()]; target_index = 0xffffffff
        if variant in ('self', 'suppressed'): objects = [obj(1)]
        elif variant in ('target', 'both'): objects = [obj(1 if variant == 'target' else 2)]; target_index = 0
        elif variant == 'target_idle': target_index = 0
        elif variant == 'unlinked_target': objects = [obj(1, linked=False)]; target_index = 0
        elif variant == 'inactive_target': objects = [obj(1, 0xca)]; target_index = 0
        elif variant == 'moving_target': objects = [obj(0x20000001)]; target_index = 0
        elif variant == 'off_lane_target': objects = [obj(1, 0x01000004)]; target_index = 0
        elif variant == 'target_behind_idle': objects = [obj(1), obj()]; target_index = 0
        elif variant == 'idle_behind_target': objects = [obj(), obj(1)]; target_index = 1
        blocked = {(x,y) for y in range(bounds[1],bounds[3]) for x in range(bounds[0],bounds[2])} if terrain == 'target_terrain' else set()
        for budget in (0, 5, 700):
            def setup():
                machine.mem_write(system, bytes(0x400)); machine.mem_write(bitmap, bytes(1024))
                machine.mem_write(cells, b''.join(struct.pack('<I', (mask & 0xff000000) | 0xffffff if (x,y) in blocked else 0xffffff) for y in range(height) for x in range(width)))
                write(system+0x1c,tilemap,1); write(system+0x30,nodes); write(system+0x3c,1024,0)
                write(system+0x50,heap); write(system+0x5c,32768,1,-3,budget,0)
                write(system+0x80,4,4,19,19); write(system+0x98,450,0); write(system+0xa0,cls,mask)
                target_object = 0 if target_index == 0xffffffff else 0x10118000 + target_index * 0x80
                write(system+0xa8,target_object)
                write(tilemap+0x28,cells); write(tilemap+0x3c,width,height); write(tilemap+0x78,links)
                write(tilemap+0x84,8192,0); write(tilemap+0x98,bitmap); write(tilemap+0xac,0xffffff)
                count = 0
                for i, object in enumerate(objects):
                    address = 0x10118000 + i*0x80; machine.mem_write(address,bytes(0x80))
                    write(address+0x34,object[4],0 if object[6] else -1); write(address+0x40,object[5])
                    for y in range(object[1],object[3]):
                        for x in range(object[0],object[2]):
                            cell = cells + 4*(y*width+x); old = read(cell)[0]
                            write(links+8*count,0x01000000 | (old & 0xffffff),address)
                            write(cell,(old & 0xff000000) | count); count += 1
                write(tilemap+0x88,count)
                start = run(0x6f147af0,system,4,4); goal = run(0x6f147af0,system,19,19)
                write(system+0x90,start,goal)
                return start, goal, target_object
            start, goal, target_object = setup(); result = run(0x6f14aa10,system)
            complete = result != 0xffffffff; chosen = result if complete else read(system+0x9c)[0]
            chain = []; at = chosen
            while at != 0xffffffff:
                chain.append(read(nodes+at*36,2)); at = read(nodes+at*36+0x1c)[0]
                if len(chain)>1024: raise RuntimeError('cyclic target parent chain')
            chain.reverse()
            output = [read(nodes+chosen*36+0x14)[0] if complete else -1, read(system+0x6c)[0], read(system+0x40)[0], len(chain), int(complete and result != goal), *read(nodes+chosen*36,2)]
            if read(system+0xcc)[0]: raise RuntimeError('target exit flag was not consumed')
            # Repeat through complete original setup/search/reconstruction.
            setup(); write(route,0,0,0,route_data,0,0,1024,0)
            machine.mem_write(source_ptr,struct.pack('<ff',*source)); machine.mem_write(target_ptr,struct.pack('<ff',*target))
            write(mask_ptr,mask); machine.mem_write(radius_ptr,struct.pack('<f',cls/2))
            returned = run(0x6f148100,system,route,source_ptr,target_ptr,mask_ptr,budget,radius_ptr,target_object)
            count = read(route+0x1c)[0]
            route_points = [list(struct.unpack('<ff',machine.mem_read(route_data+i*8,8))) for i in range(count)]
            endpoint = list(target) if complete and result == goal else [p+.5 for p in chain[-1]] if chosen != start else list(source)
            if returned != int(complete) or read(system+0x6c)[0] != output[1] or read(system+0x40)[0] != output[2] or not route_points or route_points[0] != endpoint or route_points[-1] != list(source):
                raise RuntimeError(str(dict(bounds=bounds,terrain=terrain,cls=cls,mask=hex(mask),variant=variant,budget=budget,core=output,full=[returned,read(system+0x6c)[0],read(system+0x40)[0]],route=route_points,endpoint=endpoint)))
            query = [width,height,4,4,19,19,budget,cls,mask,0,len(objects),target_index]
            record = dict(bounds=bounds,terrain=terrain,variant=variant,input=query,objects=objects,blocked=[list(p) for p in sorted(blocked)],output=output,path=chain,route=route_points)
            if engine:
                raw = [v for obj in objects for v in obj]
                data = ObjectInput((ctypes.c_uint8*(width*height))(*(mask>>24 if (x,y) in blocked else 0 for y in range(height) for x in range(width))), (ctypes.c_uint32*len(raw))(*raw))
                out = (ctypes.c_int32*4096)(); engine.pathing_fine_target((ctypes.c_uint32*12)(*query),ctypes.byref(data),out)
                if list(out)[:7] != output or [[out[7+2*i],out[8+2*i]] for i in range(out[3])] != chain: raise RuntimeError('production C target exit differs: '+str(record))
            records.append(record)
    assert len(records)==2304
    witness = next(r for r in records if r['bounds']==[12,12,13,13] and r['terrain']=='open' and r['variant']=='target' and r['input'][7]==0 and r['input'][8]==0x02000002 and r['input'][6]==700)
    assert witness['output'][4:]==[1,11,11]
    if args.fixture: args.fixture.write_text(json.dumps(dict(binary_sha256=digest,cases=records,scope=__doc__),separators=(',',':'))+'\n')
    report = dict(binary_sha256=digest,passed=True,cases=len(records),target_exits=sum(r['output'][4] for r in records),engine_cases=len(records) if engine else 0,witness=witness)
    args.report.write_text(json.dumps(report,indent=2)+'\n'); print(json.dumps({k:v for k,v in report.items() if k!='witness'},indent=2))

if __name__=='__main__': main()
