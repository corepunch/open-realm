#!/usr/bin/env python3
"""Execute retail terrain endpoint -> path-map descriptor production and map initialization.

No stubs or retail bytes. Full bounds getter, bounded loader/constructor prefix,
full base-map initializers and complete no-file loader calls using real registry slots.
"""
import argparse
import ctypes
import math
import hashlib
import itertools
import json
import struct
from pathlib import Path


SHA256 = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--engine-library', type=Path, help='compare production coordinate arithmetic')
    parser.add_argument('--coordinate-fixture', type=Path, help='freeze constructed maps, boundary words and full classification grids')
    args = parser.parse_args()
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        engine.pathing_world_grid.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
    binary = args.binary.read_bytes()
    digest = hashlib.sha256(binary).hexdigest()
    if digest != SHA256:
        parser.error('unsupported binary; requires game.dll 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', binary, opt + offset)[0] for offset in (28, 56))
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(base, (size + 4095) & ~4095)
    uc.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
        va, count, offset = struct.unpack_from('<III', binary, section + 12)
        if count:
            uc.mem_write(base + va, binary[offset:offset + count])
    uc.mem_map(0x10000000, 0x100000)
    uc.mem_map(0x20000000, 0x10000)
    owner, system, fine, game = 0x10000000, 0x10001000, 0x10002000, 0x10003000
    cells, xptr, yptr, rect = 0x10004000, 0x10008000, 0x10008010, 0x10008020
    maps = [0x10009000 + n * 0x100 for n in range(4)]
    storage = [0x10010000 + n * 0x10000 for n in range(4)]
    stack, stop = 0x20008000, 0x30000000

    def write(address, *values):
        uc.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def floats(address, *values):
        uc.mem_write(address, struct.pack('<' + 'f' * len(values), *values))

    def words(address, count):
        return list(struct.unpack('<' + 'I' * count, uc.mem_read(address, 4 * count)))

    def run(entry, ecx, *arguments, edx=0):
        write(stack, stop, *arguments)
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        uc.reg_write(UC_X86_REG_EDX, edx)
        uc.emu_start(entry, stop, count=10000000)
        assert uc.reg_read(UC_X86_REG_EIP) == stop, hex(entry)

    uc.mem_map(0, 0x1000)
    uc.mem_map(0x11000000, 0x200000)
    terrain, terrain_records, bounds, input_scale = 0x10080000, 0x10083000, 0x10084000, 0x10084100
    registry, slots = 0x10085000, 0x10085100
    descriptors = [0x10086000 + 0x40 * n for n in range(6)]
    map_objects = [0x10087000 + 0x100 * n for n in range(6)]
    buffers = [0x11000000 + 0x40000 * n for n in range(6)]
    for address, value in [(0x6fd3c740, -1), (0x6fd3c744, 0), (0x6fd3c748, 1),
                           (0x6fd53a50, 8), (0x6fd53a54, 2), (0x6fd53a58, 4),
                           (0x6fd53a5c, 8), (0x6fd53a60, 16)]:
        floats(address, value)
    # Original integer initializer: packed terrain indices decode around -32768.
    run(0x6f017dc0, 0)
    assert struct.unpack('<f', uc.mem_read(0x6fd723f4, 4))[0] == -98304
    write(0x6fd53a48, owner)
    write(0x6fd3c82c, game)
    write(0x6fd726c0, terrain)
    write(0x6fd68610, registry)
    floats(input_scale, 1)
    write(terrain + 0xe0, 2, terrain_records)
    cases = initialized_maps = consumer_cases = 0
    examples = []
    endpoints = [(0, 0), (128, 192), (248, 244), (256, 256), (400, 400)]
    extents = [(1, 1), (4, 6), (8, 5), (17, 9), (32, 24)]
    for (x0, y0), (dx, dy) in itertools.product(endpoints, extents):
        x1, y1 = x0 + dx, y0 + dy
        write(terrain_records, (x0 << 23) | (y0 << 14))
        write(terrain_records + 28, (x1 << 23) | (y1 << 14))
        # The actual caller 78c090 invokes this mode-0 getter before 04c860.
        run(0x6f78b0a0, bounds, edx=0)
        world_bounds = [128 * y0 - 32768, 128 * x0 - 32768,
                        128 * y1 - 32768, 128 * x1 - 32768]
        assert list(struct.unpack('<4f', uc.mem_read(bounds, 16))) == world_bounds
        uc.mem_write(owner, bytes(0x600))
        write(0, 0)
        write(stack, stop, input_scale, input_scale)
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, bounds)
        uc.reg_write(UC_X86_REG_EDX, 0)  # No filename needed before allocation.
        uc.ctl_flush_tb()
        uc.emu_start(0x6f04c860, 0x6f14efe0, count=200000)
        assert uc.reg_read(UC_X86_REG_EIP) == 0x6f14efe0
        assert list(struct.unpack('<4f', uc.mem_read(game + 0x6c, 16))) == [
            world_bounds[1], world_bounds[0], world_bounds[3], world_bounds[2]]
        frame = uc.reg_read(UC_X86_REG_EBP)
        width, height = 4 * dx, 4 * dy
        base_w, base_h = (width + 16) // 2 + 1, (height + 16) // 2 + 1
        dimensions = [((width + 16) // 8 + 1, (height + 16) // 8 + 1),
                      (width, height)] + [(base_w >> n, base_h >> n) for n in range(4)]
        offsets = [-0x30, -0x5c, -0xb4, -0x10c, -0xe0, -0x88]
        for n, (offset, descriptor, dimension) in enumerate(zip(offsets, descriptors, dimensions)):
            raw = bytes(uc.mem_read(frame + offset, 44))
            uc.mem_write(descriptor, raw)
            assert words(descriptor + 0x10, 2) == list(dimension), (n, dimension)
            assert words(descriptor + 0x24, 2) == [0xffffffff, 0xffffffff]
        assert uc.reg_read(UC_X86_REG_ECX) == owner + 0x234
        assert uc.reg_read(UC_X86_REG_EDX) == frame - 0x30
        # Prefix intentionally ends before any factory call. Reset its open SEH
        # frame before independent complete initializer calls; this is fixture reset.
        write(0, 0)
        uc.mem_write(registry, bytes(0x100))
        write(registry + 0xc, slots)
        write(registry + 0x1c, 6)
        write(registry + 0x40, 0)
        write(registry + 0x50, 100)
        for n in range(6):
            write(slots + 8 * n, n + 1 if n < 5 else -1, 0)
        for n, (obj, data, descriptor, (w, h)) in enumerate(zip(map_objects, buffers, descriptors, dimensions)):
            uc.mem_write(obj, bytes(0x100))
            write(obj + 0x28, data)
            write(obj + 0x34, 0x8000, 0)
            size = 4 if n < 2 else 8
            uc.mem_write(data, b'\xa5' * (w * h * size + 16))
            run(0x6f14c8e0 if n < 2 else 0x6f151760, obj, descriptor)
            assert words(obj + 0x3c, 2) == [w, h]
            assert words(obj + 0x54, 4) == [0, 0, h, w]
            assert list(struct.unpack('<4f', uc.mem_read(obj + 0x44, 16))) == [0, 0, h, w]
            assert list(struct.unpack('<2f', uc.mem_read(obj + 0x64, 8))) == [1, 1]
            assert words(obj + 0x14, 2) == [n, 100 + n]
            assert words(slots + n * 8, 2) == [0xfffffffe, obj]
            assert words(obj + 0x38, 1) == [w * h]
            assert words(data, w * h * (size // 4)) == ([0xffffff] * (w * h) if n < 2 else [0] * (w * h * 2))
            assert bytes(uc.mem_read(data + w * h * size, 16)) == b'\xa5' * 16
            assert words(0, 1) == [0]
            initialized_maps += 1
        assert words(registry + 0x40, 1) == [0xffffffff]
        assert words(registry + 0x48, 1) == [6]
        write(owner + 0x24c, system)
        write(system + 0x1c, map_objects[1])
        # Consume the actual produced world origin and fine map without rewriting them.
        for x, y in [(0, 0), (width - 1, height - 1), (width, 0), (-1, 0)]:
            floats(xptr, world_bounds[1] + 32 * (x + 0.25))
            floats(yptr, world_bounds[0] + 32 * (y + 0.75))
            before = words(buffers[1], width * height)
            run(0x6f04d870, xptr, 2, 1, edx=yptr)
            expected = before[:]
            if 0 <= x < width and 0 <= y < height:
                expected[y * width + x] |= 0x02000000
            assert words(buffers[1], width * height) == expected
            consumer_cases += 1
        cases += 1
        examples.append(dict(packed_min=[x0, y0], packed_max=[x1, y1], world_bounds=world_bounds,
                             fine=[width, height], map_dimensions=dimensions))
    factory_cases = 0
    coordinate_maps = []
    grid_reads, selected_cells = [], []
    uc.hook_add(UC_HOOK_CODE, lambda m,a,n,d:grid_reads.append(words(m.reg_read(UC_X86_REG_EDX),1)[0]),
                begin=0x6f070c80, end=0x6f070c80)
    uc.hook_add(UC_HOOK_CODE, lambda m,a,n,d:selected_cells.append(m.reg_read(UC_X86_REG_ECX)),
                begin=0x6f054000, end=0x6f054000)

    def float_word(value):
        return struct.unpack('<I', struct.pack('<f', value))[0]

    def adjacent(value, step):
        raw = float_word(value)
        if not step: return raw
        if not raw & 0x7fffffff: return 0x80000001 if step < 0 else 1
        return raw - step if raw & 0x80000000 else raw + step

    def class_runs():
        values = []
        for data, (w,h) in zip(buffers[2:], dimensions[2:]):
            values.extend(word >> 24 for word in words(data,w*h*2)[1::2])
        return [[len(list(group)), value] for value, group in itertools.groupby(values)]

    maintenance_callbacks = []
    def observe_maintenance(machine, address, size, data):
        maintenance_callbacks.append((machine.reg_read(UC_X86_REG_ECX), words(owner + 0x164 + 0x40, 1)[0]))
    uc.hook_add(UC_HOOK_CODE, observe_maintenance, begin=0x6f14df20, end=0x6f14df20)
    maintenance_drains = release_prefixes = 0
    # Compose the same real producer continuously through all six factories,
    # actual free-list pops, registration, storage initialization and proximity
    # maintenance-request insertion. Observe before the search factories, then
    # resume through their initialization and complete no-file loader return.
    clock, timer_heap = owner + 0x164, 0x10089000
    timer_blocks = [0x1008a000, 0x1008a080]
    dirty_buffers = [0x1008b000, 0x1008c000]
    search_objects = [0x1008d000, 0x1008e000]
    acc_index = 0x10090000
    for (x0, y0), (dx, dy) in itertools.product(endpoints, extents):
        write(terrain_records, (x0 << 23) | (y0 << 14))
        write(terrain_records + 28, ((x0 + dx) << 23) | ((y0 + dy) << 14))
        run(0x6f78b0a0, bounds, edx=0)
        uc.mem_write(owner, bytes(0x1000))
        uc.mem_write(registry, bytes(0x100))
        write(registry + 0xc, slots)
        write(registry + 0x1c, 8)
        write(registry + 0x40, 0)
        write(registry + 0x50, 100)
        for n, obj in enumerate(map_objects):
            write(slots + 8 * n, n + 1 if n < 5 else -1, 0)
            uc.mem_write(obj - 4, bytes(0x104))
            # These are already constructed reusable pool entries, with genuine
            # retail vtables and preallocated dynamic tables; no allocator stub.
            write(obj, 0x6fa90a44 if n < 2 else 0x6fa90bd8)
            write(obj - 4, map_objects[n + 1] - 4 if n in [0, 2, 3, 4] else 0)
            allocated = 0x8000 * (4 if n < 2 else 8)
            write(obj + 0x20, buffers[n], allocated, buffers[n], allocated)
            write(obj + 0x34, 0x8000, 0)
            if n < 2:
                write(obj + 0x90, dirty_buffers[n], 4096, dirty_buffers[n], 4096)
                write(obj + 0xa4, 1024, 0)
        write(slots + 5 * 8, 6, 0)
        write(slots + 6 * 8, 7, 0)
        write(slots + 7 * 8, -1, 0)
        for obj, pool, vtable in zip(search_objects, [owner + 0x918, owner + 0x938], [0x6fa908ec, 0x6fa90c40]):
            uc.mem_write(obj - 4, bytes(0x204))
            write(obj, vtable)
            write(pool + 0x14, obj - 4)
        # A constructed priority queue retains its one sentinel slot. Supply a
        # consistent byte allocation header so release cannot silently avoid free.
        for n, (obj, offset) in enumerate(zip(search_objects, [0x44, 0x70])):
            data = 0x10091000 + n * 0x100
            write(obj + offset + 4, data, 12, data, 12)
            write(obj + offset + 0x18, 1, 1)
        write(search_objects[1] + 0x34, acc_index, 256 * 12, acc_index, 256 * 12)
        write(search_objects[1] + 0x3c, acc_index)
        write(search_objects[1] + 0x48, 256, 0)
        uc.mem_write(acc_index, b'\xa5' * (256 * 12 + 16))
        write(owner + 0x598 + 0x14, map_objects[0] - 4)
        write(owner + 0x5b8 + 0x14, map_objects[2] - 4)
        write(clock + 0x10, timer_heap)
        write(clock + 0x1c, 16, 1)
        write(clock + 0x38, timer_blocks[0])
        for n, block in enumerate(timer_blocks):
            uc.mem_write(block, bytes(0x80))
            write(block, timer_blocks[1] if n == 0 else 0)
        write(0, 0)
        write(stack, stop, input_scale, input_scale)
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, bounds)
        uc.reg_write(UC_X86_REG_EDX, 0)
        uc.ctl_flush_tb()
        uc.emu_start(0x6f04c860, 0x6f14ecb0, count=2000000)
        assert uc.reg_read(UC_X86_REG_EIP) == 0x6f14ecb0
        assert words(owner + 0x234, 6) == map_objects
        width, height = 4 * dx, 4 * dy
        base_w, base_h = (width + 16) // 2 + 1, (height + 16) // 2 + 1
        dimensions = [((width + 16) // 8 + 1, (height + 16) // 8 + 1), (width, height)] + [
            (base_w >> n, base_h >> n) for n in range(4)]
        for n, (obj, (w, h), scale) in enumerate(zip(map_objects, dimensions, [8, 1, 2, 4, 8, 16])):
            assert words(obj + 0x3c, 2) == [w, h]
            assert words(obj + 0x54, 4) == [0, 0, h, w]
            assert list(struct.unpack('<2f', uc.mem_read(obj + 0x64, 8))) == [scale, 1 / scale]
            assert words(obj + 0x14, 2) == [n, 100 + n]
            count = w * h
            assert words(buffers[n], count * (1 if n < 2 else 2)) == ([0xffffff] * count if n < 2 else [0] * (count * 2))
            if n < 2:
                assert words(obj + 0xb8, 1) == [timer_blocks[n] + 4]
                assert words(dirty_buffers[n], (count >> 5) + 1) == [0] * ((count >> 5) + 1)
                assert words(timer_blocks[n] + 4 + 0x10, 1) == [0x20001]
                assert words(timer_blocks[n] + 4 + 0x18, 1) == [obj]
        assert words(clock + 0x38, 2) == [0, 2]
        assert words(clock + 0x20, 1) == [3]
        assert words(timer_heap + 4, 2) == [block + 4 for block in timer_blocks]
        assert words(owner + 0x598 + 0x14, 3) == [0, 2, 2]
        assert words(owner + 0x5b8 + 0x14, 3) == [0, 4, 4]
        # Resume unchanged instruction state through both search factories,
        # owner/map links, no-file loader branch and full hierarchy construction.
        uc.emu_start(0x6f14ecb0, stop, count=5000000)
        assert uc.reg_read(UC_X86_REG_EIP) == stop
        assert words(0, 1) == [0]
        assert words(owner + 0x24c, 2) == search_objects
        assert words(search_objects[0] + 0x1c, 1) == [map_objects[1]]
        assert words(search_objects[1] + 0x1c, 4) == map_objects[2:]
        assert words(search_objects[1] + 0x4c, 1) == [256]
        assert words(acc_index, 256 * 3) == [0] * (256 * 3)
        assert bytes(uc.mem_read(acc_index + 256 * 12, 16)) == b'\xa5' * 16
        assert words(registry + 0x40, 1) == [0xffffffff]
        assert words(registry + 0x48, 1) == [8]
        for n, (obj, pool) in enumerate(zip(search_objects, [owner + 0x918, owner + 0x938])):
            assert words(obj + 0x14, 2) == [6 + n, 106 + n]
            assert words(slots + (6 + n) * 8, 2) == [0xfffffffe, obj]
            assert words(pool + 0x14, 3) == [0, 1, 1]
        expected_levels = []
        for level, (data, (w, h)) in enumerate(zip(buffers[2:], dimensions[2:])):
            expected = [0] * (w * h * 2)
            scale = 2 << level
            for y in range(min(h, height // scale + 1)):
                for x in range(min(w, width // scale + 1)):
                    states = []
                    for dy, dx in itertools.product(range(2), repeat=2):
                        cx, cy = 2 * x + dx, 2 * y + dy
                        if level == 0:
                            states.append(int(cx >= width or cy >= height))
                        else:
                            pw, ph = dimensions[level + 1]
                            states.append(1 if cx >= pw or cy >= ph else expected_levels[-1][2 * (cy * pw + cx) + 1] >> 30)
                    state = 0 if all(v == 0 for v in states) else 1 if all(v == 1 for v in states) else 2
                    expected[2 * (y * w + x) + 1] = (0x55 * state) << 24
            assert words(data, len(expected)) == expected, (level, width, height)
            expected_levels.append(expected)
        # Use the fully constructed world origin, fine map and hierarchy.
        # Each corner crosses both axes with adjacent raw words and decimal
        # fractions on either side. No coordinate producer/map state is replaced.
        ox, oy, mx, my = struct.unpack('<4f', uc.mem_read(game + 0x6c,16))
        boundary_cases = []
        initial_classes = class_runs()
        clean_fine = words(buffers[1],width*height)
        for corner_x, corner_y in itertools.product(range(2),repeat=2):
            edges = [(ox,mx)[corner_x], (oy,my)[corner_y]]
            choices = [[adjacent(edge,step) for step in (-1,0,1)] +
                       [float_word(edge - .1),float_word(edge + .1)] for edge in edges]
            for wx, wy in itertools.product(*choices):
                write(xptr,wx); write(yptr,wy)
                grid_reads.clear(); selected_cells.clear()
                run(0x6f04d870,xptr,2,1,edx=yptr)
                assert len(grid_reads)==2
                fine_words = grid_reads[::-1]
                xy = [math.floor(struct.unpack('<f',struct.pack('<I',v))[0]) for v in fine_words]
                valid = 0<=xy[0]<width and 0<=xy[1]<height
                index = xy[1]*width+xy[0] if valid else -1
                assert selected_cells==[buffers[1]+4*index if valid else 0]
                expected = clean_fine[:]
                if valid: expected[index] |= 0x02000000
                assert words(buffers[1],width*height)==expected
                output = fine_words + [v&0xffffffff for v in xy]
                scalar_a, scalar_b, scalar_out = 0x10008500,0x10008510,0x10008520
                for k in range(2):
                    write(scalar_a,fine_words[k]); write(scalar_b,float_word(32))
                    run(0x6f06f9c0,scalar_out,scalar_b,edx=scalar_a)
                    write(scalar_a,words(scalar_out,1)[0]); write(scalar_b,float_word((ox,oy)[k]))
                    run(0x6f06fbb0,scalar_out,scalar_b,edx=scalar_a)
                    output.append(words(scalar_out,1)[0])
                inputs = [wx,wy,float_word(ox),float_word(oy),float_word(32),float_word(32)]
                if engine:
                    actual = (ctypes.c_uint32*6)()
                    engine.pathing_world_grid((ctypes.c_uint32*6)(*inputs),actual)
                    assert list(actual)==output,(width,height,inputs,list(actual),output)
                boundary_cases.append(dict(corner=[corner_x,corner_y], input=[wx,wy], output=output, index=index))
                run(0x6f04d870,xptr,2,0,edx=yptr)
                assert words(buffers[1],width*height)==clean_fine
        assert class_runs()==initial_classes
        # Retain original clipped boundary effects in every movement lane.
        # Complete original edits followed by its explicit hierarchy rebuild;
        # reverse every edit and demand exact full-grid restoration.
        edits = []
        for mask in (2,4,0x40,0x80):
            for x,y in ((0,0),(width-1,0),(0,height-1),(width-1,height-1)):
                wx,wy = float_word(ox+32*(x+.25)),float_word(oy+32*(y+.75))
                write(xptr,wx); write(yptr,wy)
                run(0x6f04d870,xptr,mask,1,edx=yptr)
                edits.append([wx,wy,mask])
        run(0x6f04e0b0,0)
        edited_classes = class_runs()
        edited_fine = [v>>24 for v in words(buffers[1],width*height)]
        for wx,wy,mask in reversed(edits):
            write(xptr,wx); write(yptr,wy)
            run(0x6f04d870,xptr,mask,0,edx=yptr)
        run(0x6f04e0b0,0)
        assert words(buffers[1],width*height)==clean_fine
        assert class_runs()==initial_classes
        coordinate_maps.append(dict(bounds=list(map(float_word,(ox,oy,mx,my))), dimensions=dimensions,
            initial_classes=initial_classes, boundary_cases=boundary_cases, edits=edits,
            edited_fine=[[len(list(group)),value] for value,group in itertools.groupby(edited_fine)],
            edited_classes=edited_classes))
        # A terrain edit leaves the hierarchy stale even when the actual
        # constructed proximity timers fire repeatedly at their real deadlines.
        stale = [bytes(uc.mem_read(data, w * h * 8)) for data, (w, h) in zip(buffers[2:], dimensions[2:])]
        origin_x, origin_y = struct.unpack('<2f', uc.mem_read(game + 0x6c, 8))
        floats(xptr, origin_x + 8)
        floats(yptr, origin_y + 8)
        run(0x6f04d870, xptr, 2, 1, edx=yptr)
        assert words(buffers[1], 1) == [0x02ffffff]
        for iteration in range(3):
            requests = [block + 4 for block in timer_blocks]
            deadlines = [words(request + 4, 1)[0] for request in requests]
            assert deadlines[0] == deadlines[1] and deadlines[0] > 0
            write(clock + 0x40, deadlines[0] - 1)
            maintenance_callbacks.clear()
            run(0x6f0523d0, clock)
            assert not maintenance_callbacks
            assert words(clock + 0x40, 1) == [deadlines[0] - 1]
            # Dirty empty cells are fixture state; callback visitation/clearing is
            # real, but this does not claim terrain edits produced dirty flags.
            for obj, dirty in zip(map_objects[:2], dirty_buffers):
                write(obj + 0xb4, 0x80000000)
                write(dirty, 1)
            write(clock + 0x40, deadlines[0])
            run(0x6f0523d0, clock)
            assert maintenance_callbacks == [(obj, deadlines[0]) for obj in map_objects[:2]]
            assert words(clock + 0x40, 1) == [deadlines[0]]
            for obj, dirty, request in zip(map_objects[:2], dirty_buffers, requests):
                assert words(obj + 0xb4, 1) == [0]
                assert words(dirty, 1) == [0]
                assert words(request + 0x10, 1) == [0x20001]
                assert words(request + 4, 1)[0] > deadlines[0]
            assert words(clock + 0x20, 1) == [3]
            assert words(clock + 0x38, 2) == [0, 2]
            assert [bytes(uc.mem_read(data, w * h * 8)) for data, (w, h) in zip(buffers[2:], dimensions[2:])] == stale
            assert words(buffers[1], 1) == [0x02ffffff]
            maintenance_drains += 1
        run(0x6f04e0b0, 0)
        assert words(buffers[2] + 4, 1) == [0x80000000]
        # Original release reaches the external allocator when shrinking the first
        # proximity dirty table. Observe before the import thunk: never fake frees.
        write(stack, stop)
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, owner)
        uc.ctl_flush_tb()
        uc.emu_start(0x6f15b670, 0x6f07c678, count=5000000)
        assert uc.reg_read(UC_X86_REG_EIP) == 0x6f07c678
        free_stack = uc.reg_read(UC_X86_REG_ESP)
        assert words(free_stack + 4, 1) == [dirty_buffers[0]]
        assert words(owner + 0x24c, 2) == [0, 0]
        assert words(registry + 0x48, 1) == [6]
        for obj, pool in zip(search_objects, [owner + 0x918, owner + 0x938]):
            assert words(obj + 0x14, 2) == [0xffffffff, 0xffffffff]
            assert words(pool + 0x14, 2) == [obj - 4, 0]
        assert words(map_objects[0] + 0xb8, 1) == [0]
        assert words(timer_blocks[0] + 4 + 0x10, 1) == [0x30001]
        assert words(owner + 0x234, 6) == map_objects
        write(0, 0)
        release_prefixes += 1
        factory_cases += 1
    if args.coordinate_fixture:
        args.coordinate_fixture.write_text(json.dumps(dict(binary_sha256=digest,maps=coordinate_maps,
            scope='25 complete original no-file constructors from actual packed terrain bounds; full allocated static hierarchies, all corners and decimal/adjacent-word edits with original inverse calls; four-lane corner edit/rebuild/reversal. Engine uses BoxEdicts instead of allocating the native proximity grid.'),separators=(',',':'))+'\n')
    report = dict(binary_sha256=digest, terrain_bounds_full_calls=cases,
                  constructed_corner_cases=sum(len(m['boundary_cases']) for m in coordinate_maps),
                  constructed_corner_engine_cases=sum(len(m['boundary_cases']) for m in coordinate_maps) if engine else 0,
                  constructed_four_lane_edit_cases=sum(len(m['edits']) for m in coordinate_maps),
                  constructed_grid_reversal_cases=len(coordinate_maps),
                  constructor_factory_observation="6f14ecb0; then resume through full 04c860 return",
                  factory_prerequisites="Six reusable map pool entries with original vtables; preallocated cell/dirty tables; eight free registry slots and two reusable search-system pool entries; two maintenance request blocks and heap capacity. No stubs/import hooks.",
                  loader_constructor_prefix_cases=cases, map_initializer_full_calls=initialized_maps,
                  composed_loader_full_return_cases=factory_cases,
                  maintenance_before_deadline_drains=maintenance_drains,
                  maintenance_due_drains=maintenance_drains, maintenance_callbacks=maintenance_drains * 2,
                  release_prefixes=release_prefixes, release_boundary="6f07c678 Storm Ordinal_403: first proximity dirty-buffer free",
                  produced_origin_terrain_edit_cases=consumer_cases, examples=examples,
                  scope='Original 78b0a0→7425a0→73db20 bounds producer; original 04c860→15ab60 prefix stops at 14efe0 factory entry; full 14c8e0/151760 base-map initializers with real registry/preallocated storage; produced origin consumed by 04d870. Additionally 25 complete loader calls execute six map and two search-system factories, registration, maintenance-request scheduling, scale assignment, search/map links and hierarchy construction. No-file branch: no terrain deserialization; reusable pool entries and preallocated tables. Real repeating proximity maintenance clears supplied dirty empty cells/stamp wrap, preserves terrain edit and stale hierarchy across three deadlines. Explicit rebuild sees terrain edit. Release frees both search identities/pools then stops before external dirty-buffer free; full map release/reload unexecuted.')
    if engine: report['engine_library_sha256']=hashlib.sha256(args.engine_library.read_bytes()).hexdigest()
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'examples'}, indent=2))


if __name__ == '__main__':
    main()
