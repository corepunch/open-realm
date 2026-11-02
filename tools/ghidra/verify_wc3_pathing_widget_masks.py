#!/usr/bin/env python3
"""Compose original widget texture rasterization, record mutation and hierarchy rebuild.

Preallocated region collections; original callbacks and cell insertion; independent
rotated sample geometry, effective occupancy and accelerator classification model.
Also executes full CUnit inherited widget reapply/removal through original resource-cache
lookup, pose/heading adapters, snapping and refresh gate. Widget creation, cache
miss allocation and authored resource decoding are excluded. One accepted callback
continues through actual order/task factories, user queue, Move task acceptance,
fresh search and seven/thirteen elapsed ticks to arrival/reclamation with the blocking
footprint retained. Shipped CRT math is loaded unchanged. Destruction stops at
genuine Storm403; this fixture advances the group directly, not the whole owner.
The default control supplies terrain-only mask02000000. The --stock-mask variant
executes original observed Footman profile getters/bridge before admission and
retains02000002 through thirteen ticks. Cache/unit backing and radius8 remain
supplied; full6945a0 class notification and public creation are separate scopes.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import math
import struct
from pathlib import Path


SHA256 = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_INVALID
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EAX, UC_X86_REG_EBP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--engine-library', type=Path)
    parser.add_argument('--fixture', type=Path, default=Path(__file__).with_name('fixtures') / 'retail-widget-escape-journey-1.27.json')
    parser.add_argument('--stock-mask', action='store_true', help='publish observed Footman profile masks through original getters/bridge before admission')
    parser.add_argument('--solid-footprint', action='store_true', help='retain a supplied solid9x9 widget and verify original cannot-path recovery')
    args = parser.parse_args()
    if args.solid_footprint and not args.stock_mask:
        parser.error('--solid-footprint requires --stock-mask')
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

    links, dirty = 0x10060000, 0x10065000
    texture, pixels, center, bounds = 0x10070000, 0x10070100, 0x10070200, 0x10070300
    collections = [0x10071000, 0x10071100]
    lists = [0x10071200, 0x10071300]
    objects = [[0x10072000 + 0x400 * group + 0x80 * n for n in range(4)] for group in range(2)]
    region_masks = [0xc2, 0x10, 8, 4]
    lane_masks = [0x06000006, 0x80000080, 0x40000040, 0x04000004]
    width = height = 16
    sizes = [(17 >> n, 17 >> n) for n in range(4)]
    for address, value in [(0x6fd3c740, -1), (0x6fd3c744, 0), (0x6fd3c748, 1)]:
        floats(address, value)
    run(0x6f006e90, 0)
    run(0x6f006ea0, 0)
    run(0x6f070d80, 0x6fd3c7a8, edx=32)  # actual CRT initializer001c70
    write(0x6fd53a48, owner)
    write(0x6fd3c82c, game)
    write(owner + 0x24c, system)
    write(system + 0x1c, fine)
    write(owner + 0x23c, *maps)
    write(fine + 0x28, cells)
    write(fine + 0x3c, width, height)
    write(fine + 0x54, 0, 0, height, width)
    write(fine + 0x78, links)
    write(fine + 0x84, 2048, 0)
    write(fine + 0x98, dirty)
    write(fine + 0xa8, 8)
    for level, (tilemap, data, (w, h)) in enumerate(zip(maps, storage, sizes)):
        write(tilemap + 0x28, data)
        write(tilemap + 0x3c, w, h)
        floats(tilemap + 0x64, 2 << level, 1 / (2 << level))
    write(texture + 0x20, pixels)
    for group, collection in enumerate(collections):
        write(collection, 4, 4, lists[group])
        write(lists[group], *objects[group])
        for obj, mask in zip(objects[group], region_masks):
            write(obj + 0x2c, fine)
            write(obj + 0x34, 0x01000000 | mask, 0, 0, 0x10000000)
    observed = []
    callbacks = []
    additional_maps = set()

    def insertion(machine, address, size, data):
        sp = machine.reg_read(UC_X86_REG_ESP)
        assert machine.reg_read(UC_X86_REG_ECX) in {fine} | additional_maps
        observed.append(tuple(words(sp + 4, 4)))

    def callback(machine, address, size, data):
        point = machine.reg_read(UC_X86_REG_ECX)
        sp = machine.reg_read(UC_X86_REG_ESP)
        callbacks.append((struct.unpack('<2f', machine.mem_read(point, 8)), tuple(words(sp + 4, 4))))

    uc.hook_add(UC_HOOK_CODE, insertion, begin=0x6f14d9e0, end=0x6f14d9e0)
    for entry in [0x6f652b40, 0x6f650a70]:
        uc.hook_add(UC_HOOK_CODE, callback, begin=entry, end=entry)

    snap_engine_cases = 0
    clamp_engine_cases = 0
    if args.engine_library:
        # Original22f410 first calls04c330 (map-bound clamp). Keep these
        # independent snap inputs inside real, provisioned map bounds.
        floats(game+0x6c, -2147483648., -2147483648., 2147483648., 2147483648.)
        engine = ctypes.CDLL(str(args.engine_library.resolve()))
        word = ctypes.c_uint32
        engine.pathing_widget_snap.argtypes = [ctypes.POINTER(word), ctypes.POINTER(word)]
        positions = [(0., -0.), (.001, -.001), (31.75, -31.75), (32., -32.),
                     (63.75, -63.75), (64., -64.), (64.25, -64.25),
                     (-1936., -560.), (-7168., -3072.), (5119.875, 5119.125),
                     (65535.75, -65535.75), (1073741824., -1073741824.)]
        for (w, h), orientation, (x, y) in itertools.product(
                [(1,1), (2,3), (3,2), (4,4), (20,4), (4,20)], range(4), positions):
            write(texture+8, w, h)
            floats(xptr, x); floats(yptr, y)
            inputs = (word*5)(*words(xptr,1), *words(yptr,1), w, h, orientation)
            result = (word*2)()
            engine.pathing_widget_snap(inputs, result)
            run(0x6f22f410, texture, xptr, yptr, orientation)
            original = words(xptr,1) + words(yptr,1)
            assert list(result) == original, ('production widget snap', w, h, orientation, x, y, list(result), original)
            snap_engine_cases += 1
        engine.pathing_widget_clamp.argtypes = [ctypes.POINTER(word), ctypes.POINTER(word)]
        for box in [(0.,0.,1024.,1024.), (-7168.,-3072.,5120.,5120.), (-32.,-64.,32.,64.)]:
            floats(game+0x6c,*box)
            for at in range(9):
                axes = [[lo-128,lo-.001,lo,lo+.001,(lo+hi)/2,hi-32,hi-31.999,hi,hi+128]
                        for lo,hi in zip(box[:2],box[2:])]
                floats(center,axes[0][at],axes[1][at])
                inputs = (word*6)(*words(center,2),*words(game+0x6c,4))
                result = (word*2)()
                engine.pathing_widget_clamp(inputs,result)
                run(0x6f04c330,center)
                assert list(result)==words(center,2), ('production widget clamp',box,at)
                clamp_engine_cases += 1

    def levels():
        return [words(data, w * h * 2) for data, (w, h) in zip(storage, sizes)]

    def expected_levels(active):
        result = []
        for level, (w, h) in enumerate(sizes):
            values = [0] * (w * h * 2)
            scale = 2 << level
            for y in range(min(h, height // scale + 1)):
                for x in range(min(w, width // scale + 1)):
                    byte = 0
                    for lane, lane_mask in enumerate(lane_masks):
                        states = []
                        for dy, dx in itertools.product(range(2), repeat=2):
                            cx, cy = 2 * x + dx, 2 * y + dy
                            if level == 0:
                                blocked = cx >= width or cy >= height or any(
                                    enabled and ax == cx and ay == cy and region_mask & lane_mask & 0xffffff
                                    for (ax, ay, group, region_mask), enabled in active.items())
                                states.append(int(blocked))
                            else:
                                pw, ph = sizes[level - 1]
                                states.append(1 if cx >= pw or cy >= ph else (result[-1][2 * (cy * pw + cx) + 1] >> (30 - 2 * lane)) & 3)
                        state = 0 if all(v == 0 for v in states) else 1 if all(v == 1 for v in states) else 2
                        byte |= state << (6 - 2 * lane)
                    values[2 * (y * w + x) + 1] = byte << 24
            result.append(values)
        return result

    def sample_points(w, h, orientation, cx, cy):
        result = []
        for row, col in itertools.product(range(h), range(w)):
            dx, dy = (col - (w - 1) / 2) * 32, (row - (h - 1) / 2) * 32
            rx, ry = [(dx, dy), (-dy, dx), (-dx, -dy), (dy, -dx)][orientation]
            result.append((cx + rx, cy + ry))
        return result

    cases = stages = total_records = total_callbacks = 0
    patterns = [(2,), (0, 2, 4, 8, 0x10, 0x40, 0x80, 0xc2, 0xff), (0xff, 0, 0x42, 4)]
    for (tw, th), orientation, (ox, oy), pattern, shift in itertools.product(
            [(1, 1), (2, 3), (3, 2)], range(4), [(0, 0), (-1024, -512)], patterns, [0, 32]):
        floats(game + 0x6c, ox, oy, ox + width * 32, oy + height * 32)
        terrain = [0xffffff] * (width * height)
        write(cells, *terrain)
        write(fine + 0x88, 0)
        write(fine + 0xac, 0xffffff, 0, 0)
        write(dirty, *([0] * 8))
        for group in objects:
            for obj in group:
                write(obj + 0x38, 0, 0)
        for data, (w, h) in zip(storage, sizes):
            uc.mem_write(data, bytes(w * h * 8))
        run(0x6f15d360, owner, 0, 0)
        active = {}
        baseline = levels()
        assert baseline == expected_levels(active)
        history = [[] for _ in terrain]
        allocated = 0
        object_counts = {obj: 0 for group in objects for obj in group}
        dirty_cells = set()
        payload = bytes(pattern[n % len(pattern)] for n in range(tw * th))
        write(texture + 8, tw, th)
        uc.mem_write(pixels, payload)
        for group, enable in [(0, 1), (1, 1), (0, 0), (1, 0)]:
            cx, cy = ox + 7 * 32 + group * shift, oy + 7 * 32 + group * shift
            floats(center, cx, cy)
            points = sample_points(tw, th, orientation, cx, cy)
            expected_records = []
            for (wx, wy), byte in zip(points, payload):
                x, y = math.floor((wx - ox) / 32), math.floor((wy - oy) / 32)
                for obj, mask in zip(objects[group], region_masks):
                    if byte & mask:
                        expected_records.append((x, y, obj, 0x01000000 if enable else 0))
                        active[x, y, group, mask] = bool(enable)
                        history[y * width + x].insert(0, (allocated, obj, enable))
                        object_counts[obj] += 1
                        allocated += 1
                        if not enable:
                            dirty_cells.add(y * width + x)
            before = levels()
            observed.clear()
            callbacks.clear()
            run(0x6f22e9c0, texture, center, orientation, 0x6f652b40 if enable else 0x6f650a70, collections[group])
            assert observed == expected_records, (tw, th, orientation, group, enable, observed, expected_records)
            expected_callbacks = [(point, (n // tw, orientation, byte, collections[group])) for n, (point, byte) in enumerate(zip(points, payload))]
            assert callbacks == expected_callbacks
            assert levels() == before
            assert words(fine + 0x88, 1) == [allocated]
            assert words(fine + 0xb0, 1) == [allocated]
            for obj, count in object_counts.items():
                assert words(obj + 0x3c, 1) == [count]
            for index, records in enumerate(history):
                expected_head = records[0][0] if records else 0xffffff
                assert words(cells + 4 * index, 1) == [expected_head]
                for n, (slot, obj, inserted) in enumerate(records):
                    next_slot = records[n + 1][0] if n + 1 < len(records) else 0xffffff
                    assert words(links + 8 * slot, 2) == [next_slot | (0x01000000 if inserted else 0), obj]
            expected_dirty = [sum(1 << (index % 32) for index in dirty_cells if index // 32 == row) for row in range(8)]
            assert words(dirty, 8) == expected_dirty
            run(0x6f22f1d0, texture, bounds, center, orientation)
            hx, hy = (tw * 16, th * 16) if orientation % 2 == 0 else (th * 16, tw * 16)
            assert list(struct.unpack('<4f', uc.mem_read(bounds, 16))) == [cy - hy, cx - hx, cy + hy, cx + hx]
            run(0x6f04e0b0, bounds)
            assert levels() == expected_levels(active), (tw, th, orientation, group, enable, shift)
            stages += 1
            total_records += len(observed)
            total_callbacks += len(callbacks)
        assert levels() == baseline
        cases += 1
    # Actual CUnit's inherited widget method: authored-resource cache lookup,
    # pose adapter, heading-to-quarter conversion, snapped position and real gate.
    widget, mover, registry, slots = 0x10074000, 0x10074400, 0x10074800, 0x10074900
    resource_entry, resource_bucket, key_ptr = 0x10074a00, 0x10074b00, 0x10074c00
    key = 0x68746f77
    write(key_ptr, key)
    run(0x6f198420, key_ptr)
    key_hash = uc.reg_read(UC_X86_REG_EAX)
    write(0x6fd6a698, resource_bucket)
    write(0x6fd6a6a0, 0)
    write(resource_bucket, 0, 0, resource_entry)
    write(resource_entry + 4, key_hash)
    write(resource_entry + 0x18, key)
    write(resource_entry + 0xcc, texture)
    write(0x6fd68610, registry)
    write(registry + 0xc, slots)
    write(registry + 0x1c, 1)
    write(slots, -2, mover)
    write(mover + 0x14, 0, 100)
    floats(owner + 0x54, 0)
    write(owner + 0x58, 0)
    write(widget, 0x6fb77eb0)
    write(widget + 0x30, key, collections[0])
    write(widget + 0x5c, 0x10000)
    write(widget + 0x164, 0x6fac2f10, 0, 0, 100)
    rotations = [0] + [words(address, 1)[0] for address in [0x6fcd5458, 0x6fcd545c, 0x6fcd5460]]
    method_rasters, method_rebuilds = [], []
    def observe_raster(machine, address, size, data):
        sp = machine.reg_read(UC_X86_REG_ESP)
        point, rotation, callback_fn, collection = words(sp + 4, 4)
        method_rasters.append((machine.reg_read(UC_X86_REG_ECX),
            struct.unpack('<2f', machine.mem_read(point, 8)), rotation, callback_fn, collection))
    def observe_rebuild(machine, address, size, data):
        point = machine.reg_read(UC_X86_REG_ECX)
        method_rebuilds.append(struct.unpack('<4f', machine.mem_read(point, 16)))
    uc.hook_add(UC_HOOK_CODE, observe_raster, begin=0x6f22e9c0, end=0x6f22e9c0)
    uc.hook_add(UC_HOOK_CODE, observe_rebuild, begin=0x6f04e0b0, end=0x6f04e0b0)
    uc.ctl_flush_tb()
    def snap_axis(value, extent):
        sign = -1 if value < 0 else 1
        return math.trunc(math.trunc(value) / 64) * 64 + sign * (32 * ((extent >> 1) & 1) + 16 * (extent & 1))
    widget_cases = 0
    for (tw, th), orientation, (ox, oy), offset, gate in itertools.product(
            [(1, 1), (2, 3), (3, 2)], range(4), [(0, 0), (-1024, -512)], [0, 15.5], [0, 1]):
        floats(game + 0x6c, ox, oy, ox + width * 32, oy + height * 32)
        cx, cy = ox + 7 * 32 + offset, oy + 7 * 32 - offset
        sx, sy = (tw, th) if orientation % 2 == 0 else (th, tw)
        snapped = (snap_axis(cx, sx), snap_axis(cy, sy))
        floats(mover + 0x70, 0)
        write(mover + 0x74, 0)
        floats(mover + 0x78, (cx - ox) / 32, (cy - oy) / 32, 0, 0)
        write(mover + 0xc8, rotations[orientation])
        write(cells, *([0xffffff] * (width * height)))
        write(fine + 0x88, 0)
        write(fine + 0xac, 0xffffff, 0, 0)
        write(dirty, *([0] * 8))
        for obj in objects[0]:
            write(obj + 0x38, 0, 0)
        for data, (w, h) in zip(storage, sizes):
            uc.mem_write(data, bytes(w * h * 8))
        run(0x6f15d360, owner, 0, 0)
        empty = levels()
        payload = bytes([2 if n % 2 == 0 else 4 for n in range(tw * th)])
        write(texture + 8, tw, th)
        uc.mem_write(pixels, payload)
        floats(center, *snapped)
        run(0x6f22e9c0, texture, center, orientation, 0x6f652b40, collections[0])
        run(0x6f22f1d0, texture, bounds, center, orientation)
        footprint = struct.unpack('<4f', uc.mem_read(bounds, 16))
        run(0x6f04e0b0, bounds)
        blocked = levels()
        assert blocked != empty
        method_rasters.clear()
        method_rebuilds.clear()
        observed.clear()
        callbacks.clear()
        run(0x6f651160, gate)
        run(0x6f6514d0, widget)
        assert method_rasters == [(texture, snapped, orientation, 0x6f650a70, collections[0])], (tw, th, orientation, ox, oy, offset, method_rasters, snapped)
        assert method_rebuilds == ([footprint] if gate else [])
        wanted = []
        for (wx, wy), byte in zip(sample_points(tw, th, orientation, *snapped), payload):
            x, y = math.floor((wx - ox) / 32), math.floor((wy - oy) / 32)
            for obj, mask in zip(objects[0], region_masks):
                if byte & mask:
                    wanted.append((x, y, obj, 0))
        assert observed == wanted
        assert levels() == (empty if gate else blocked)
        if not gate:
            run(0x6f04e0b0, bounds)
            assert levels() == empty
        assert words(widget + 0x34, 1) == [collections[0]]
        assert words(resource_entry + 0xcc, 1) == [texture]
        widget_cases += 1
    # Reapply additionally queries terrain height and nearby agile agents.
    # Supply existing terrain/overlay/query storage and an empty proximity grid;
    # original enumeration executes, but its nonempty callback is out of scope.
    uc.mem_map(0, 0x1000)  # Original terrain singleton getter's FS exception chain.
    terrain_object, terrain_records = 0x10078000, 0x1007b000
    overlays, query_slots, query = 0x1007c000, 0x1007c100, 0x1007c200
    proximity, proximity_cells = 0x1007d000, 0x1007d100
    write(0x6fd726c0, terrain_object)
    write(0x6fd726cc, overlays)
    write(terrain_object + 0xb4, 4, 4, 16, 16)
    write(terrain_object + 0xe4, terrain_records)
    write(game + 0x58, 1, 1, query_slots, 0)
    write(query_slots, query)
    write(owner + 0x234, proximity)
    write(proximity + 0x28, proximity_cells)
    write(proximity + 0x3c, 3, 3)
    write(proximity + 0x54, 0, 0, 3, 3)
    floats(proximity + 0x64, 8, .125)
    write(proximity_cells, *([0xffffff] * 9))
    widgets, movers = [widget, 0x10075000], [mover, 0x10075400]
    uc.mem_write(widgets[1], bytes(uc.mem_read(widget, 0x200)))
    uc.mem_write(movers[1], bytes(uc.mem_read(mover, 0x100)))
    write(registry + 0x1c, 2)
    write(slots, -2, movers[0], -2, movers[1])
    for group in range(2):
        write(widgets[group] + 0x34, collections[group])
        write(widgets[group] + 0x16c, group, 100 + group)
        write(movers[group] + 0x14, group, 100 + group)
    for index in range(25):
        uc.mem_write(terrain_records + index * 28 + 10, bytes([0xa5]))
    reapply_trace = []
    def observe_reapply_dependencies(machine, address, size, data):
        if address == 0x6f654635:
            assert machine.reg_read(UC_X86_REG_EAX) == 5
        reapply_trace.append(address)
    for entry in [0x6f654635, 0x6f04c5d0, 0x6f05ef40, 0x6f654090]:
        uc.hook_add(UC_HOOK_CODE, observe_reapply_dependencies, begin=entry, end=entry)
    uc.ctl_flush_tb()
    reapply_cases = reapply_calls = removal_calls = proximity_visits = 0
    for (tw, th), orientation, (ox, oy), shift, gate in itertools.product(
            [(1, 1), (2, 3), (3, 2)], range(4), [(0, 0), (-1024, -512)], [0, 64], [0, 1]):
        floats(game + 0x6c, ox, oy, ox + width * 32, oy + height * 32)
        write(cells, *([0xffffff] * (width * height)))
        write(fine + 0x88, 0)
        write(fine + 0xac, 0xffffff, 0, 0)
        write(dirty, *([0] * 8))
        for group in objects:
            for obj in group:
                write(obj + 0x38, 0, 0)
        for data, (w, h) in zip(storage, sizes):
            uc.mem_write(data, bytes(w * h * 8))
        run(0x6f15d360, owner, 0, 0)
        active = {}
        payload = bytes([2 if n % 2 == 0 else 4 for n in range(tw * th)])
        write(texture + 8, tw, th)
        uc.mem_write(pixels, payload)
        run(0x6f651160, gate)
        for group, enable in [(0, 1), (1, 1), (0, 0), (1, 0)]:
            cx, cy = ox + 224 + group * shift + 15.5, oy + 224 - 15.5
            sx, sy = (tw, th) if orientation % 2 == 0 else (th, tw)
            snapped = snap_axis(cx, sx), snap_axis(cy, sy)
            floats(movers[group] + 0x78, (cx - ox) / 32, (cy - oy) / 32, 0, 0)
            write(movers[group] + 0xc8, rotations[orientation])
            before = levels()
            method_rasters.clear()
            method_rebuilds.clear()
            observed.clear()
            callbacks.clear()
            reapply_trace.clear()
            run(0x6f6544f0 if enable else 0x6f6514d0, widgets[group])
            assert reapply_trace.count(0x6f654635) == enable
            assert reapply_trace.count(0x6f04c5d0) == enable
            assert (reapply_trace.count(0x6f05ef40) > 0) == bool(enable)
            assert 0x6f654090 not in reapply_trace
            proximity_visits += reapply_trace.count(0x6f05ef40)
            wanted = []
            for (wx, wy), byte in zip(sample_points(tw, th, orientation, *snapped), payload):
                x, y = math.floor((wx - ox) / 32), math.floor((wy - oy) / 32)
                for obj, mask in zip(objects[group], region_masks):
                    if byte & mask:
                        wanted.append((x, y, obj, 0x01000000 if enable else 0))
                        active[x, y, group, mask] = bool(enable)
            assert observed == wanted
            assert method_rasters == [(texture, snapped, orientation, 0x6f652b40 if enable else 0x6f650a70, collections[group])]
            hx, hy = sx * 16, sy * 16
            footprint = (snapped[1] - hy, snapped[0] - hx, snapped[1] + hy, snapped[0] + hx)
            assert method_rebuilds == ([footprint] if gate else [])
            assert levels() == (expected_levels(active) if gate else before)
            assert words(game + 0x64, 1) == [0]
            assert words(query + 0x1c, 1) == [0]
            assert words(query + 0x40, 1) == [0]
            if not gate:
                floats(bounds, *footprint)
                run(0x6f04e0b0, bounds)
                assert levels() == expected_levels(active)
            reapply_calls += enable
            removal_calls += 1 - enable
            if group == 0 and not enable:
                assert levels() != expected_levels({})  # B remains after A removal.
        assert levels() == expected_levels({})
        reapply_cases += 1
    # Registered nonempty proximity occupant: full callback geometry, with the
    # real unit flag4 suppression or actual empty-ability-list order rejection.
    from verify_wc3_pathing_numeric import bits, add, subtract
    occupant, occupant_mover, actor = 0x10080000, 0x10080400, 0x10080800
    occupant_region, query_items, proximity_links = 0x10080900, 0x10080a00, 0x10080b00
    radius_entry, radius_bucket = 0x10081000, 0x10081200
    uc.mem_write(occupant, bytes(uc.mem_read(widget, 0x200)))
    uc.mem_write(occupant_mover, bytes(uc.mem_read(mover, 0x100)))
    write(occupant + 4, 1)
    write(occupant + 0xc, 2, 102)
    write(occupant + 0x5c, 0x10004)
    write(occupant + 0x16c, 3, 103)
    write(occupant_mover + 0x14, 3, 103)
    write(occupant_mover + 0x30, actor)
    write(occupant_mover + 0x98, occupant_region)
    write(actor + 0xc, 0x2b61676c)
    write(actor + 0x14, 2, 102)
    write(actor + 0x54, occupant)
    write(registry + 0x1c, 4)
    write(slots + 16, -2, actor, -2, occupant_mover)
    write(query + 0xc, query_items)
    write(query + 0x14, 8, 8)
    write(proximity + 0x78, proximity_links)
    write(occupant_region + 0x2c, proximity, occupant_mover, 0, 0, 0x10000000, 0)
    write(radius_bucket, 0, 0, radius_entry)
    write(radius_entry, key_hash)
    write(radius_entry + 0x14, key)
    write(0x6fd709f4, radius_bucket)
    write(0x6fd709fc, 0)
    floats(radius_entry + 0x19c, 8)
    floats(0x6fd3c74c, 2)
    run(0x6f017bf0, 0)  # Authentic 128 jitter scale initializer.
    write(occupant + 0x1dc, 0xffffffff, 0xffffffff)  # Empty authentic ability list.
    proposals, proposal_inputs, order_attempts = [], [], []
    expected_order_result = 0xdd
    def observe_proposal(machine, address, size, data):
        frame = machine.reg_read(UC_X86_REG_EBP)
        if address == 0x6f654242:
            proposal_inputs.append((words(frame - 0x20, 1)[0], words(frame - 0x28, 1)[0], words(frame - 0x18, 1)[0]))
        elif address == 0x6f6543ac:
            proposals.append(tuple(words(frame - 0x3c, 2)))
        elif address == 0x6f69dd60:
            sp = machine.reg_read(UC_X86_REG_ESP)
            order, player, target, xp, yp = words(sp + 4, 5)
            assert machine.reg_read(UC_X86_REG_ECX) == occupant
            order_attempts.append((order, target, words(xp, 1)[0], words(yp, 1)[0]))
        else:
            assert machine.reg_read(UC_X86_REG_EAX) == expected_order_result
    for entry in [0x6f654242, 0x6f6543ac, 0x6f69dd60, 0x6f6543d5]:
        uc.hook_add(UC_HOOK_CODE, observe_proposal, begin=entry, end=entry)
    uc.ctl_flush_tb()
    nonempty_cases = order_rejections = 0
    for (tw, th), orientation, (ox, oy), delta, gate, suppress in itertools.product(
            [(3, 3), (2, 3)], range(4), [(0, 0), (-1024, -512)],
            [(0, 0), (-20, 0), (20, 0), (0, -20), (0, 20)], [0, 1], [0, 1]):
        write(occupant + 0x5c, 0x10000 | (4 if suppress else 0))
        floats(game + 0x6c, ox, oy, ox + 512, oy + 512)
        write(cells, *([0xffffff] * 256))
        write(fine + 0x88, 0)
        write(fine + 0xac, 0xffffff, 0, 0)
        write(dirty, *([0] * 8))
        for obj in objects[0]:
            write(obj + 0x38, 0, 0)
        for data, (w, h) in zip(storage, sizes):
            uc.mem_write(data, bytes(w * h * 8))
        run(0x6f15d360, owner, 0, 0)
        empty = levels()
        write(texture + 8, tw, th)
        uc.mem_write(pixels, bytes([2] * (tw * th)))
        floats(mover + 0x78, 7, 7, 0, 0)
        write(mover + 0xc8, rotations[orientation])
        write(widget + 0x34, collections[0])
        sx, sy = (tw, th) if orientation % 2 == 0 else (th, tw)
        cx, cy = snap_axis(ox + 224, sx), snap_axis(oy + 224, sy)
        px, py = cx + delta[0], cy + delta[1]
        floats(occupant_mover + 0x78, (px - ox) / 32, (py - oy) / 32, 0, 0)
        write(proximity_cells, *([0xffffff] * 9))
        cell = int((py - oy) / 256) * 3 + int((px - ox) / 256)
        write(proximity_cells + cell * 4, 0)
        write(proximity_links, 0x01ffffff, occupant_region)
        write(occupant_region + 0x38, 0)
        write(query + 0x1c, 0)
        write(owner, 0x12345678, 0x10203040)
        before_pose = bytes(uc.mem_read(occupant_mover + 0x70, 0x18))
        reapply_trace.clear()
        proposals.clear()
        proposal_inputs.clear()
        order_attempts.clear()
        method_rebuilds.clear()
        run(0x6f651160, gate)
        run(0x6f6544f0, widget)
        assert reapply_trace.count(0x6f654090) == 1, reapply_trace
        assert len(proposals) == len(proposal_inputs) == 1
        jx, jy, radius = proposal_inputs[0]
        assert radius == bits(16)
        left, right, bottom, top = cx - sx * 16, cx + sx * 16, cy - sy * 16, cy + sy * 16
        dx, dy = min(abs(px - left), abs(px - right)), min(abs(py - bottom), abs(py - top))
        if dy <= dx:
            expected = (add(bits(px), jx), add(add(bits(top), radius), jy) if dy == abs(py - top)
                        else subtract(subtract(bits(bottom), radius), jy))
        else:
            expected = (add(add(bits(right), radius), jx) if dx == abs(px - right)
                        else subtract(subtract(bits(left), radius), jx), add(bits(py), jy))
        assert proposals == [expected], (orientation, ox, oy, delta, proposals, expected)
        assert order_attempts == ([] if suppress else [(0xd0012, 0, *expected)])
        order_rejections += not suppress
        assert words(occupant + 4, 1) == [1]
        assert bytes(uc.mem_read(occupant_mover + 0x70, 0x18)) == before_pose
        assert words(query + 0x1c, 1) == [1]
        assert words(query + 0x40, 1) == [0]
        assert words(game + 0x64, 1) == [0]
        assert len(method_rebuilds) == gate
        assert (levels() != empty) == bool(gate)
        nonempty_cases += 1
    # Uninterrupted accepted widget call with a real attached Move ability.
    ability, ability_wrapper = 0x10082000, 0x10082400
    write(ability, 0x6fb62794)
    write(ability + 4, 1)
    write(ability + 0xc, 4, 104)
    write(ability + 0x20, 4, 0xffffffff, 0xffffffff)
    write(ability + 0x30, occupant)
    write(ability_wrapper, 0x6fa8099c)
    write(ability_wrapper + 0xc, 0x2b61676c)
    write(ability_wrapper + 0x14, 4, 104)
    write(ability_wrapper + 0x54, ability)
    write(registry + 0x1c, 5)
    write(slots + 32, -2, ability_wrapper)
    write(occupant + 0x1dc, 4, 104)
    write(occupant + 0x5c, 0x10000)
    expected_order_result = 0
    factory_requests = []
    def observe_order_factory(machine, address, size, data):
        sp = machine.reg_read(UC_X86_REG_ESP)
        target, flags, xp, yp, extra1, extra2 = words(sp + 4, 6)
        factory_requests.append((machine.reg_read(UC_X86_REG_ECX), target, flags,
                                 words(xp, 1)[0], words(yp, 1)[0], extra1, extra2))
    factory_hook = uc.hook_add(UC_HOOK_CODE, observe_order_factory, begin=0x6f69bd80, end=0x6f69bd80)
    # All allocator and subscriber provisioning precedes the tested entry.
    class_bucket, class_row, wrapper_pool = 0x10083000, 0x10083100, 0x10083400
    wrapper_block, order_block = 0x10083800, 0x10084000
    write(owner + 0x10, 0x6f04c220)
    write(owner + 0x254, 0x6f04d9c0)
    write(game + 0x28, class_bucket, 0, 0)
    write(class_bucket, 8, 0, class_row)
    write(key_ptr, 0x6f726474)
    run(0x6f198420, key_ptr)
    write(class_row + 4, uc.reg_read(UC_X86_REG_EAX))
    write(class_row + 0x18, 0x6f726474)
    if args.solid_footprint:
        # Actual RTTI parent COrderPoint;04ce00 checks immediate parent before cache.
        write(class_row + 0x78, 0x6f72642e)
    write(class_row + 0x6c, class_row + 0x80, 0x6fd70e44)
    write(0x6fd70e44, 0x6fb78984)
    run(0x6f06a270, 0x6fd70e48, 0x88, 4)
    write(0x6fd70e44 + 0x14, order_block)
    write(game + 0x34, wrapper_pool)
    write(wrapper_pool + 0x14, wrapper_block)
    for index in range(4):
        item = order_block + index * 0xc0
        write(item, item + 0xc0 if index < 3 else 0)
        item = wrapper_block + index * 0x100
        write(item, item + 0x100 if index < 3 else 0)
        run(0x6f056f20, item + 4)
        write(item + 4, 0x6fa8099c)
    write(registry + 0x18, 16, 16)
    write(registry + 0x40, 5)
    write(registry + 0x48, 5)
    write(registry + 0x50, 105)
    for index in range(5, 16):
        write(slots + index * 8, index + 1 if index < 15 else -1, 0)
    dispatch_orders = []
    def observe_order_dispatch(machine, address, size, data):
        assert machine.reg_read(UC_X86_REG_ECX) == occupant
        sp = machine.reg_read(UC_X86_REG_ESP)
        order, mode, force = words(sp + 4, 3)
        dispatch_orders.append(order)
        assert (mode, force) == (0, 1)
        assert words(order, 1) == [0x6fb78994]
        assert words(order + 0x24, 1) == [0xd0012]
        assert (words(order + 0x48, 1)[0], words(order + 0x50, 1)[0]) == proposals[0]
        assert words(order + 0x38, 2) == [0xffffffff, 0xffffffff]
        assert words(order + 0x58, 2) == [0xffffffff, 0xffffffff]
        index, generation = words(order + 0xc, 2)
        wrapper = words(words(registry + 0xc, 1)[0] + index * 8 + 4, 1)[0]
        assert words(wrapper + 0x14, 2) == [index, generation]
        assert words(wrapper + 0x54, 1) == [order]
        assert words(registry + 0x48, 1) == [6]
    dispatch_hook = uc.hook_add(UC_HOOK_CODE, observe_order_dispatch, begin=0x6f680320, end=0x6f680320)
    # Continue actual displacement dispatch using existing class/allocator state.
    additional_maps.add(proximity)
    read = lambda address, count=1: words(address, count)
    # Load the shipped CRT's real transform math; relocate data, never replace code.
    crt=(args.binary.parent/'msvcr120.dll').read_bytes()
    crt_digest=hashlib.sha256(crt).hexdigest()
    if crt_digest!='86e39b5995af0e042fcdaa85fe2aefd7c9ddc7ad65e6327bd5e7058bc3ab615f':
        parser.error('unsupported shipped msvcr120.dll')
    cp=struct.unpack_from('<I',crt,0x3c)[0]; co=cp+24
    old_base,crt_size=struct.unpack_from('<I',crt,co+28)[0],struct.unpack_from('<I',crt,co+56)[0]
    crt_base=0x50000000
    uc.mem_map(crt_base,(crt_size+4095)&~4095)
    uc.mem_write(crt_base,crt[:struct.unpack_from('<I',crt,co+60)[0]])
    for i in range(struct.unpack_from('<H',crt,cp+6)[0]):
        section=co+struct.unpack_from('<H',crt,cp+20)[0]+40*i
        va,count,offset=struct.unpack_from('<III',crt,section+12)
        if count: uc.mem_write(crt_base+va,crt[offset:offset+count])
    reloc,reloc_size=struct.unpack_from('<II',crt,co+96+5*8)
    cursor=crt_base+reloc
    while cursor<crt_base+reloc+reloc_size:
        page,block=read(cursor,2)
        if not block:break
        for item in struct.unpack('<'+'H'*((block-8)//2),uc.mem_read(cursor+8,block-8)):
            if item>>12==3:
                address=crt_base+page+(item&4095)
                write(address,read(address)[0]+crt_base-old_base)
            else:assert item>>12==0
        cursor+=block
    export=crt_base+struct.unpack_from('<I',crt,co+96)[0]
    count,functions,names,ordinals=read(export+24,4)
    imports={'_libm_sse2_sin_precise':0x6fa7c48c,'_libm_sse2_cos_precise':0x6fa7c47c,'_libm_sse2_sqrt_precise':0x6fa7c4f8,'_CIatan2':0x6fa7c450,'_libm_sse2_asin_precise':0x6fa7c44c}
    resolved_crt_exports={}
    for i in range(count):
        name_address=crt_base+read(crt_base+names+i*4)[0]
        name=bytes(uc.mem_read(name_address,100)).split(b'\0',1)[0].decode()
        if name in imports:
            ordinal=struct.unpack('<H',uc.mem_read(crt_base+ordinals+i*2,2))[0]
            entry=crt_base+read(crt_base+functions+ordinal*4)[0]
            write(imports[name],entry)
            resolved_crt_exports[name]=dict(iat=hex(imports[name]),entry=hex(entry))
    assert resolved_crt_exports.keys()==imports.keys()

    extra = 0x10100000
    uc.mem_map(extra, 0x40000)
    subtable, subbuckets, subnodes = extra, extra + 0x100, extra + 0x400
    release_nodes, release_heap = extra + 0x1000, extra + 0x2000
    vertices, layer, samples = extra + 0x3000, extra + 0x3800, extra + 0x4000
    grid_objects = [extra + 0x5000, extra + 0x5080]
    current_path, acc_container, acc_map = extra + 0x5200, extra + 0x6000, extra + 0x6200
    generator, group, path, members = [extra + n for n in (0x7004, 0x8004, 0x9004, 0xa000)]
    newslots = extra + 0x22000
    uc.mem_write(newslots, bytes(uc.mem_read(slots, 5 * 8)))
    write(registry + 0xc, newslots)
    write(registry + 0x18, 128, 128)
    for index in range(5, 128):write(newslots + index * 8, index + 1 if index < 127 else -1, 0)
    # Existing registry slots0..4 remain authoritative and unchanged.
    task_classes = [(0x74736b2e, 0x6fd70f1c, 0x6fb78eb0, 0x50),
                    (0x7461736b, 0x6fd70ea4, 0x6fb78bcc, 0x34),
                    (0x74736b41, 0x6fd70eec, 0x6fb78d88, 0x38),
                    (0x74736b4f, 0x6fd70f04, 0x6fb78e1c, 0x38)]
    for index, (rawcode, factory, vt, psize) in enumerate(task_classes):
        write(factory, vt)
        run(0x6f06a270, factory + 4, psize, 8)
        block = extra + 0x10000 + index * 0x1000
        write(factory + 0x14, block)
        for n in range(8):write(block + n * 0xc0, block + (n + 1) * 0xc0 if n < 7 else 0)
        row = extra + 0x14000 + index * 0x100
        write(key_ptr, rawcode)
        run(0x6f198420, key_ptr)
        write(row + 4, uc.reg_read(UC_X86_REG_EAX))
        write(row + 0xc, words(class_bucket + 8, 1)[0])
        write(row + 0x18, rawcode)
        write(row + 0x6c, row + 0x80, factory)
        write(class_bucket + 8, row)
    write(wrapper_pool + 0x14, extra + 0x18000)
    for n in range(32):
        block = extra + 0x18000 + n * 0x100
        write(block, block + 0x100 if n < 31 else 0)
        run(0x6f056f20, block + 4)
        write(block + 4, 0x6fa8099c)
    classrow, cachebucket, cacheentry = extra + 0x15000, extra + 0x15100, extra + 0x15200
    write(key_ptr, 0x416d6f76)
    run(0x6f198420, key_ptr)
    write(classrow + 4, uc.reg_read(UC_X86_REG_EAX))
    write(classrow + 0xc, words(class_bucket + 8, 1)[0])
    write(classrow + 0x18, 0x416d6f76)
    write(class_bucket + 8, classrow)
    write(classrow + 0x38, cachebucket)
    write(classrow + 0x40, 0)
    write(cachebucket, 0, 0, cacheentry)
    for n, rawcode in enumerate((0x42706c79, 0x4255736c, 0x41736c61)):
        row = cacheentry + n * 0x20
        write(key_ptr, rawcode)
        run(0x6f198420, key_ptr)
        write(row, uc.reg_read(UC_X86_REG_EAX), row + 0x20 if n < 2 else 0)
        write(row + 0x14, rawcode, 0)
    # COrderTarget derives from COrder: existing positive class-query cache.
    order_cache_bucket, order_cache_entry = extra + 0x15400, extra + 0x15440
    write(key_ptr, 0x2b6f7264)
    run(0x6f198420, key_ptr)
    write(class_row + 0x38, order_cache_bucket)
    write(class_row + 0x40, 0)
    write(order_cache_bucket, 0, 0, order_cache_entry)
    write(order_cache_entry, uc.reg_read(UC_X86_REG_EAX), 0)
    write(order_cache_entry + 0x14, 0x2b6f7264, 1)
    write(actor, 0x6fa8099c)
    write(actor + 0x10, 0x2b616761)
    write(occupant + 8, subtable)
    for offset in (0x174, 0x19c, 0x1a8, 0x240):write(occupant + offset, -1, -1)
    write(ability + 0x84, -1, -1)
    write(ability + 0xcc, -1, -1)
    write(ability + 0xd8, -1, -1)
    floats(ability + 0x70, 256)
    floats(ability + 0x78, 1)
    write(subtable, 64 << 8, subbuckets)
    run(0x6f06a270, 0x6fd3cce4, 0x10, 64)
    write(0x6fd3ccf4, subnodes)
    for n in range(64):write(subnodes + n * 16, subnodes + (n + 1) * 16 if n < 63 else 0)
    for event in (0xd0148, 0xd014c, 0xd0162):run(0x6f0725b0, occupant, event, event, occupant)
    run(0x6f5fe6a0, ability, 1)
    run(0x6f0725b0, occupant, 0xd0166, 0xd0166, ability)
    clock = owner + 0x14
    write(clock + 0x10, release_heap)
    write(clock + 0x1c, 64, 1)
    write(clock + 0x38, release_nodes)
    for n in range(32):write(release_nodes + n * 0x40, release_nodes + (n + 1) * 0x40 if n < 31 else 0)
    write(terrain_object, 1)
    write(terrain_object + 0xe0, 25, vertices)
    ox, oy = struct.unpack('<2f', uc.mem_read(game + 0x6c, 8))
    for y in range(5):
        for x in range(5):
            packed = ((256 + int(ox / 128) + x) << 23) | ((256 + int(oy / 128) + y) << 14) | 0x2000
            write(vertices + (y * 5 + x) * 28, packed, y * 5 + x)
            uc.mem_write(vertices + (y * 5 + x) * 28 + 10, bytes([0xa5]))
    write(terrain_object + 0x79c, layer)
    write(layer, 16, 16)
    floats(layer + 8, 32, 32)
    write(layer + 0x18, samples)
    write(occupant_mover, 0x6fa9129c, owner + 0x200, 0)
    write(occupant_mover + 0x94, *grid_objects)
    write(occupant_mover + 0x9c, -1, -1)
    write(occupant_mover + 0xa8, current_path)
    write(occupant_mover + 0xd0, 4, 4)
    floats(occupant_mover + 0x90, .25)
    for obj, grid in zip(grid_objects, [proximity, fine]):
        write(obj + 0x2c, grid)
        write(obj + 0x34, 0x01000001)
    write(proximity + 0x84, 64, 1)  # Existing occupant record occupies slot0.
    write(proximity + 0xb0, 1)
    write(proximity + 0x98, extra + 0xb000)
    write(proximity + 0xac, 0xffffff)
    write(owner + 0x250, acc_container)
    write(acc_container + 0x1c, acc_map)
    write(acc_map + 0x54, 0, 0, 16, 16)
    for ptr, ctor, offset in [(generator, 0x6f169220, 0x638), (group, 0, 0x678), (path, 0x6f1657c0, 0x958)]:
        if ctor:run(ctor, ptr)
        else:
            write(ptr, 0x6fa90d64)
            write(ptr + 0x14, -1, -1)
            write(ptr + 0x1c, 0x6fa90d5c, members, 12 * 0x2c, members, 12 * 0x2c, 0, 12, 0)
            write(ptr + 0x40, -1, -1)
        write(owner + offset + 0x14, ptr - 4, 0, 0)
    write(group + 0x28, members)
    write(group + 0x34, 12)
    floats(radius_entry + 0x1d8, 1, 522)
    floats(game + 0x80, 522)  # Configured global movement-speed maximum.
    for entry in [0x6f016290, 0x6f0162b0, 0x6f0162c0, 0x6f0162d0, 0x6f016210, 0x6f016220, 0x6f016230, 0x6f016240]:run(entry, 0)
    write(0x6fd687a8, extra + 0x23000)
    write(extra + 0x233e0, 1)
    pi = struct.unpack('<f', uc.mem_read(0x6fa8af08, 4))[0]
    angle = bits(10 * pi / 180)
    run(0x6f352db0, key, angle)
    run(0x6f352dd0, key, angle)
    run(0x6f352cf0, key, bits(20))
    from collections import deque
    dispatch_tail = deque(maxlen=32)
    def trace_dispatch(machine, address, size, data):
        dispatch_tail.append((hex(address), hex(machine.reg_read(UC_X86_REG_ECX)), hex(machine.reg_read(UC_X86_REG_EAX))))
    def invalid_dispatch(machine, access, address, size, value, data):
        print(f'Widget dispatch invalid memory {address:#x} at {machine.reg_read(UC_X86_REG_EIP):#x}; stack={words(machine.reg_read(UC_X86_REG_ESP), 12)}; tail={list(dispatch_tail)}; events={dispatch_events}', flush=True)
        return False
    uc.hook_add(UC_HOOK_MEM_INVALID, invalid_dispatch)
    dispatch_events = []
    def observe_task_dispatch(machine, address, size, data):
        packet = words(machine.reg_read(UC_X86_REG_ESP) + 4, 1)[0]
        dispatch_events.append((hex(address), hex(words(packet + 8, 1)[0])))
    for address in (0x6f071da0, 0x6f5fda10, 0x6f690490):
        uc.hook_add(UC_HOOK_CODE, observe_task_dispatch, begin=address, end=address)
    run(0x6f15fb40, occupant_mover)
    if args.stock_mask:
        # Observed hfoo category/query; custom radius8 and blank presentation
        # cache remain supplied backing, not authored resource decoding.
        write(occupant + 0x30, 0x68666f6f)
        write(key_ptr, 0x68666f6f)
        run(0x6f198420, key_ptr)
        footman_hash = uc.reg_read(UC_X86_REG_EAX)
        write(radius_entry, footman_hash)
        write(radius_entry + 0x14, 0x68666f6f)
        write(radius_entry + 0x1ac, 0xca, 2)
        footman_resource = extra + 0x26000
        uc.mem_write(footman_resource, bytes(uc.mem_read(resource_entry, 0x100)))
        write(footman_resource + 4, footman_hash)
        write(footman_resource + 0x18, 0x68666f6f)
        write(footman_resource + 0xcc, 0)
        write(resource_entry + 8, footman_resource)
        write(resource_bucket, 4)  # Chain next pointer is row+8, preserving vtable bytes.
        run(0x6f1657c0, current_path)
        run(0x6f678b50, occupant)
        query_mask = uc.reg_read(UC_X86_REG_EAX)
        run(0x6f678b60, occupant)
        category = uc.reg_read(UC_X86_REG_EAX)
        assert (query_mask, category) == (2, 0xca)
        run(0x6f05c7e0, occupant + 0x164, category, query_mask)
        assert words(current_path + 0x9c, 1) == [0x02000002]
        assert words(grid_objects[1] + 0x34, 1) == [0x010000ca]
    if args.solid_footprint:
        write(texture + 8, 9, 9)
        write(texture + 0x20, extra + 0x28000)
        uc.mem_write(extra + 0x28000, bytes([2] * 81))
    pre_dispatch_unit_references = words(occupant + 4, 1)[0]
    order_attempts.clear()
    proposals.clear()
    proposal_inputs.clear()
    source_grid = words(occupant_mover + 0x78, 2)
    preceding_rebuilds = len(method_rebuilds)
    uc.ctl_flush_tb()
    trace_hook = uc.hook_add(UC_HOOK_CODE, trace_dispatch)
    uc.ctl_flush_tb()
    run(0x6f6544f0, widget)
    uc.hook_del(trace_hook)
    assert uc.reg_read(UC_X86_REG_EIP) == stop
    assert words(0, 1) == [0]
    full_displacement_returns = 1
    assert len(proposals) == len(order_attempts) == len(factory_requests) == len(dispatch_orders) == 1
    assert order_attempts[0] == (0xd0012, 0, *proposals[0])
    assert factory_requests[0] == (0xd0012, 0, 0, *proposals[0], 0, 0)
    if not args.solid_footprint:
        assert proposals[0] == (3292303111, 3278893763)  # Prior staged snapshot comparison.
    order = dispatch_orders[0]
    order_identity = words(order + 0xc, 2)
    assert words(occupant + 0x19c, 2) == words(occupant + 0x1a8, 2) == order_identity
    assert words(occupant + 0x1b4, 1) == [1]
    assert words(occupant + 0x194, 1) == [7]
    retained_unit_references = words(occupant + 4, 1)[0]
    assert retained_unit_references == pre_dispatch_unit_references
    assert words(ability + 0x20, 1) == [0x884]
    assert words(occupant_mover + 0x9c, 2) == words(group + 0x14, 2), ('group identities', words(occupant_mover + 0x9c, 2), words(group + 0x14, 2))
    assert words(group + 0x38, 2) == [1, path]
    assert words(members, 2) == [3, 103] and words(members + 0x14, 1) == [occupant_mover]
    target_grid = [(subtract(value, bits(origin)) - 0x02800000) & 0xffffffff
                   for value, origin in zip(proposals[0], (ox, oy))]
    assert words(path + 0x1c, 6) == target_grid * 3
    assert words(group + 0x4c, 4) == target_grid + source_grid
    assert words(path + 0x84, 4) == [(5000 << 16) | 700, 0x400000, 0, 0]
    assert words(owner + 0x3b8, 1) == [group]
    assert words(occupant_mover + 0x88, 1) == [bits(8)], words(occupant_mover + 0x88, 1)
    assert words(query + 0x40, 1) == words(game + 0x64, 1) == [0]
    assert len(method_rebuilds) == preceding_rebuilds + 1
    task_codes = []
    task_identity = words(occupant + 0x174, 2)
    while task_identity[0] != 0xffffffff:
        assert len(task_codes) < 16
        wrapper = words(newslots + task_identity[0] * 8 + 4, 1)[0]
        task = words(wrapper + 0x54, 1)[0]
        assert words(task + 0xc, 2) == task_identity
        task_codes.append(words(task + 0x30, 1)[0])
        task_identity = words(task + 0x24, 2)
    assert task_codes == [0xd016b, 0xd0165, 0xd014a, 0xd0148, 0xd0166, 0xd0162]
    displacement_evidence = dict(uninterrupted=True, matches_staged_target_bits=not args.solid_footprint, pre_dispatch_unit_references=pre_dispatch_unit_references, retained_unit_references=retained_unit_references, user_order_identity=order_identity, remaining_task_codes=list(map(hex, task_codes)),
        target_world_bits=list(proposals[0]), target_grid_bits=target_grid,
        group_identity=words(group + 0x14, 2), path_identity=words(path + 0x14, 2), events=dispatch_events,
        scope='One uninterrupted original6544f0 invocation with all provisioning before entry; full680320 cancellation, real factories, user-order publication, task dispatch and Move acceptance return. Group/path/membership, callback reference release and final widget refresh asserted. Stock-style speed bounds and shipped original CRT math provisioned; no travel/arrival claim.')
    # Follow the exact widget-produced command; preserve footprint records and
    # its original random proposal. Stock mode retains its produced mask;
    # the default control supplies a terrain-only individual mask.
    from verify_wc3_pathing_numeric import multiply
    uc.mem_map(0x10200000, 0x90000)
    nodes, search_heap, member_route, group_route, member_coarse = [0x10200000 + n for n in (0, 0x30000, 0x60000, 0x64000, 0x68000)]
    write(system + 0x1c, fine, 1)
    write(system + 0x30, nodes)
    write(system + 0x3c, 4096, 0)
    write(system + 0x50, search_heap)
    write(system + 0x5c, 32768, 1, -3, 100000, 0)
    for ptr, route in ((current_path, member_route), (path, group_route)):
        write(ptr + 0x40, route)
        write(ptr + 0x4c, 1024, 0)
        write(ptr + 0x60, member_coarse if ptr == current_path else group_route + 0x2000)
        write(ptr + 0x6c, 1024, 0)
    write(current_path + 0x84, 700 | (400 << 16))
    if not args.stock_mask:
        write(current_path + 0x9c, 0x02000000)
    travel_mask = 0x02000002 if args.stock_mask else 0x02000000
    assert words(current_path + 0x9c, 1) == [travel_mask]
    for row in range(16):
        for kind, (limit, reload, budget) in enumerate(((5000, 3, 800), (2000, 2, 300), (400, 2, 900), (700, 1, 1100))):
            write(0x6fd53a90 + row * 0x70 + kind * 0x1c, limit | (reload << 16), budget, 0, 0, 0, 0, 0)
    write(owner + 0x538, 100)
    write(game + 8, 1)  # Enable native callback delivery, as in retail running-map state.
    floats(xptr, .5)
    run(0x6f05c8c0, occupant + 0x164, xptr)
    run(0x6f05c890, occupant + 0x164, xptr)
    recovery_observations = []
    def observe_recovery(machine, address, size, data):
        sp = machine.reg_read(UC_X86_REG_ESP)
        recovery_observations.append(dict(clock_bits=words(clock + 0x40, 1)[0],
            argument=words(sp + 4, 1)[0], task_head=words(occupant + 0x174, 2),
            user_head=words(occupant + 0x19c, 2)))
    uc.hook_add(UC_HOOK_CODE, observe_recovery, begin=0x6f5fb190, end=0x6f5fb190)
    arrival_observations = []
    def observe_arrival(machine, address, size, data):
        arrival_observations.append(dict(clock_bits=words(clock + 0x40, 1)[0],
                                        user_head=words(occupant + 0x19c, 2)))
    uc.hook_add(UC_HOOK_CODE, observe_arrival, begin=0x6f5fa7a0, end=0x6f5fa7a0)
    footprint_before = [words(obj + 0x34, 1)[0] for obj in objects[0]]
    def active_widget_cell(x, y):
        record = words(cells + 4 * (y * width + x), 1)[0] & 0xffffff
        seen = set()
        while record != 0xffffff:
            assert record not in seen
            seen.add(record)
            link, obj = words(links + record * 8, 2)
            if obj == objects[0][0]:return link & 0xff000000 == 0x01000000
            record = link & 0xffffff
        return False
    source_xy = [struct.unpack('<f', struct.pack('<I', value))[0] for value in source_grid]
    assert active_widget_cell(*map(math.floor, source_xy))
    uc.ctl_flush_tb()
    run(0x6f16c150, group)
    initial_route = words(member_route, words(current_path + 0x50, 1)[0] * 2)
    write(owner + 0x210, extra + 0x21000)
    write(owner + 0x230, 0)
    floats(clock + 0x48, 8)
    floats(xptr, 1 / 32)
    trajectory = []
    route_states = []
    for tick in range(1, 129):
        if words(occupant + 0x174, 1) == [0xffffffff]:break
        run(0x6f054190, xptr, edx=clock)
        oldpos, oldvel = words(occupant_mover + 0x78, 2), words(occupant_mover + 0x80, 2)
        run(0x6f16c150, group)
        expected_position = [add(p, multiply(v, bits(1 / 32))) for p, v in zip(oldpos, oldvel)]
        actual_position = words(occupant_mover + 0x78, 2)
        assert actual_position == expected_position, (tick, actual_position, expected_position)
        trajectory.append(dict(tick=tick, position_bits=actual_position, velocity_bits=words(occupant_mover + 0x80, 2)))
        assert [words(obj + 0x34, 1)[0] for obj in objects[0]] == footprint_before
        assert words(current_path + 0x9c, 1) == [travel_mask]
        count = words(current_path + 0x50, 1)[0]
        route_states.append(dict(tick=tick, fine_count=count,
                                 fine_index=words(current_path + 0x74,1)[0],
                                 adaptive_index=words(current_path + 0x78,1)[0],
                                 flags=words(current_path + 0x88,1)[0],
                                 route=words(member_route,count * 2)))
    travel_outcome = dict(initial_route=initial_route, trajectory=trajectory, arrivals=arrival_observations,
                         task_head=words(occupant + 0x174, 2), user_head=words(occupant + 0x19c, 2),
                         queue_count=words(occupant + 0x1b4, 1)[0], action=words(occupant + 0x194, 1)[0],
                         scope='Exact widget proposal and blocking footprint retained; original fresh search and up to128 elapsed group ticks. Outcome observed, not forced.')
    arrival_tick = 7 if args.solid_footprint else 13 if args.stock_mask else 7
    assert len(trajectory) == arrival_tick and len(arrival_observations) == 1
    assert arrival_observations[0]['user_head'] == order_identity
    intermediate = [bits(7.5), bits(8.5), bits(6.5), bits(8.5)] if args.stock_mask else [bits(7.5)] * 2
    if args.solid_footprint:
        assert initial_route == []
        assert all(row['position_bits'] == source_grid and row['velocity_bits'] == [0, 0] for row in trajectory)
        assert len(recovery_observations) == 1 and recovery_observations[0]['argument'] == 1
    else:
        assert initial_route == target_grid + intermediate + source_grid
        assert recovery_observations == []
    assert active_widget_cell(7, 7)  # The intermediate route point also lies in A's footprint.
    assert words(occupant + 0x174, 2) == words(occupant + 0x19c, 2) == words(occupant + 0x1a8, 2) == [0xffffffff] * 2
    assert words(occupant + 0x1b4, 1) == words(occupant + 0x194, 1) == words(ability + 0x20, 1) == [0]
    assert words(occupant_mover + 0x9c, 2) == [0xffffffff] * 2
    assert words(occupant_mover + 0x80, 2) == [0, 0]
    for _ in range(2):run(0x6f054190, xptr, edx=clock)
    run(0x6f16c150, group)
    assert words(group + 0x38, 1) == [0]
    for offset, obj in ((0x678, group), (0x958, path)):
        assert words(owner + offset + 0x18, 1) == [0]
        assert words(owner + offset + 0x14, 1) == [obj - 4]
    returned_payloads = {}
    for rawcode, factory, block, count in [(0x6f726474, 0x6fd70e44, order_block, 4)] + [
            (rawcode, factory, extra + 0x10000 + n * 0x1000, 8) for n, (rawcode, factory, vt, psize) in enumerate(task_classes)]:
        assert words(factory + 0xc, 1) == [0]
        free = set()
        item = words(factory + 0x14, 1)[0]
        while item:
            assert item not in free
            free.add(item)
            item = words(item, 1)[0]
        assert free == {block + n * 0xc0 for n in range(count)}
        returned_payloads[hex(rawcode)] = count
    assert words(wrapper_pool + 0x18, 1) == [0]
    assert words(registry + 0x48, 1) == [5]
    travel_outcome.update(outcome='cant-path' if args.solid_footprint else 'arrived', arrival_tick=arrival_tick, footprint_start_and_middle_cells_active=True,
                          payload_free_lists_restored=returned_payloads, registry_live_after_cleanup=5,
                          scope=f'Unchanged widget footprint/proposal; fresh original search, arrival after{arrival_tick}ticks, queues drain and group/path/payload/wrapper reclamation. Query mask{travel_mask:08x}; stock mode uses original getter/bridge before admission, control supplies terrain-only mask.')
    frozen = json.loads(args.fixture.read_text())
    escape_words = {key: value for key, value in travel_outcome.items() if key != 'scope'}
    assert frozen['binary_sha256'] == digest and frozen['crt_sha256'] == crt_digest
    assert frozen['supplied_query_mask'] == travel_mask
    assert frozen['route_states'] == route_states, 'widget escape route progression differs from frozen original states'
    assert frozen['target_world_bits'] == list(proposals[0]) and frozen['target_grid_bits'] == target_grid
    assert escape_words == frozen['travel'], 'widget escape journey differs from frozen original words/lifetimes'
    travel_digest = hashlib.sha256(json.dumps(escape_words, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    # Independent destruction fixture. Full650c00 reaches the real Storm403
    # array free after retiring all four original registered footprint regions.
    mover_regions = [0x10086000, 0x10086080]
    proximity_dirty, region_registry, region_slots = 0x10086200, 0x10087000, 0x10087100
    write(0x6fd6860c, region_registry)
    write(region_registry + 0xc, region_slots)
    write(region_registry + 0x18, 8, 8)
    additional_maps.add(proximity)
    write(proximity + 0x84, 64, 0)
    write(proximity + 0x98, proximity_dirty)
    write(proximity + 0xa8, 1)
    destructions = []
    def stop_at_storm_free(machine, address, size, data):
        sp = machine.reg_read(UC_X86_REG_ESP)
        pointer, label, line, flags = words(sp + 4, 4)
        destructions.append((pointer, line, flags, words(sp, 1)[0]))
        machine.emu_stop()
    uc.hook_add(UC_HOOK_CODE, stop_at_storm_free, begin=0x6f07c678, end=0x6f07c678)
    uc.ctl_flush_tb()
    for state in [0, 1]:
        write(0, 0)  # Begin independent fixture after the explicit dispatch stop.
        write(game + 0x64, 0)
        write(query + 0x1c, 0)
        write(query + 0x40, 0)
        floats(game + 0x6c, 0, 0, 512, 512)
        write(cells, *([0xffffff] * 256))
        write(fine + 0x88, 0)
        write(fine + 0xac, 0xffffff, 0, 0)
        write(dirty, *([0] * 8))
        write(proximity_cells, *([0xffffff] * 9))
        write(proximity + 0x88, 0)
        write(proximity + 0xac, 0xffffff, 0, 0)
        write(proximity_dirty, 0)
        write(region_registry + 0x40, -1)
        write(region_registry + 0x48, 8)
        active = {}
        for group in range(2):
            write(collections[group], 4, 4, lists[group])
            write(lists[group], *objects[group])
            for index, (obj, mask) in enumerate(zip(objects[group], region_masks)):
                slot = group * 4 + index
                write(region_slots + slot * 8, -2, obj)
                write(obj + 0x14, slot, 200 + slot)
                write(obj + 0x34, 0x01000000 | mask, 0, 0, 0x10000000)
        write(texture + 8, 2, 3)
        write(texture + 0x20, pixels)  # Independent destruction texture, including after solid variant.
        uc.mem_write(pixels, bytes([0xff] * 6))
        floats(center, 224, 240)
        for group in range(2):
            floats(center, 224 + group * 32, 240)
            run(0x6f22e9c0, texture, center, 0, 0x6f652b40, collections[group])
            for wx, wy in sample_points(2, 3, 0, 224 + group * 32, 240):
                for mask in region_masks:
                    active[int(wx / 32), int(wy / 32), group, mask] = True
        run(0x6f15d360, owner, 0, 0)
        blocked = levels()
        assert blocked == expected_levels(active)
        for obj, tilemap in zip(mover_regions, [proximity, fine]):
            uc.mem_write(obj, bytes(0x80))
            write(obj + 0x2c, tilemap)
            write(obj + 0x34, 0x01000000)
        floats(mover + 0x78, 7, 7.5, 0, 0)
        floats(mover + 0x90, .25)
        write(mover + 0x94, *mover_regions)
        write(mover + 0xc8, 0)
        write(widget + 0x34, collections[0])
        method_rebuilds.clear()
        run(0x6f651160, 1)
        write(stack, stop, state, 1)
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, widget)
        uc.emu_start(0x6f650c00, stop, count=1000000)
        assert uc.reg_read(UC_X86_REG_EIP) == 0x6f07c678
        assert destructions[-1][:3] == (lists[0], 0xfffffffe, 0)
        assert words(widget + 0x34, 1) == [collections[0]]  # Outerfree/clear not reached.
        assert words(lists[0], 4) == [0] * 4
        assert words(lists[1], 4) == objects[1]
        assert words(region_registry + 0x48, 1) == [4]
        assert words(region_registry + 0x40, 1) == [3]
        for index, obj in enumerate(objects[0]):
            assert words(obj + 0x14, 2) == [0xffffffff, 0xffffffff]
            assert words(obj + 0x38, 2) == [0xffffffff, 6]
            assert words(region_slots + index * 8, 2) == [0xffffffff if index == 0 else index - 1, 0]
        assert words(mover_regions[0] + 0x34, 1) == [0x09000000 | (0x04000000 if state else 0)]
        assert words(mover_regions[1] + 0x34, 1) == [0]
        assert levels() == blocked and not method_rebuilds
        # Explicit rebuild is a separate observation;650c00 did not reach it.
        write(0, 0)
        run(0x6f15d360, owner, 0, 0)
        assert levels() == expected_levels({key: value for key, value in active.items() if key[2] == 1})
        assert levels() != blocked
    report = dict(binary_sha256=digest, snap_engine_cases=snap_engine_cases, clamp_engine_cases=clamp_engine_cases,
                  lifecycle_cases=cases, raster_bounds_rebuild_stages=stages,
                  actual_callback_calls=total_callbacks, actual_cell_records=total_records,
                  widget_escape_arrival_tick=arrival_tick, widget_escape_supplied_query_mask=travel_mask,
                  widget_escape_solid_footprint=args.solid_footprint, widget_escape_recovery=recovery_observations,
                  widget_escape_original_mask_publication=args.stock_mask, widget_escape_route_states=route_states,
                  widget_escape_profile=dict(rawcode='hfoo', category=0xca, query=2, radius_world_bits=bits(8)) if args.stock_mask else None,
                  widget_escape_trajectory_sha256=travel_digest,
                  widget_escape_fixture_sha256=hashlib.sha256(args.fixture.read_bytes()).hexdigest(),
                  travel_outcome=travel_outcome, full_displacement_returns=full_displacement_returns, displacement_evidence=displacement_evidence, crt_sha256=crt_digest,
                  destruction_prefixes=dict(count=len(destructions), stop="6f07c678 Storm403", first_free="collection pointer array", observations=destructions, scope="Actual650c00 flags/pose update then063b40 retires four A regions: registry live8->4, A identities/stamps invalid, pending records6 each, array entries zero. Stops at mandatory Storm403 (return063bb1); array/collection free, widget34 clearing and method refresh not executed. Separate15d360 rebuild removes A-only coverage and preserves translated overlapping B. Region object pool return/compaction not executed."),
                  constructed_order_observation=dict(count=1, entry="6f680320", order=hex(dispatch_orders[0]), scope="Resumed accepted Move prefix through full original69bd80 COrderTarget creation, constructor, identity registration and point initialization; observed during uninterrupted widget execution; see displacement_evidence."),
                  accepted_move_observation=dict(count=1, entry="6f69bd80", request=list(factory_requests[0]), scope="Original attached Move ability600620/43b6e0/5fba80/4197d0 accepts0xd0012; observed during uninterrupted widget execution; see displacement_evidence."),
                  nonempty_geometry_cases=nonempty_cases, nonempty_order_rejections=order_rejections,
                  nonempty_observation='Full6544f0→proximity query→654090: one registered occupant, original RNG draws and radius-cache lookup, exact nearest-edge proposal from independent numeric geometry using observed jitter; flag5c bit4 suppresses dispatch, otherwise real69dd60 receives order0xd0012 and rejects empty ability list with0xdd. Pose unchanged; reference/query lifetimes balanced.',
                  widget_reapply_lifecycles=reapply_cases, empty_proximity_cell_visits=proximity_visits, widget_reapply_calls=reapply_calls, widget_paired_removal_calls=removal_calls,
                  widget_reapply_sequence='Distinct A reapply, B reapply, A remove preserving B occupancy, B remove restoring empty hierarchy; 48 deferred and 48 immediate gate cases.',
                  widget_reapply_dependencies='Original terrain record low nibble (0xa5 -> height 5), empty overlay list, preallocated reusable query and empty proximity grid; query active/depth reset after every call; callback654090 never invoked.',
                  widget_method_full_calls=widget_cases, widget_method="6f6514d0 via actual CUnit/pose vtables; resource-cache hit; real651160 gate",
                  widget_gate_cases={'deferred': widget_cases // 2, 'immediate': widget_cases // 2},
                  widget_observation='Identical expected fine removal records for both gate values; gate zero retains blocked hierarchy until explicit rebuild, gate one calls original bounds rebuild and restores empty hierarchy.',
                  snapping='World-coordinate truncation toward zero to a multiple of 64, plus sign(value) * (32 * ((rotated_extent >> 1) & 1) + 16 * (rotated_extent & 1)); exercised strictly inside world bounds.',
                  sequence='A insert, overlapping B insert, A remove, B remove; explicit bounds rebuild after every raster',
                  entries=['6f22e9c0', '6f652b40', '6f650a70', '6f063e50', '6f14d9e0', '6f22f1d0', '6f04e0b0', '6f15d360', '6f6514d0', '6f6524c0', '6f344760', '6f058900', '6f252b30', '6f22f410', '6f651160', '6f6544f0', '6f78bc90', '6f744040', '6f04c5d0', '6f05ef40', '6f654090', '6f674280', '6f1b7130', '6f69dd60', '6f600620', '6f69bd80', '6f680970', '6f689a60', '6f650c00', '6f063b40', '6f14dae0', '6f1cbfb0', '6f680320', '6f673fe0', '6f691c70', '6f67abe0', '6f5fd270', '6f67df00', '6f16c150', '6f054190', '6f5fa7a0'],
                  scope='Full original raster/callback/record/bounds/rebuild functions and CUnit inherited reapply/removal; authentic vtables, cached resource lookup, registered stationary poses, original snapping and refresh gate. Four orientations, odd/even nonsquare dimensions, two world origins, fractional offsets, mixed masks and overlapping distinct region collections. Independent geometry and hierarchy models. Preallocated cache, terrain, collections and query storage. Excludes widget creation and completed destruction, authored resource decoding, cache-miss allocation, snapping at map edges, independent RNG model, multiple displacement targets and crowds with multiple query occupants. Nonempty callback geometry and actual order rejection are included; one attached-Move case constructs the real COrderTarget, publishes the user order and accepts its generated movement task before full6544f0 return, then preserves footprint records through fresh search, the declared seven/13-tick variant, completion and reclamation (solid9x9 stock mode cannot path, retains zero velocity and drains recovery); default terrain-only mask is supplied, stock mode executes original observed profile getters/bridge before admission. Full owner cadence remains excluded. Two destruction prefixes stop at genuine Storm403 after registry retirement and before collection free.')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
