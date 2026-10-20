#!/usr/bin/env python3
"""Verify original WC3 terrain coordinate edits and clipped hierarchy updates.

No stubs or retail bytes. Synthetic preallocated maps, exact dyadic coordinates,
independent cell/rectangle/reducer models. Does not execute map constructors.
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
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EAX, UC_X86_REG_EBP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--coordinate-fixture', type=Path, help='freeze complete original world-to-grid boundary words')
    parser.add_argument('--point-order-fixture',type=Path,help='freeze original05b986..05ba9e point-bound clipping words')
    parser.add_argument('--engine-library', type=Path, help='compare production coordinate arithmetic')
    args = parser.parse_args()
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        engine.pathing_world_grid.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
        engine.pathing_point_order_clip.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
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
    cells, xptr, yptr, rect = 0x10060000, 0x10008000, 0x10008010, 0x10008020
    maps = [0x10009000 + n * 0x100 for n in range(4)]
    storage = [0x10010000 + n * 0x10000 for n in range(4)]
    stack, stop = 0x20008000, 0x30000000

    def write(address, *values):
        uc.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def floats(address, *values):
        uc.mem_write(address, struct.pack('<' + 'f' * len(values), *values))

    def words(address, count=1):
        return list(struct.unpack('<' + 'I' * count, uc.mem_read(address, 4 * count)))

    def run(entry, ecx, *arguments, edx=0):
        write(stack, stop, *arguments)
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        uc.reg_write(UC_X86_REG_EDX, edx)
        uc.emu_start(entry, stop, count=2000000)
        assert uc.reg_read(UC_X86_REG_EIP) == stop, hex(entry)

    for address, value in [(0x6fd3c740, -1), (0x6fd3c744, 0), (0x6fd3c748, 1)]:
        floats(address, value)
    write(0x6fd53a48, owner)
    write(0x6fd3c82c, game)
    write(owner + 0x24c, system)
    write(system + 0x1c, fine)
    write(owner + 0x23c, *maps)
    write(fine + 0x28, cells)
    observed = []

    def cell_entry(machine, address, size, data):
        observed.append(machine.reg_read(UC_X86_REG_ECX))

    uc.hook_add(UC_HOOK_CODE, cell_entry, begin=0x6f054000, end=0x6f054000)
    masks = [0x06000006, 0x80000080, 0x40000040, 0x04000004]
    assert words(0x6fce4570, 4) == masks
    coordinate_cases = composed_cases = rectangle_cases = 0
    selected_cells = set()
    dimensions = [(16, 16), (17, 23), (31, 18)]
    origins = [(0, 0), (-1024, -2048), (512, -256)]

    def read_levels():
        return [words(data, w * h * 2) for data, (w, h) in zip(storage, sizes)]

    def set_levels(levels):
        for data, values in zip(storage, levels):
            write(data, *values)

    def reduce_states(states):
        return 0 if all(s == 0 for s in states) else 1 if all(s == 1 for s in states) else 2

    def model_update(levels, terrain, rectangle, mode):
        result = [values[:] for values in levels]
        y0, x0, y1, x1 = rectangle if rectangle is not None else (0, 0, height, width)
        y0, x0, y1, x1 = max(0, y0), max(0, x0), min(height, y1), min(width, x1)
        if y0 >= y1 or x0 >= x1:
            return result
        for level, (w, h) in enumerate(sizes):
            scale = 2 << level
            for y in range(y0 // scale, min(h, y1 // scale + 1)):
                for x in range(x0 // scale, min(w, x1 // scale + 1)):
                    byte = 0
                    for lane, mask in enumerate(masks):
                        states = []
                        for dy, dx in itertools.product(range(2), repeat=2):
                            cy, cx = 2 * y + dy, 2 * x + dx
                            if level == 0:
                                blocked = cy >= height or cx >= width or bool(terrain[cy * width + cx] & mask & 0xff000000)
                                states.append(int(blocked))
                            else:
                                pw, ph = sizes[level - 1]
                                states.append(1 if cy >= ph or cx >= pw else
                                              (result[level - 1][2 * (cy * pw + cx) + 1] >> (30 - 2 * lane)) & 3)
                        state = 0 if level == 0 and mode else reduce_states(states)
                        byte |= state << (6 - 2 * lane)
                    index = 2 * (y * w + x) + 1
                    result[level][index] = (result[level][index] & 0xffffff) | (byte << 24)
        return result

    def checked_update(terrain, rectangle=None, mode=0):
        before = read_levels()
        expected = model_update(before, terrain, rectangle, mode)
        if rectangle is not None:
            write(rect, *rectangle)
        run(0x6f15d360, owner, rect if rectangle is not None else 0, mode)
        actual = read_levels()
        if actual != expected:
            for level, (a, e) in enumerate(zip(actual, expected)):
                for index, (av, ev) in enumerate(zip(a, e)):
                    if av != ev:
                        raise AssertionError((width, height, rectangle, mode, level, index, hex(av), hex(ev)))
        assert words(cells, width * height) == terrain
        return actual

    for width, height in dimensions:
        write(fine + 0x3c, width, height)
        write(fine + 0x54, 0, 0, height, width)
        sizes = [(((width + 16) // 2 + 1) >> n, ((height + 16) // 2 + 1) >> n) for n in range(4)]
        initial = []
        for level, (tilemap, data, (w, h)) in enumerate(zip(maps, storage, sizes)):
            write(tilemap + 0x28, data)
            write(tilemap + 0x3c, w, h)
            floats(tilemap + 0x64, 2 << level, 1 / (2 << level))
            # Preserve metadata, but keep byte +6 zero: nonzero means special edge.
            values = [v for n in range(w * h) for v in (0x12340000 + n, 0x00005678)]
            initial.append(values)
        terrain = [0xffffff] * (width * height)
        write(cells, *terrain)
        set_levels(initial)
        baseline = checked_update(terrain)
        xs = [-1, -0.25, 0, 0.25, 1, 1.75, width - 0.25, width, width + 0.25]
        ys = [-1, -0.25, 0, 0.25, 1, 1.75, height - 0.25, height, height + 0.25]
        for (ox, oy), x, y, mask in itertools.product(origins, xs, ys, [2, 4, 0x40, 0x80]):
            floats(game + 0x6c, ox, oy)
            floats(xptr, ox + 32 * x)
            floats(yptr, oy + 32 * y)
            ix, iy = math.floor(x), math.floor(y)
            valid = 0 <= ix < width and 0 <= iy < height
            pointer = cells + 4 * (iy * width + ix) if valid else 0
            observed.clear()
            # Set, repeated set, clear, repeated clear cover idempotence and reversal.
            for blocked in [1, 1, 0, 0]:
                run(0x6f04d870, xptr, mask, blocked, edx=yptr)
                if valid:
                    terrain[iy * width + ix] = (0xffffff | mask << 24) if blocked else 0xffffff
                assert words(cells, width * height) == terrain, (width, height, ox, oy, x, y, mask, blocked)
                assert observed[-1] == pointer, (x, y, hex(observed[-1]), hex(pointer))
                assert read_levels() == baseline
                coordinate_cases += 1
            assert observed == [pointer] * 4
            if valid:
                selected_cells.add((width, height, ix, iy))
        # Composed mutation/recompute/reversal on corners, odd cells and interior.
        for (ox, oy), (x, y), mask in itertools.product(origins,
                [(0, 0), (width - 1, 0), (0, height - 1), (width - 1, height - 1), (1, 1), (width // 2, height // 2)],
                [2, 4, 0x40, 0x80]):
            floats(game + 0x6c, ox, oy)
            floats(xptr, ox + 32 * (x + 0.25))
            floats(yptr, oy + 32 * (y + 0.75))
            run(0x6f04d870, xptr, mask, 1, edx=yptr)
            terrain[y * width + x] |= mask << 24
            assert read_levels() == baseline
            changed = checked_update(terrain, (y, x, y + 1, x + 1))
            # Edge blocks can already be mixed because missing fine cells block.
            set_levels(initial)
            assert checked_update(terrain) == changed
            run(0x6f04d870, xptr, mask, 0, edx=yptr)
            terrain[y * width + x] = 0xffffff
            assert read_levels() == changed
            assert checked_update(terrain, (y, x, y + 1, x + 1)) == baseline
            composed_cases += 1
        # Every cell has all terrain lanes blocked. Test clipping and mode-1 clear
        # against a separate geometric model, then recompute the same rectangle.
        terrain = [0xffffffff] * (width * height)
        write(cells, *terrain)
        blocked_baseline = checked_update(terrain)
        rectangles = [None, (0, 0, height, width), (-2, -3, 2, 3),
                      (height - 2, width - 2, height + 3, width + 4),
                      (-4, -4, -1, -1), (height, width, height + 4, width + 4),
                      (2, 3, 2, 5), (3, 4, 1, 2), (1, 1, 2, 2),
                      (1, 3, height - 1, width - 1)]
        for rectangle in rectangles:
            set_levels(blocked_baseline)
            checked_update(terrain, rectangle, 1)
            assert checked_update(terrain, rectangle, 0) == blocked_baseline
            rectangle_cases += 1
        # Source/target exclusion must restore the original coarse ground6,
        # including no-fly-only4 terrain that remains fine-ground2 pathable.
        terrain=[0xffffff]*(width*height)
        for y in (8,9):
            for x in (8,9):terrain[y*width+x]=0x04ffffff
        write(cells,*terrain);set_levels(initial)
        before=checked_update(terrain)
        at=2*(4*sizes[0][0]+4)+1
        assert before[0][at]>>24==0x41 # native ground lane0 + flight lane6
        cleared=checked_update(terrain,(8,8,10,10),1)
        assert cleared[0][at]>>24==0
        restored=checked_update(terrain,(8,8,10,10),0)
        assert restored==before and restored[0][at]>>24==0x41
        rectangle_cases+=1
    # Complete original edits at predecessor/exact/successor boundary words.
    # The original scales by32 directly; whole-map normalization is absent.
    raw_cases=[]
    grid_reads=[]
    grid_hook=uc.hook_add(UC_HOOK_CODE, lambda m,a,n,d:grid_reads.append(words(m.reg_read(UC_X86_REG_EDX))[0]),
                          begin=0x6f070c80,end=0x6f070c80)
    baseline_levels=read_levels()
    scalar_a,scalar_b,scalar_out=0x10008500,0x10008510,0x10008520
    def word(value):return struct.unpack('<I',struct.pack('<f',value))[0]
    def adjacent(value,step):
        raw=word(value)
        if not step:return raw
        if not raw&0x7fffffff:return 0x80000001 if step<0 else 1
        return raw-step if raw&0x80000000 else raw+step
    for size,(ox,oy),axis in itertools.product([23,96,160,288],origins,range(2)):
        write(fine+0x3c,size,size)
        floats(game+0x6c,ox,oy)
        terrain=[0xffffff]*(size*size)
        write(cells,*terrain)
        for edge,step in itertools.product(sorted({0,1,3,7,13,size//2,size-1,size}),[-1,0,1]):
            positions=[word(ox+528),word(oy+528)]
            positions[axis]=adjacent((ox,oy)[axis]+32*edge,step)
            inputs=positions+[word(ox),word(oy),word(32),word(32)]
            write(xptr,positions[0]);write(yptr,positions[1])
            observed.clear();grid_reads.clear()
            run(0x6f04d870,xptr,2,1,edx=yptr)
            assert len(grid_reads)==2
            fine_words=grid_reads[::-1]
            xy=[math.floor(struct.unpack('<f',struct.pack('<I',w))[0]) for w in fine_words]
            valid=all(0<=v<size for v in xy)
            pointer=cells+4*(xy[1]*size+xy[0]) if valid else 0
            assert observed==[pointer],(size,inputs,xy,observed,pointer)
            if valid:terrain[xy[1]*size+xy[0]]=0x02ffffff
            assert words(cells,size*size)==terrain
            outputs=fine_words[:]
            for k in range(2):
                write(scalar_a,fine_words[k])
                run(0x6f070c80,scalar_out,edx=scalar_a)
                run(0x6f070120,scalar_out)
                integer=uc.reg_read(UC_X86_REG_EAX)
                assert integer==xy[k]&0xffffffff
                outputs.append(integer)
            for k in range(2):
                write(scalar_a,fine_words[k]);write(scalar_b,word(32))
                run(0x6f06f9c0,scalar_out,scalar_b,edx=scalar_a)
                write(scalar_a,words(scalar_out)[0]);write(scalar_b,inputs[k+2])
                run(0x6f06fbb0,scalar_out,scalar_b,edx=scalar_a)
                outputs.append(words(scalar_out)[0])
            if engine:
                actual=(ctypes.c_uint32*6)();original=(ctypes.c_uint32*6)(*inputs)
                engine.pathing_world_grid(original,actual)
                assert list(actual)==outputs,(size,inputs,list(actual),outputs)
                assert list(original)==inputs
            raw_cases.append(dict(dimensions=[size,size],input=inputs,output=outputs,valid=valid))
            run(0x6f04d870,xptr,2,0,edx=yptr)
            if valid:terrain[xy[1]*size+xy[0]]=0xffffff
            assert words(cells,size*size)==terrain
            assert read_levels()==baseline_levels
    uc.hook_del(grid_hook)
    assert len(raw_cases)==576
    if args.coordinate_fixture:
        args.coordinate_fixture.write_text(json.dumps(dict(binary_sha256=digest,cases=raw_cases,
            scope='Complete04d870 fine inputs/cell admission and scalar inverse composition; origin/map producers and full Move caller excluded'),separators=(',',':'))+'\n')
    # Original point bridge's complete coordinate prefix, without replacing
    # any math callees. Stop before request allocation; full caller/route/task
    # composition is independently witnessed by public original captures.
    point_cases=[]
    clip_bounds=[(-7168,-3072,5120,5120),(-512.25,1024.5,4096.75,8192.5),(0,0,1024,2048)]
    for bounds,cell,axis,side,step in itertools.product(clip_bounds,[16,32,64],range(2),range(2),[-1,0,1]):
        edge=bounds[axis]+4*cell if side==0 else bounds[axis+2]-4*cell
        position=[word((bounds[0]+bounds[2])*.5),word((bounds[1]+bounds[3])*.5)]
        position[axis]=adjacent(edge,step)
        inputs=position+list(map(word,bounds))+[word(cell)]
        floats(game+0x6c,*bounds);floats(0x6fd3c7a8,cell);floats(0x6fd3c754,4)
        write(xptr,position[0]);write(yptr,position[1]);write(stack+8,xptr,yptr)
        uc.reg_write(UC_X86_REG_EBP,stack);uc.reg_write(UC_X86_REG_ESP,stack-0x40)
        uc.emu_start(0x6f05b986,0x6f05ba9e,count=200000)
        assert uc.reg_read(UC_X86_REG_EIP)==0x6f05ba9e
        outputs=[words(stack-0x14)[0],words(stack-0x10)[0],words(stack-0x1c)[0],words(stack-0x18)[0]]
        assert words(xptr)[0]==position[0] and words(yptr)[0]==position[1]
        if engine:
            actual=(ctypes.c_uint32*4)();engine.pathing_point_order_clip((ctypes.c_uint32*7)(*inputs),actual)
            assert list(actual)==outputs,(inputs,list(actual),outputs)
        point_cases.append(dict(input=inputs,output=outputs,axis=axis,side=side,step=step))
    assert len(point_cases)==108
    if args.point_order_fixture:
        args.point_order_fixture.write_text(json.dumps(dict(binary_sha256=digest,cases=point_cases,
            scope='Original05b986..05ba9e clipping/conversion prefix with actual math calls; controlled world bounds and cell size; request allocation/task/owner composition certified separately by public traces'),indent=2)+'\n')
    report = dict(point_order_clip_cases=len(point_cases),point_order_clip_engine_cases=len(point_cases) if engine else 0,world_grid_boundary_cases=len(raw_cases),world_grid_boundary_engine_cases=len(raw_cases) if engine else 0,
                  binary_sha256=digest, terrain_entry='6f04d870', cell_edit='6f054000',
                  hierarchy_entry='6f15d360', coordinate_cases=coordinate_cases,
                  composed_edit_rebuild_restore_cases=composed_cases,
                  clipped_clear_restore_cases=rectangle_cases,nofly_exclusion_restore_cases=3,
                  dimensions=dimensions, origins=origins, selected_cells=len(selected_cells),
                  scope='Complete original calls; exact dyadic world inputs; terrain high-bit set/clear, cell selection, no implicit hierarchy mutation; clipped base/parent updates and metadata preservation against independent model. Synthetic maps; constructors, actual origin producers, objects and non-dyadic rounding excluded.')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    if engine:report['engine_library_sha256']=hashlib.sha256(args.engine_library.read_bytes()).hexdigest()
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
