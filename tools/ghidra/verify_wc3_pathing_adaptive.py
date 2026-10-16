#!/usr/bin/env python3
"""Execute complete adaptive requests over synthetic classified base maps.

Original parent reduction, setup, cell promotion, node creation, expansion,
queue, search and reconstruction execute without code stubs. The base-map
classification and preallocated storage are supplied by the harness. Explicit
intervention switches can disable promotion or override one predicate result;
these changes are marked in reports and are not normal retail execution.
"""
import argparse
import collections
import ctypes
import hashlib
import itertools
import json
import random
import struct
from pathlib import Path
from verify_wc3_pathing_grid import reference


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--size-input', type=int, choices=(0, 1), default=0, help='adaptive stored size is 1 << input')
    parser.add_argument('--trace-graph', action='store_true', help='record relaxed edges, expanded nodes and final node fields')
    parser.add_argument('--east-boundary', type=int, nargs=2, metavar=('X', 'Y'), help='observe a selected east-side predicate result')
    parser.add_argument('--force-east-boundary', action='store_true', help='controlled intervention: turn that selected rejection into acceptance')
    parser.add_argument('--fixture', help='run one named fixture')
    parser.add_argument('--map-json', type=Path, help='custom map object with a blocked coordinate list; dimensions/endpoints stay fixed')
    parser.add_argument('--lane', type=int, choices=(0, 2, 4, 6), help='run just this lane')
    parser.add_argument('--disable-promotion', action='store_true', help='controlled intervention: mark all parents mixed')
    parser.add_argument('--engine-library', type=Path, help='compare production adaptive search and reconstructed route words')
    parser.add_argument('--budget', type=int, default=100000, help='charged-pop request budget (default100000)')
    parser.add_argument('--stamp-wrap',action='store_true',help='retained32-bit metadata across all lanes and both stored sizes')
    parser.add_argument('--wrap-fixture',type=Path,help='assert frozen original adaptive wrap nodes/routes')
    parser.add_argument('--terrain-producer',action='store_true',help='build the custom map through original fine-cell classification and padded hierarchy updates')
    parser.add_argument('--producer-fixture',type=Path,help='assert frozen terrain-produced classification and request state')
    args = parser.parse_args()
    if args.wrap_fixture and not args.stamp_wrap: parser.error('wrap fixture requires --stamp-wrap')
    if args.stamp_wrap and (args.disable_promotion or args.force_east_boundary): parser.error('wrap requires ordinary classification policy')
    if args.producer_fixture and not args.terrain_producer: parser.error('producer fixture requires --terrain-producer')
    if args.terrain_producer and (not args.map_json or args.disable_promotion or args.force_east_boundary or args.stamp_wrap):
        parser.error('terrain producer requires a custom map and ordinary traversal policy')
    if not 0 <= args.budget <= 0xffffffff:
        parser.error('--budget must fit an unsigned32-bit word')
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if args.force_east_boundary and args.east_boundary is None:
        parser.error('--force-east-boundary requires --east-boundary X Y')
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

    machine.mem_map(0x10000000, 0x400000)
    machine.mem_map(0x20000000, 0x10000)
    system, maps = 0x10000000, [0x10001000 + i * 0x100 for i in range(4)]
    data = [0x10010000 + i * 0x10000 for i in range(4)]
    nodes, heap, route, route_data = 0x10080000, 0x10100000, 0x10200000, 0x10201000
    source_ptr, target_ptr = 0x10000400, 0x10000410
    stack, stop, width = 0x20008000, 0x30000000, 32
    start, goal = (4, 4), (27, 27)
    source, target = (4.25, 4.75), (27.25, 27.75)
    constants = {'6fd3c740': -1.0, '6fd3c744': 0.0, '6fd3c748': 1.0, '6fd53a74': -128000.0078125}
    for address, value in constants.items():
        machine.mem_write(int(address, 16), struct.pack('<f', value))

    def write(address, *values):
        machine.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def read(address, count=1):
        return list(struct.unpack('<' + 'I' * count, machine.mem_read(address, count * 4)))

    def run(entry, self, *arguments):
        write(stack, stop, *arguments)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, self)
        machine.emu_start(entry, stop, count=20000000)
        if machine.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError('retail adaptive operation exceeded instruction budget')
        return machine.reg_read(UC_X86_REG_EAX)

    # Separate fine and adaptive owners: the base classifier follows the real
    # global fine-system pointer, while adaptive setup consumes all four maps.
    owner,fine_system,fine,game,cells,rectangle=0x10300000,0x10301000,0x10302000,0x10303000,0x10310000,0x10308000
    base_width=(width*2+16)//2+1 if args.terrain_producer else width
    sides=[base_width>>level for level in range(4)]
    producer_inventory=[]
    producer_classes=None

    def setup_producer():
        write(0x6fd53a48,owner);write(0x6fd3c82c,game)
        write(owner+0x24c,fine_system);write(fine_system+0x1c,fine)
        write(owner+0x23c,*maps);write(owner+0x250,system)
        write(fine+0x28,cells);write(fine+0x3c,width*2,width*2)
        write(fine+0x54,0,0,width*2,width*2)
        for level,(tilemap,storage,side) in enumerate(zip(maps,data,sides)):
            machine.mem_write(tilemap,bytes(0x100));machine.mem_write(storage,bytes(side*side*8))
            write(tilemap+0x28,storage);write(tilemap+0x3c,side,side)
            machine.mem_write(tilemap+0x64,struct.pack('<ff',2<<level,1/(2<<level)))
            write(system+0x1c+level*4,tilemap)

    if args.terrain_producer:
        # Enumerate every ordinary four-bit fine occupancy pattern. Retain one
        # witness for each attainable four-lane tuple, then execute the original
        # rectangle/base/parent producers for every witness. No classifier stub.
        masks=(6,0x80,0x40,4)
        witnesses={}
        for pattern in itertools.product(range(16),repeat=4):
            flags=[sum(mask for bit,mask in enumerate((2,4,0x40,0x80)) if v&(1<<bit)) for v in pattern]
            classes=tuple(0 if not any(v&mask for v in flags) else 1 if all(v&mask for v in flags) else 2 for mask in masks)
            witnesses.setdefault(classes,flags)
        setup_producer();write(cells,*([0xffffff]*(width*2)**2));write(rectangle,0,0,1,1)
        for classes,flags in sorted(witnesses.items()):
            for index,flag in enumerate(flags):write(cells+4*((index//2)*width*2+index%2),0xffffff|(flag<<24))
            run(0x6f15d360,owner,rectangle,0)
            word=read(data[0]+4)[0]
            actual=[(word>>(30-2*lane))&3 for lane in range(4)]
            assert actual==list(classes),(classes,flags,hex(word))
            producer_inventory.append(dict(classes=actual,fine_flags=flags,base_word=word))
        assert len(producer_inventory)==54

    graph_edges, graph_expansions, boundary_checks = [], [], []

    def trace_edge(uc, address, size, data):
        sp = uc.reg_read(UC_X86_REG_ESP)
        caller, destination, parent = read(sp, 3)
        graph_edges.append(dict(parent=parent, destination=destination, caller=hex(caller)))

    def trace_expand(uc, address, size, data):
        graph_expansions.append(read(uc.reg_read(UC_X86_REG_ESP) + 4)[0])

    def trace_boundary(uc, address, size, data):
        frame = uc.reg_read(UC_X86_REG_EBP)
        xy = [read(frame + 0x10)[0], uc.reg_read(UC_X86_REG_ESI)]
        if xy != args.east_boundary:
            return
        result = uc.reg_read(UC_X86_REG_EAX)
        row = dict(xy=xy, parent=read(frame + 8)[0], level=uc.reg_read(UC_X86_REG_EDI), caller_level=read(frame + 0xc)[0], original=result)
        if args.force_east_boundary and result == 0:
            uc.reg_write(UC_X86_REG_EAX, 1)
            row['forced'] = 1
        boundary_checks.append(row)

    if args.east_boundary is not None:
        machine.hook_add(UC_HOOK_CODE, trace_boundary, begin=0x6f163332, end=0x6f163332)

    if args.trace_graph:
        machine.hook_add(UC_HOOK_CODE, trace_edge, begin=0x6f164020, end=0x6f164020)
        for address in (0x6f1643d0, 0x6f1644d0):
            machine.hook_add(UC_HOOK_CODE, trace_expand, begin=address, end=address)

    fixtures = [('open', set()), ('solid_wall', {(16, y) for y in range(width)})]
    fixtures += [(f'gap_{gap}', {(16, y) for y in range(width) if not 14 <= y < 14 + gap}) for gap in range(1, 7)]
    for coordinate in (8, 9, 15, 17, 23, 24):
        fixtures.append((f'vertical_{coordinate}', {(coordinate, y) for y in range(width)}))
        fixtures.append((f'horizontal_{coordinate}', {(x, coordinate) for x in range(width)}))
    rng = random.Random(12717085)
    for index in range(64):
        density = (.08, .16, .24, .32)[index % 4]
        blocked = {(x, y) for y in range(width) for x in range(width) if rng.random() < density}
        for cx, cy in (start, goal):
            blocked.difference_update((x, y) for x in range(cx - 2, cx + 3) for y in range(cy - 2, cy + 3))
        fixtures.append((f'random_{index}', blocked))
    fixtures += [('blocked_start', {start}), ('blocked_goal', {goal}), ('blocked_both', {start, goal}),
                 ('same_coarse_cell', set()), ('same_fine_cell', set())]
    if args.map_json:
        if args.fixture:
            parser.error('--map-json and --fixture are mutually exclusive')
        custom = json.loads(args.map_json.read_text())
        blocked = {tuple(p) for p in custom['blocked']}
        if any(len(p) != 2 or any(not isinstance(v, int) or not 0 <= v < width for v in p) for p in blocked):
            parser.error('custom blocked cells must be integer pairs inside 32x32')
        fixtures = [('custom', blocked)]
    if args.fixture:
        fixtures = [(name, blocked) for name, blocked in fixtures if name == args.fixture]
        if not fixtures:
            parser.error('unknown fixture')
    records, failures, lane_baselines = [], [], {}
    for name, blocked in fixtures:
        for lane in ((args.lane,) if args.lane is not None else (0, 2, 4, 6)):
            request_goal = (5, 5) if name == 'same_coarse_cell' else start if name == 'same_fine_cell' else goal
            request_target = (request_goal[0] + .25, request_goal[1] + .75)
            if name == 'same_fine_cell':
                request_target = (4.875, 4.125)
            shortcut = name in ('blocked_start', 'blocked_both', 'same_fine_cell') or (name == 'same_coarse_cell' and not args.disable_promotion)
            other_lanes = 0x55000000 & ~(0xc0000000 >> lane)
            machine.mem_write(system, bytes(0x400))
            for level, (tilemap, storage) in enumerate(zip(maps, data)):
                side = sides[level]
                machine.mem_write(tilemap, bytes(0x100))
                machine.mem_write(storage, bytes(side * side * 8))
                write(tilemap + 0x28, storage)
                write(tilemap + 0x3c, side, side)
                write(system + 0x1c + level * 4, tilemap)
                for y in range(side):
                    for x in range(side):
                        cell = storage + (y * side + x) * 8
                        write(cell + 4, other_lanes)
                        if level == 0:
                            write(cell + 4, other_lanes | ((0x40000000 >> lane) if (x, y) in blocked else 0))
                        else:
                            run(0x6f15d1c0, 0, cell, maps[level - 1], lane, 2 * x, 2 * y)
                            if args.disable_promotion:
                                write(cell + 4, other_lanes | (0x80000000 >> lane))
            if args.terrain_producer:
                setup_producer()
                terrain=[0xffffff|((0xc6<<24) if (x//2,y//2) in blocked else 0) for y in range(width*2) for x in range(width*2)]
                write(cells,*([0xffffff]*(width*2)**2))
                machine.mem_write(game+0x6c,struct.pack('<ff',0,0))
                setter_calls=0
                for y in range(width*2):
                    for x in range(width*2):
                        if (x//2,y//2) not in blocked:continue
                        machine.mem_write(source_ptr,struct.pack('<ff',(x+.25)*32,(y+.75)*32))
                        for mask in (2,4,0x40,0x80):
                            machine.reg_write(UC_X86_REG_EDX,source_ptr+4)
                            run(0x6f04d870,source_ptr,mask,1)
                            setter_calls+=1
                assert read(cells,(width*2)**2)==terrain
                run(0x6f15d360,owner,0,0)
                classes=[read(storage+8*i+4)[0]>>24 for storage,side in zip(data,sides) for i in range(side*side)]
                if producer_classes is None:producer_classes=classes
                assert classes==producer_classes
            write(system + 0x5c, nodes)
            write(system + 0x68, 4096, 0)
            write(system + 0x7c, heap)
            write(system + 0x88, 65536, 0)
            write(route + 0xc, route_data)
            write(route + 0x18, 4096, 0)
            machine.mem_write(source_ptr, struct.pack('<ff', *source))
            machine.mem_write(target_ptr, struct.pack('<ff', *request_target))
            boundary_checks.clear()
            graph_edges.clear()
            graph_expansions.clear()
            result = run(0x6f162cb0, system, lane, route, source_ptr, target_ptr, args.budget, args.size_input, 0)
            # Size 2's base footprint is anchored toward positive X/Y. The
            # fine graph helper represents its 2x2 square with the opposite
            # node convention, so translate reference endpoints by (1,1).
            shift = args.size_input
            ref_start = tuple(v + shift for v in start)
            ref_goal = tuple(v + shift for v in request_goal)
            expected, ref_reachable = reference(blocked, width, width, ref_start, ref_goal, shift)
            reachable = {(x - shift, y - shift): cost for (x, y), cost in ref_reachable.items()}
            count, node_count = read(route + 0x1c)[0], read(system + 0x6c)[0]
            if not 1 <= count <= 4096 or node_count > 4096:
                raise RuntimeError('invalid adaptive result dimensions')
            points = [struct.unpack('<ff', machine.mem_read(route_data + i * 8, 8)) for i in range(count)]
            if engine:
                params=[base_width,base_width,args.size_input,args.budget]+list(struct.unpack('<4I',struct.pack('<4f',*source,*request_target)))
                classes=[(read(storage+(y*side+x)*8+4)[0]>>(30-lane))&3
                    for storage,side in zip(data,sides)
                    for y in range(side) for x in range(side)]
                expected_words=[result,read(system+0x9c)[0],node_count,count]
                expected_words+=list(struct.unpack('<'+'I'*(count*2),machine.mem_read(route_data,count*8)))
                output=(ctypes.c_uint32*32772)()
                engine.pathing_adaptive_route((ctypes.c_uint32*8)(*params),(ctypes.c_uint8*len(classes))(*classes),output)
                if list(output[:len(expected_words)])!=expected_words:
                    raise RuntimeError(('production adaptive route differs',name,lane,expected_words,list(output[:len(expected_words)])))
            levels = collections.Counter(machine.mem_read(nodes + i * 36 + 0x22, 1)[0] for i in range(node_count))
            row = dict(fixture=name, lane=lane, setup_shortcut=shortcut, result=result, reference_reached=expected is not None,
                       pops=read(system + 0x9c)[0], nodes=node_count, levels=dict(levels), points=points,
                       adjusted_target=struct.unpack('<ff', machine.mem_read(system + 0xac, 8)))
            if args.terrain_producer:
                row['route_words']=read(route_data,count*2)
                row['node_state']=[[read(nodes+36*i)[0],read(nodes+36*i+4)[0],read(nodes+36*i+0x14)[0],read(nodes+36*i+0x18)[0],read(nodes+36*i+8)[0],read(nodes+36*i+0x1c)[0],
                    0 if read(nodes+36*i+0xc)[0]==0xffffffff else 1 if read(nodes+36*i+0xc)[0]==0xfffffffe else 2,machine.mem_read(nodes+36*i+0x22,1)[0]] for i in range(node_count)]
                if engine:
                    state=(ctypes.c_uint32*(1+node_count*8))()
                    engine.pathing_adaptive_node_state(state)
                    assert list(state)==[node_count]+[v for n in row['node_state'] for v in n],'terrain-produced engine node state differs'
            if result == 0:
                nearest_index, start_index = read(system + 0xd0)[0], read(system + 0xc4)[0]
                nearest_xy = read(nodes + nearest_index * 36, 2)
                wanted = source if nearest_index == start_index else tuple(v + .5 for v in nearest_xy)
                row['nearest_node_xy'] = nearest_xy
                row['base_component_nearest_distance2'] = min((x - request_goal[0]) ** 2 + (y - request_goal[1]) ** 2 for x, y in reachable)
                row['adaptive_nearest_distance2'] = read(system + 0xcc)[0]
                if points[0] != wanted or (nearest_index != start_index and tuple(row['adjusted_target']) != wanted):
                    raise RuntimeError('adaptive partial destination differs from selected node centre')
            if args.east_boundary is not None:
                row['east_boundary_checks'] = list(boundary_checks)
                row['east_ordinary_occupancy'] = run(0x6f163370, system, *args.east_boundary)
            if args.trace_graph:
                row['graph'] = dict(edges=list(graph_edges), expanded=list(graph_expansions),
                    nodes=[dict(index=i, xy=read(nodes + i * 36, 2), generation=read(nodes + i * 36 + 8)[0],
                                state=hex(read(nodes + i * 36 + 0xc)[0]), g=read(nodes + i * 36 + 0x14)[0],
                                parent=read(nodes + i * 36 + 0x1c)[0],
                                level=machine.mem_read(nodes + i * 36 + 0x22, 1)[0]) for i in range(node_count)])
            records.append(row)
            if shortcut:
                if result != 1 or points != [request_target] or row['pops'] != 0:
                    failures.append(row)
                if name in ('blocked_start', 'blocked_both') and read(system + 0xc4, 2) != [0xffffffff, 0xffffffff]:
                    raise RuntimeError('blocked-start shortcut did not preserve invalid start/goal indices')
            elif result != int(expected is not None) or points[-1] != source or (result and points[0] != request_target):
                failures.append(row)
            comparable = {k: v for k, v in row.items() if k != 'lane'}
            if name not in lane_baselines:
                lane_baselines[name] = comparable
            elif comparable != lane_baselines[name]:
                raise RuntimeError('same graph produced different results after traversal-lane shift')
            if len(records) % 64 == 0:
                print(f'{len(records)} adaptive requests; {len(failures)} differences', flush=True)
    # Exact size-2 east-boundary predicate: candidate cell itself was already
    # admitted by cell lookup. Test its seven additional cells and map bounds.
    # These independent controls retain their historical32x32 geometry.
    if args.terrain_producer:
        for tilemap,level in zip(maps,range(4)):write(tilemap+0x3c,width>>level,width>>level)
    predicate_cases = 0
    offsets = ((1, 0), (0, 1), (1, 1), (0, -1), (1, -1), (-2, 1), (-1, 1))
    for lane in (0, 2, 4, 6):
        write(system + 0xd4, lane)
        other_lanes = 0x55000000 & ~(0xc0000000 >> lane)
        clear_cells = struct.pack('<II', 0, other_lanes) * (width * width)
        for x, y in ((0, 0), (1, 1), (2, 2), (18, 13), (31, 31), (30, 30), (0, 15), (31, 15), (15, 0), (15, 31)):
            for bits in range(128):
                machine.mem_write(data[0], clear_cells)
                allowed = []
                for bit, (dx, dy) in enumerate(offsets):
                    cx, cy = x + dx, y + dy
                    inside = 0 <= cx < width and 0 <= cy < width
                    allowed.append(inside and not bits & (1 << bit))
                    if inside and bits & (1 << bit):
                        write(data[0] + (cy * width + cx) * 8 + 4, other_lanes | (0x40000000 >> lane))
                r = all(allowed[:3])
                a, b, c, d = allowed[3:]
                expected = r and ((a and b) or (c and d) or (d and a))
                ordinary = run(0x6f163370, system, x, y)
                boundary = run(0x6f1635b0, system, x, y)
                if ordinary != int(r) or boundary != int(expected):
                    raise RuntimeError('size-2 east-boundary predicate mismatch')
                predicate_cases += 1

    # Isolate original path-owned size/lane selection instructions. Their
    # scheduling/exclusion callers are not executed by these slice checks.
    size_samples = []
    bits = {value + delta for value in (0x3f000000, 0x3f800000, 0x3fc00000) for delta in range(-2, 3)}
    bits.update(struct.unpack('<I', struct.pack('<f', v))[0] for v in (.03125, .96875, 2.0, 10.0, 100.0))
    machine.reg_write(UC_X86_REG_EBX, system)
    machine.reg_write(UC_X86_REG_EBP, stack)
    for encoded in sorted(bits):
        scalar = struct.unpack('<f', struct.pack('<I', encoded))[0]
        write(system + 0xb4, encoded)
        machine.emu_start(0x6f166cd3, 0x6f166d23, count=100)
        actual = read(stack - 8)[0]
        if machine.reg_read(UC_X86_REG_EIP) != 0x6f166d23 or actual != int(scalar >= 1):
            raise RuntimeError('path-owned adaptive size selection mismatch')
        size_samples.append(dict(scalar=scalar, input_class=actual, stored_size=1 << actual))
    lane_cases = 0
    for top in range(4):
        for low in (0, 1, 0x15555555, 0x3fffffff):
            write(system + 0x88, (top << 30) | low)
            machine.reg_write(UC_X86_REG_ESP, stack)
            machine.emu_start(0x6f166d6c, 0x6f166d7b, count=100)
            if machine.reg_read(UC_X86_REG_EIP) != 0x6f166d7b or read(stack - 4)[0] != top * 2:
                raise RuntimeError('path-owned traversal lane selection mismatch')
            lane_cases += 1
    report = dict(binary_sha256=digest, cases=len(records), size_input=args.size_input, stored_size=1 << args.size_input, promotion_disabled=args.disable_promotion, east_boundary=args.east_boundary, forced_east_boundary=args.force_east_boundary, differences=failures, setup_shortcuts=sum(r['setup_shortcut'] for r in records),
                  runtime_constants=constants, seed=12717085, dimensions=[width, width],
                  fixture_cells={name: sorted(blocked) for name, blocked in fixtures},
                  size_selection=size_samples, lane_selection_cases=lane_cases, east_boundary_predicate_cases=predicate_cases,
                  scope='complete original adaptive requests, selected size, all lanes with other lanes blocked, no warp; original parent reducer; supplied base classifications; compare ordinary reachability against point/positive-anchored-2x2 base graph; separately verify setup shortcuts',
                  searches=records)
    report['budget']=args.budget
    if args.terrain_producer:report['scope']='complete original terrain setters/classification and adaptive requests over supplied empty fine storage/padded headers, no warp; original selected size/lane, node state and fractional reconstruction; preserve conventional-reference differences'
    if args.terrain_producer:
        producer=dict(binary_sha256=digest,scope='Original04d870/054000 terrain setters,15d360/base/fine-query/parent producers and complete162cb0 requests; supplied64x64 empty fine storage and padded41/20/10/5 map headers, no objects/special edges. Four-lane classification inventory has54 original witnesses; ordinary class3 and27 ground/flight-inconsistent tuples are rejected. Full world loading and mover fallback remain separate.',
            dimensions=[64,64],hierarchy_sides=sides,fine_flags=[(v>>24)&255 for v in terrain],class_bytes=producer_classes,
            terrain_setter_calls=setter_calls,
            inventory=producer_inventory,rejected_tuples=[list(c) for c in itertools.product(range(3),repeat=4) if list(c) not in [r['classes'] for r in producer_inventory]],
            size_input=args.size_input,budget=args.budget,searches=records)
        producer=json.loads(json.dumps(producer))
        if args.producer_fixture:
            assert producer==json.loads(args.producer_fixture.read_text()),'terrain-produced frozen state differs'
        report['terrain_producer']=producer
        report['producer_classification_cases']=len(producer_inventory)
        report['producer_classification_rejected']=len(producer['rejected_tuples'])
        report['producer_hierarchy_cells']=len(producer_classes)
    if engine:
        report['engine_exact_cases']=len(records)
        report['engine_library_sha256']=hashlib.sha256(args.engine_library.read_bytes()).hexdigest()
    if args.stamp_wrap:
        border={(x,y) for y in range(width) for x in range(width) if x in (0,31) or y in (0,31)}
        air=border|{(16,y) for y in range(32) if not 19<=y<25}
        ground=air|{(x,12) for x in range(32) if not 7<=x<13}
        floating=border|{(12,y) for y in range(32) if not 9<=y<15}|{(20,y) for y in range(32) if not 19<=y<25}
        amphibious=border|{(16,y) for y in range(32)}
        blocked_lanes=[ground,amphibious,floating,air]
        # A supplied ordinary four-lane map, reduced by the original producer.
        machine.mem_write(system,bytes(0x400))
        for level,(tilemap,storage) in enumerate(zip(maps,data)):
            side=width>>level
            machine.mem_write(tilemap,bytes(0x100));machine.mem_write(storage,bytes(side*side*8))
            write(tilemap+0x28,storage);write(tilemap+0x3c,side,side);write(system+0x1c+level*4,tilemap)
            for y in range(side):
                for x in range(side):
                    cell=storage+8*(y*side+x)
                    if level==0:
                        write(cell+4,sum((0x40000000>>(2*lane)) for lane,blocked in enumerate(blocked_lanes) if (x,y) in blocked))
                    else:
                        for lane in (0,2,4,6):run(0x6f15d1c0,0,cell,maps[level-1],lane,x*2,y*2)
        write(system+0x5c,nodes);write(system+0x68,4096,0)
        write(system+0x7c,heap);write(system+0x88,65536,0)
        write(route+0xc,route_data);write(route+0x18,4096,0)
        machine.mem_write(source_ptr,struct.pack('<ff',*source));machine.mem_write(target_ptr,struct.pack('<ff',*target))
        # Warm all searchable cell identities using actual lazy lookup/creation.
        # This is an explicitly supplied pre-wrap map history; untouched zero
        # stamps at a forced DWORD zero are a separate diagnostic below.
        warm_lookups=0
        for lane,blocked in zip((0,2,4,6),blocked_lanes):
            run(0x6f164230,system,0);write(system+0x2c,100+lane);write(system+0xd4,lane);write(system+0x90,1)
            for y in range(width):
                for x in range(width):
                    if (x,y) not in blocked:
                        assert run(0x6f1625f0,system,0,x,y)!=0xffffffff
                        warm_lookups+=1
        def adaptive_wrap_request(lane,size):
            result=run(0x6f162cb0,system,lane,route,source_ptr,target_ptr,400,size,0)
            count=read(route+0x1c)[0];node_count=read(system+0x6c)[0]
            assert 0<count<=4096 and node_count<=4096
            assert read(system+0xd4)[0]==lane and read(system+0x90)[0]==1<<size
            assert read(system+0xd8)[0]==0 and read(system+0xa0)[0]==0
            assert all(read(nodes+36*i+32)[0]&255==0 for i in range(node_count))
            return dict(lane=lane,size_input=size,stamp=read(system+0x2c)[0],result=result,
                work=read(system+0x9c)[0],nodes=node_count,route_words=read(route_data,count*2),
                node_state=[[v[0],v[1],v[5],v[6],v[2],v[7],0 if v[3]==0xffffffff else 1 if v[3]==0xfffffffe else 2,(v[8]>>16)&255]
                    for v in (read(nodes+36*i,9) for i in range(node_count))])
        write(system+0x2c,0xfffffffd)
        retained=[adaptive_wrap_request(lane,size) for size in (0,1) for lane in (0,2,4,6)]
        assert [r['stamp'] for r in retained]==[0xfffffffe,0xffffffff,0,1,2,3,4,5]
        clean_maps=[bytes(machine.mem_read(storage,(width>>level)**2*8)) for level,storage in enumerate(data)]
        for index,row in enumerate(retained):
            for level,(storage,raw) in enumerate(zip(data,clean_maps)):
                cleaned=bytearray(raw)
                for offset in range(0,len(raw),8):cleaned[offset:offset+4]=bytes(4);cleaned[offset+4:offset+6]=bytes(2)
                machine.mem_write(storage,bytes(cleaned))
            write(system+0x2c,200+index)
            clean=adaptive_wrap_request(row['lane'],row['size_input'])
            assert {k:v for k,v in row.items() if k!='stamp'}=={k:v for k,v in clean.items() if k!='stamp'},index
            if engine:
                classes=[(struct.unpack_from('<I',raw,8*i+4)[0]>>(30-row['lane']))&3
                    for raw in clean_maps for i in range(len(raw)//8)]
                query=(ctypes.c_uint32*8)(32,32,row['size_input'],400,*struct.unpack('<4I',struct.pack('<4f',*source,*target)))
                out=(ctypes.c_uint32*32772)();engine.pathing_adaptive_route(query,(ctypes.c_uint8*len(classes))(*classes),out)
                assert list(out[:4])==[row['result'],row['work'],row['nodes'],len(row['route_words'])//2]
                assert list(out[4:4+len(row['route_words'])])==row['route_words']
                engine.pathing_adaptive_node_state.argtypes=[ctypes.POINTER(ctypes.c_uint32)]
                state=(ctypes.c_uint32*(1+8*16386))();engine.pathing_adaptive_node_state(state)
                assert [list(state[1+8*i:9+8*i]) for i in range(state[0])]==row['node_state']

        report.update(stamp_wrap_cases=retained,stamp_wrap_warmed_lookups=warm_lookups,
            stamp_wrap_budget=400,stamp_wrap_clean_requests=8,stamp_wrap_engine_requests=8 if engine else 0,
            stamp_wrap_input_rows=[[sum(1<<x for x in range(32) if (x,y) in blocked) for y in range(32)] for blocked in blocked_lanes])
        if args.wrap_fixture:
            frozen=json.loads(args.wrap_fixture.read_text())
            assert retained==frozen['cases'] and report['stamp_wrap_input_rows']==frozen['input_rows']
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(records)} adaptive requests; {len(failures)} reference differences')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
