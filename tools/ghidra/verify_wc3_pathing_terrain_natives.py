#!/usr/bin/env python3
"""Execute complete original JASS terrain queries/writes with production scalar/flag helpers."""
import argparse, ctypes, hashlib, itertools, json, struct
from pathlib import Path


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_EAX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--engine-library', type=Path)
    args = parser.parse_args()
    binary = args.binary.read_bytes(); digest = hashlib.sha256(binary).hexdigest()
    if digest != 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':
        parser.error('unsupported game.dll')
    pe = struct.unpack_from('<I', binary, 60)[0]; opt = pe+24
    base, size = (struct.unpack_from('<I', binary, opt+i)[0] for i in (28,56))
    u = Uc(UC_ARCH_X86, UC_MODE_32); u.mem_map(base, (size+4095)&~4095)
    for i in range(struct.unpack_from('<H', binary, pe+6)[0]):
        section = opt+struct.unpack_from('<H', binary, pe+20)[0]+40*i
        va, n, offset = struct.unpack_from('<III', binary, section+12)
        if n: u.mem_write(base+va, binary[offset:offset+n])
    u.mem_map(0,4096); u.mem_map(0x10000000,0x100000); u.mem_map(0x20000000,0x10000)
    owner, system, fine, game, cells, x, y = (0x10000000,0x10001000,0x10002000,
                                           0x10003000,0x10060000,0x10008000,0x10008010)
    stack, stop = 0x20008000,0x30000000
    def write(address, *words): u.mem_write(address,struct.pack('<'+'I'*len(words),*(v&0xffffffff for v in words)))
    def run(entry, *arguments):
        write(stack,stop,*arguments); u.reg_write(UC_X86_REG_ESP,stack)
        u.emu_start(entry,stop,count=2000000)
        if u.reg_read(UC_X86_REG_EIP)!=stop or u.reg_read(UC_X86_REG_ESP)!=stack+4:
            raise RuntimeError('cdecl native ABI differs')
        if bytes(u.mem_read(0,4))!=bytes(4): raise RuntimeError('SEH restoration differs')
        return u.reg_read(UC_X86_REG_EAX)
    for entry in (0x6f001dd0,0x6f001a80,0x6f001b80): run(entry)
    write(0x6fd53a48,owner); write(0x6fd3c82c,game)
    write(owner+0x24c,system); write(system+0x1c,fine); write(fine+0x28,cells)
    write(fine+0x3c,16,16); write(fine+0xac,0xffffff)
    origin = (-1024,-2048); u.mem_write(game+0x6c,struct.pack('<ff',*origin))
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    seeds = (0,1,2,4,8,16,32,64,128,0x42,0x55,0xaa,255)
    sources = ((-896,-1888),(-895.875,-1887.75),(-1024.125,-1888),(-512,-1888))
    records = []; enum_masks = {}
    for typ in (*range(8),-1,42): enum_masks[typ] = run(0x6f201630,typ)&255
    for typ, seed, passable, source in itertools.product(enum_masks,seeds,(0,1),sources):
        before_cells = bytearray(struct.pack('<I',0xffffff)*256)
        # Both admitted sources select4,5; outside writes leave this witness unchanged.
        struct.pack_into('<I',before_cells,4*(5*16+4),(seed<<24)|0xffffff)
        u.mem_write(cells,bytes(before_cells)); u.mem_write(x,struct.pack('<f',source[0])); u.mem_write(y,struct.pack('<f',source[1]))
        write(system+0xd4,7)
        before = run(0x6f205e90,x,y,typ)
        run(0x6f2148f0,x,y,typ,passable)
        after = run(0x6f205e90,x,y,typ)
        result_cells = bytes(u.mem_read(cells,len(before_cells)))
        flags = struct.unpack_from('<I',result_cells,4*(5*16+4))[0]>>24
        expected = bytearray(before_cells); struct.pack_into('<I',expected,4*(5*16+4),(flags<<24)|0xffffff)
        if result_cells!=expected or bytes(u.mem_read(system+0xd4,4))!=struct.pack('<I',7):
            raise RuntimeError('native changed other cells/occupancy/endpoint mode')
        inp = [*struct.unpack('<IIII',struct.pack('<ffff',*source,*origin)),typ&0xffffffff,seed,passable]
        out = [enum_masks[typ],before,after,flags]; records.append({'input':inp,'output':out})
        if engine:
            actual = (ctypes.c_uint32*4)()
            engine.pathing_terrain_native((ctypes.c_uint32*7)(*inp),actual)
            if list(actual)!=out: raise RuntimeError(str(records[-1]|{'engine':list(actual)}))
    result = dict(passed=True,binary_sha256=digest,cases=len(records),original_calls=3*len(records)+len(enum_masks),
                  stack_abi_verified=True,mode_seh_restoration=True,unrelated_cells_preserved=True,
                  scope='Complete original205e90/2148f0 terrain query/write chain: ten enum values, thirteen seed bytes, both booleans, integer/fractional and outside sources; no imports replaced. Adaptive rebuild, objects and bridge placement remain separate.')
    if args.fixture: args.fixture.write_text(json.dumps(result|{'cases':records},indent=2)+'\n')
    args.report.write_text(json.dumps(result,indent=2)+'\n'); print(json.dumps(result,indent=2))


if __name__ == '__main__': main()
