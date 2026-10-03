#!/usr/bin/env python3
"""Run original fine-grid searches against independent Dijkstra graph costs.

No code stubs: retail node creation, per-cell stamps, occupancy, expansion,
heap, relaxation and loop execute. Search/map storage is initialized directly;
this does not validate constructors, request admission or route smoothing.
"""
import argparse
import ctypes
import hashlib
import itertools
import heapq
import json
import random
import struct
from pathlib import Path
from verify_wc3_pathing_footprints import CLASSES, DIRECTIONS, perimeter


class ObjectInput(ctypes.Structure):
    _fields_ = [('cells', ctypes.POINTER(ctypes.c_uint8)), ('objects', ctypes.POINTER(ctypes.c_uint32))]


def footprint_graph(blocked, dimensions, size_class):
    """Recovered legal graph, independent of the C search's queue/heuristic."""
    width, height = dimensions
    _, offset, ring_width, masks = CLASSES[size_class]
    edges = []
    for y in range(height):
        for x in range(width):
            bits = sum(1 << i for i, p in enumerate(perimeter(x, y, offset, ring_width))
                       if not (0 <= p[0] < width and 0 <= p[1] < height) or p in blocked)
            edges.append(sum(1 << d for d, ((dx, dy), mask) in enumerate(zip(DIRECTIONS, masks))
                             if not bits & mask and 0 <= x + dx < width and 0 <= y + dy < height))
    return bytes(edges)


def reference(blocked, width, height, start, goal, size_class):
    _, offset, ring_width, masks = CLASSES[size_class]
    costs, pending = {start: 0}, [(0, start)]
    while pending:
        cost, point = heapq.heappop(pending)
        if cost != costs[point]:
            continue
        if point == goal:
            return cost, costs
        x, y = point
        bits = sum(1 << i for i, p in enumerate(perimeter(x, y, offset, ring_width))
                   if not (0 <= p[0] < width and 0 <= p[1] < height) or p in blocked)
        for (dx, dy), mask in zip(DIRECTIONS, masks):
            other = x + dx, y + dy
            if bits & mask or not (0 <= other[0] < width and 0 <= other[1] < height):
                continue
            value = cost + (21 if dx and dy else 15)
            if value < costs.get(other, 1 << 60):
                costs[other] = value
                heapq.heappush(pending, (value, other))
    return None, costs


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, help='freeze original cell routes for asset-free engine comparisons')
    parser.add_argument('--engine-library', type=Path, help='compare production C cell routes, work and node creation')
    parser.add_argument('--partials', action='store_true', help='freeze nearest-chain results at request budget boundaries')
    parser.add_argument('--movement-profiles', action='store_true', help='include published float/amphibious masks in object searches')
    parser.add_argument('--objects', action='store_true', help='full mixed object chains for ground/flight query masks')
    parser.add_argument('--corridors', action='store_true', help='cardinal corridors of width 0..5 across four classes')
    parser.add_argument('--passages', action='store_true', help='four-lane cardinal/corner/edge passage matrix with exact fractional requests')
    parser.add_argument('--queue-composition',action='store_true',help='one natural full request with ties/reopening/stale entries')
    parser.add_argument('--stamp-wrap',action='store_true',help='sequential request stamp wrap with retained metadata and lane/class changes')
    parser.add_argument('--wrap-fixture',type=Path,help='assert frozen original wrap/control records')
    args = parser.parse_args()
    if args.stamp_wrap and not args.queue_composition: parser.error('stamp wrap requires queue composition fixture')
    if args.passages and (args.objects or args.partials or args.corridors): parser.error('passages is a separate matrix')
    if args.queue_composition and (args.passages or args.objects or args.partials or args.corridors): parser.error('queue composition is a separate fixture')
    if args.movement_profiles and not args.objects: parser.error('--movement-profiles requires --objects')
    if args.corridors and (args.objects or args.partials): parser.error('corridors is a separate matrix')
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        engine.pathing_fine_grid.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint8),
                                            ctypes.POINTER(ctypes.c_int32)]
        engine.pathing_fine_objects.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ObjectInput),
                                               ctypes.POINTER(ctypes.c_int32)]
        engine.pathing_fine_partial.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ObjectInput),
                                               ctypes.POINTER(ctypes.c_int32)]
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

    machine.mem_map(0,0x1000)  # Original footprint consumer SEH chain.
    machine.mem_map(0x10000000, 0x200000)
    machine.mem_map(0x20000000, 0x10000)
    system, tilemap, cells, bitmap = 0x10000000, 0x10000200, 0x10001000, 0x10002000
    nodes, links, heap = 0x10010000, 0x10020000, 0x10030000
    stack, stop = 0x20008000, 0x30000000
    route, route_data = 0x10100000, 0x10101000
    source_ptr, target_ptr, mask_ptr, radius_ptr = 0x10000400, 0x10000410, 0x10000420, 0x10000430
    for address, value in ((0x6fd3c740, -1.0), (0x6fd3c744, 0.0), (0x6fd3c748, 1.0)):
        machine.mem_write(address, struct.pack('<f', value))
    width = height = 24
    start, goal = (4, 4), (19, 19)
    if args.partials: goal = (19, 4)
    if args.corridors:
        width = height = 16
        start, goal = (8, 4), (8, 11)

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
            raise RuntimeError('retail grid search exceeded instruction budget')
        return machine.reg_read(UC_X86_REG_EAX)

    rng = random.Random(12717085)
    fixtures = [('open', set()), ('solid_wall', {(12, y) for y in range(height)})]
    fixtures += [(f'gap_{gap}', {(12, y) for y in range(height) if not 10 <= y < 10 + gap}) for gap in range(1, 7)]
    for index in range(64):
        density = (.08, .16, .24, .32)[index % 4]
        blocked = {(x, y) for y in range(height) for x in range(width) if rng.random() < density}
        for cx, cy in (start, goal):
            blocked.difference_update((x, y) for x in range(cx - 3, cx + 4) for y in range(cy - 3, cy + 4))
        fixtures.append((f'random_{index}', blocked))
    if args.corridors:
        fixtures = [(f'corridor_{span}', {(x, y) for y in range(height) for x in range(width)
                                         if not 8 - span // 2 <= x < 8 - span // 2 + span}) for span in range(6)]
    profiles = {}
    queue_input = None
    if args.queue_composition:
        queue_input=json.loads(Path(__file__).with_name('fixtures').joinpath('retail-fine-queue-1.27.json').read_text())['input']
        width=height=queue_input['dim']; start=tuple(queue_input['start']); goal=tuple(queue_input['goal'])
        cells=0x10060000
        blocked={(x,y) for y in range(height) for x in range(width) if bytes.fromhex(queue_input['cells'])[y*width+x]}
        fixtures=[('queue_composition',blocked)]
        profiles['queue_composition']=dict(mask=0x02000002,objects=[])
    pop_records=[]; trace_active=False
    def observe_pop(m,address,size,data):
        if not trace_active or m.reg_read(UC_X86_REG_ECX)!=system+0x44: return
        key,index,generation=read(heap+12,3)
        n=read(nodes+index*36,9)
        state=0 if n[3]==0xffffffff else 1 if n[3]==0xfffffffe else 2
        pop_records.append([key,index,generation,n[2],read(system+0x6c)[0],n[5],n[6],n[7],state,read(system+0x60)[0]-1])
    if args.queue_composition:
        machine.hook_add(UC_HOOK_CODE,observe_pop,begin=0x6f148240,end=0x6f148240)

    if args.passages:
        width = height = 16
        shapes = []
        all_cells = {(x,y) for y in range(16) for x in range(16)}
        for span in range(6):
            vertical = {(x,y) for x,y in all_cells if 8-span//2 <= x < 8-span//2+span}
            shapes.append((f'vertical_{span}',all_cells-vertical,(8,4),(8,11),0xc6))
            shapes.append((f'horizontal_{span}',all_cells-{(y,x) for x,y in vertical},(4,8),(11,8),0xc6))
            clear = {(x,y) for x,y in all_cells if
                (4-span//2 <= x < 4-span//2+span and 4<=y<=11) or
                (11-span//2 <= y < 11-span//2+span and 4<=x<=11) or
                (2<=x<=6 and 2<=y<=6) or (9<=x<=13 and 9<=y<=13)}
            def rotate(point,turn):
                x,y=point
                for _ in range(turn): x,y=15-y,x
                return x,y
            for turn in range(4):
                shapes.append((f'corner_{span}_{turn}',{rotate(p,turn) for p in all_cells-clear},
                    rotate((4,4),turn),rotate((11,11),turn),0xc6))
            # Two static rectangles touch at span0 and open progressively.
            blocked={(x,y) for x,y in all_cells if 6<=x<10 and not 8-span//2<=y<8-span//2+span}
            shapes.append((f'touching_{span}',blocked,(3,8),(12,8),0xc6))
        for side,(start,goal) in enumerate((((0,4),(0,11)),((15,4),(15,11)),((4,0),(11,0)),((4,15),(11,15)))):
            shapes.append((f'edge_{side}',set(),start,goal,0xc6))
        for flag in (2,4,0x40,0x80):
            shapes.append((f'lane_wall_{flag}',{(8,y) for y in range(16)},(4,8),(11,8),flag))
        fixtures=[]
        for (label,blocked,a,b,flags),mask,(fx,fy) in itertools.product(shapes,
                (0x02000002,0x04000004,0x40000040,0x80000080),
                ((.125,.875),(.5,.5),(.875,.125),(.1,.9))):
            name=f'{label}_{mask:08x}_{fx}_{fy}'
            fixtures.append((name,blocked))
            profiles[name]=dict(mask=mask,objects=[],terrain_flags=flags,start=a,goal=b,
                source=[a[0]+fx,a[1]+fy],target=[b[0]+fx,b[1]+fy],shape=label)

    if args.objects:
        fixtures = []
        rect = [10, 10, 14, 14]
        def obj(flags=0, mask=0x010000ca, linked=True, bounds=rect):
            return dict(bounds=bounds, flags=flags, mask=mask, linked=linked)
        variants = {
            'idle': [obj()], 'moving': [obj(0x20000000)], 'transient': [obj(0x40000000)],
            'suppressed': [obj(1)], 'disabled': [obj(0x80000000)], 'unlinked': [obj(linked=False)],
            'inactive': [obj(mask=0xca)], 'flight_category': [obj(mask=0x01000000)],
            'moving_then_idle': [obj(0x20000000), obj()],
            'idle_then_moving': [obj(), obj(0x20000000)],
            'overlap_categories': [obj(mask=0x01000000), obj(bounds=[12, 9, 16, 13])],
            'idle_wall': [obj(bounds=[12, 0, 14, 24])],
        }
        if args.partials:
            variants['idle_wall'] = [obj(bounds=[11, 0, 13, 24])]
            variants['idle_goal'] = [obj(bounds=[18, 3, 20, 5])]
        for terrain, static in [('open', set()), ('gap4', {(12,y) for y in range(height) if not 10 <= y < 14})]:
            for label, objects in variants.items():
                for mask in ((0x02000002, 0x04000004, 0x40000040, 0x80000080) if args.movement_profiles else (0x02000002, 0x04000004)):
                    name = f'{terrain}_{label}_{mask:08x}'
                    fixtures.append((name, static))
                    profiles[name] = dict(mask=mask, objects=objects)
    records, edge_cases, engine_cases, budget_cases, passage_records = [], [], [], [], []
    for name, blocked in fixtures:
        profile = profiles.get(name, dict(mask=0x02000000, objects=[]))
        query_mask, objects = profile['mask'], profile['objects']
        terrain_flags=profile.get('terrain_flags',query_mask>>24)
        if args.passages: start,goal=profile['start'],profile['goal']
        effective = set(blocked) if terrain_flags & (query_mask>>24) else set()
        for object in objects:
            flags = object['flags']
            if (object['linked'] and object['mask'] & 0x01000000 and not flags & 0x8fffffff and
                    not flags & 0x60000000 and object['mask'] & query_mask & 0xffffff):
                x0,y0,x1,y1 = object['bounds']
                effective.update((x,y) for y in range(y0,y1) for x in range(x0,x1))
        for size_class in (range(1) if args.queue_composition else range(4)):
            machine.mem_write(system, bytes(0x400))
            machine.mem_write(bitmap, bytes(1024))
            machine.mem_write(cells, b''.join(struct.pack('<I', (terrain_flags << 24) | 0xffffff if (x, y) in blocked else 0x00ffffff)
                                            for y in range(height) for x in range(width)))
            write(system + 0x1c, tilemap, 1)
            write(system + 0x30, nodes)
            write(system + 0x3c, 1024, 0)
            write(system + 0x50, heap)
            write(system + 0x5c, 32768, 1, -3, 100000, 0)
            write(system + 0x80, *start, *goal)
            write(system + 0x98, sum((a - b) ** 2 for a, b in zip(start, goal)), 0)
            write(system + 0xa0, size_class, query_mask)
            write(tilemap + 0x28, cells)
            write(tilemap + 0x3c, width, height)
            write(tilemap + 0x78, links)
            write(tilemap + 0x84, 8192, 0)
            write(tilemap + 0x98, bitmap)
            write(tilemap + 0xac, 0xffffff)
            link_count = 0
            for i, object in enumerate(objects):
                address = 0x10118000 + i * 0x80
                machine.mem_write(address, bytes(0x80))
                write(address + 0x34, object['mask'], 0 if object['linked'] else -1)
                write(address + 0x40, object['flags'])
                x0,y0,x1,y1 = object['bounds']
                for y in range(y0,y1):
                    for x in range(x0,x1):
                        cell = cells + 4 * (y * width + x)
                        old = read(cell)[0]
                        write(links + 8 * link_count, 0x01000000 | (old & 0xffffff), address)
                        write(cell, (old & 0xff000000) | link_count)
                        link_count += 1
            write(tilemap + 0x88, link_count)
            start_index = run(0x6f147af0, system, *start)
            goal_index = run(0x6f147af0, system, *goal)
            write(system + 0x90, start_index, goal_index)
            trace_active=args.queue_composition
            pop_records.clear()
            result = run(0x6f14aa10, system)
            trace_active=False
            core_pops=pop_records[:]
            expected, reachable = reference(effective, width, height, start, goal, size_class)
            actual = None if result == 0xffffffff else read(nodes + result * 36 + 0x14)[0]
            if (result != 0xffffffff and result != goal_index) or (actual != expected and not args.queue_composition):
                raise RuntimeError(f'grid mismatch {name} class={size_class} result={result} cost={actual} wanted={expected}')
            chain, points = [], []
            if result != 0xffffffff:
                index = result
                while index != 0xffffffff:
                    if index in chain:
                        raise RuntimeError('cyclic retail parent chain')
                    chain.append(index)
                    index = read(nodes + index * 36 + 0x1c)[0]
                if chain[-1] != start_index:
                    raise RuntimeError('retail parent chain does not reach start')
                points = [tuple(read(nodes + index * 36, 2)) for index in reversed(chain)]
                reconstructed_cost = 0
                _, offset, ring_width, masks = CLASSES[size_class]
                for a, b in zip(points, points[1:]):
                    delta = b[0] - a[0], b[1] - a[1]
                    mask = masks[DIRECTIONS.index(delta)]
                    bits = sum(1 << i for i, p in enumerate(perimeter(*a, offset, ring_width))
                               if not (0 <= p[0] < width and 0 <= p[1] < height) or p in effective)
                    if bits & mask:
                        raise RuntimeError('retail chain contains forbidden footprint edge')
                    reconstructed_cost += 21 if all(delta) else 15
                if reconstructed_cost != actual:
                    raise RuntimeError('retail parent chain cost differs from goal g')
            else:
                nearest = min(sum((a - b) ** 2 for a, b in zip(p, goal)) for p in reachable)
                if read(system + 0x98)[0] != nearest:
                    raise RuntimeError('failed search nearest-distance mismatch')
            nearest_at = read(system + 0x9c)[0]
            partial = []
            if result == 0xffffffff:
                index = nearest_at
                while index != 0xffffffff:
                    partial.append(list(read(nodes + index * 36, 2)))
                    index = read(nodes + index * 36 + 0x1c)[0]
                    if len(partial) > 1024: raise RuntimeError('cyclic nearest-node chain')
                partial.reverse()
            first_pops, first_nodes = read(system + 0x6c)[0], read(system + 0x40)[0]
            if engine:
                graph = footprint_graph(effective, (width, height), size_class)
                edges = (ctypes.c_uint8 * len(graph)).from_buffer_copy(graph)
                query = (ctypes.c_uint32 * 7)(width, height, *start, *goal, 2048)
                wanted = [actual if actual is not None else -1, first_pops, first_nodes, len(points)]
                for _ in range(2):
                    output = (ctypes.c_int32 * (6 + 2 * 16386))()
                    if args.objects or args.passages:
                        raw = [v for obj in objects for v in (*obj['bounds'], obj['mask'], obj['flags'], obj['linked'])]
                        object_words = (ctypes.c_uint32 * len(raw))(*raw)
                        terrain = (ctypes.c_uint8 * (width * height))(*(terrain_flags if (x,y) in blocked else 0
                            for y in range(height) for x in range(width)))
                        data = ObjectInput(terrain, object_words)
                        inp = (ctypes.c_uint32 * 11)(*query, size_class, query_mask, 0, len(objects))
                        engine.pathing_fine_objects(inp, ctypes.byref(data), output)
                    else:
                        engine.pathing_fine_grid(query, edges, output)
                    path = [[output[6 + i * 2], output[7 + i * 2]] for i in range(output[3])]
                    if list(output[:4]) != wanted or path != [list(p) for p in points]:
                        raise RuntimeError(f'production C fine-search mismatch: {name} class={size_class}')
            link_count = read(tilemap + 0x88)[0]
            node_bytes = bytes(machine.mem_read(nodes, first_nodes * 36))
            run(0x6f14a980, system, 0)
            write(system + 0x20, 2)
            write(system + 0x98, sum((a - b) ** 2 for a, b in zip(start, goal)), 0)
            write(system + 0xcc, 0, 0, 0)
            start_index = run(0x6f147af0, system, *start)
            goal_index = run(0x6f147af0, system, *goal)
            write(system + 0x90, start_index, goal_index)
            repeated = run(0x6f14aa10, system)
            if (repeated != result or read(system + 0x6c)[0] != first_pops or
                read(system + 0x40)[0] != first_nodes or read(tilemap + 0x88)[0] != link_count or
                bytes(machine.mem_read(nodes, first_nodes * 36)) != node_bytes):
                raise RuntimeError('repeated search with retained per-cell metadata differs')
            # Execute the real setup/search/reconstruction request as well.
            source = tuple(profile.get('source',(start[0]+.25,start[1]+.75)))
            target = tuple(profile.get('target',(goal[0]+.25,goal[1]+.75)))
            source=struct.unpack('<ff',struct.pack('<ff',*source))
            target=struct.unpack('<ff',struct.pack('<ff',*target))
            machine.mem_write(source_ptr, struct.pack('<ff', *source))
            machine.mem_write(target_ptr, struct.pack('<ff', *target))
            machine.mem_write(radius_ptr, struct.pack('<f', .25 + .5 * size_class))
            write(mask_ptr, query_mask)
            write(route + 0xc, route_data)
            write(route + 0x18, 1024, 0)
            trace_active=args.queue_composition
            pop_records.clear()
            request_result = run(0x6f148100, system, route, source_ptr, target_ptr, mask_ptr, 100000, radius_ptr, 0)
            trace_active=False
            if args.queue_composition: assert pop_records==core_pops
            route_count = read(route + 0x1c)[0]
            if not 1 <= route_count <= 1024:
                raise RuntimeError('request produced invalid route length')
            route_points = [struct.unpack('<ff', machine.mem_read(route_data + i * 8, 8)) for i in range(route_count)]
            nearest_index = read(system + 0x9c)[0]
            nearest_xy = read(nodes + nearest_index * 36, 2)
            wanted_end = target if expected is not None else source if nearest_index == 0 else tuple(v + .5 for v in nearest_xy)
            adjusted_target = struct.unpack('<ff', machine.mem_read(system + 0x78, 8))
            if (request_result != int(expected is not None) or route_points[0] != wanted_end or route_points[-1] != source or
                read(system + 0xa0)[0] & 0xffff != size_class or read(system + 0x6c)[0] != first_pops):
                raise RuntimeError('setup/search/reconstruction request disagrees with core search')
            if expected is None and nearest_index != 0 and adjusted_target != wanted_end:
                raise RuntimeError('partial route did not update stored adjusted destination')
            records.append(dict(fixture=name, size_class=size_class, cost=actual,
                                pops=read(system + 0x6c)[0], nodes=read(system + 0x40)[0], chain_length=len(chain),
                                request_result=request_result, route_points=route_count, route_end=route_points[0]))
            passage = {}
            if args.passages:
                # Full original fractional footprint consumer, not a supplied
                # graph/model. Its owner resolves the already initialized fine map.
                owner=0x10150000
                write(0x6fd53a48,owner);write(owner+0x24c,system)
                endpoint_results=[]
                for ptr in (source_ptr,target_ptr):
                    endpoint_results.append(run(0x6f149370,system,ptr,mask_ptr,size_class))
                route_words=read(route_data,route_count*2)
                passage=dict(mask=query_mask,terrain_flags=terrain_flags,start=list(start),goal=list(goal),
                    source_words=read(source_ptr,2),target_words=read(target_ptr,2),endpoints=endpoint_results,
                    result=request_result,route_words=route_words)
                passage_records.append(dict(fixture=name,size_class=size_class,**passage))
            if args.fixture:
                engine_cases.append(dict(fixture=name, size_class=size_class, cost=actual, pops=first_pops,
                                         nodes=first_nodes, path=points, partial=partial,
                                         nearest=list(read(nodes + nearest_at * 36, 2)), distance=read(system + 0x98)[0],**passage))
            if args.partials:
                for budget in sorted({0, 1, 5, max(0, first_pops - 1), first_pops, first_pops + 1, 2048}):
                    machine.mem_write(target_ptr, struct.pack('<ff', *target))
                    value = run(0x6f148100, system, route, source_ptr, target_ptr, mask_ptr, budget, radius_ptr, 0)
                    nearest = read(system + 0x9c)[0]
                    index = read(system + 0x94)[0] if value else nearest
                    parent_chain = []
                    while index != 0xffffffff:
                        parent_chain.append(list(read(nodes + index * 36, 2)))
                        index = read(nodes + index * 36 + 0x1c)[0]
                        if len(parent_chain) > 1024: raise RuntimeError('cyclic budget partial chain')
                    parent_chain.reverse()
                    route_count = read(route + 0x1c)[0]
                    route_xy = [list(struct.unpack('<ff', machine.mem_read(route_data + 8*i, 8)))
                                for i in range(route_count)]
                    endpoint = list(target) if value else list(source) if nearest == 0 else [v+.5 for v in read(nodes + 36*nearest,2)]
                    if not route_xy or route_xy[0] != endpoint or route_xy[-1] != list(source):
                        raise RuntimeError('budget partial request endpoint differs from nearest node')
                    if read(system + 0x6c)[0] > budget + 1: raise RuntimeError('budget overcharged')
                    budget_cases.append(dict(fixture=name, size_class=size_class, budget=budget, result=value,
                        pops=read(system + 0x6c)[0], nodes=read(system + 0x40)[0], path=parent_chain,
                        nearest=list(read(nodes + nearest*36,2)), distance=read(system + 0x98)[0], route=route_xy))
                    if engine:
                        raw = [v for obj in objects for v in (*obj['bounds'], obj['mask'], obj['flags'], obj['linked'])]
                        words = (ctypes.c_uint32 * len(raw))(*raw)
                        terrain = (ctypes.c_uint8 * (width * height))(*(terrain_flags if (x,y) in blocked else 0
                            for y in range(height) for x in range(width)))
                        data = ObjectInput(terrain, words)
                        inp = (ctypes.c_uint32 * 11)(width, height, *start, *goal, budget, size_class, query_mask, 0, len(objects))
                        record = budget_cases[-1]
                        wanted = [value, record['pops'], record['nodes'], len(parent_chain), *record['nearest'], record['distance']]
                        for _ in range(2):
                            output = (ctypes.c_int32 * (7 + 2 * 16386))()
                            engine.pathing_fine_partial(inp, ctypes.byref(data), output)
                            path = [[output[7 + 2*i], output[8 + 2*i]] for i in range(output[3])]
                            if list(output[:7]) != wanted or path != parent_chain:
                                raise RuntimeError(f'production C partial mismatch: {name} class={size_class} budget={budget}')
            if name == 'open':
                for label, destination, budget, wanted_result, wanted_pops in (
                        ('same_cell', (start[0] + .875, start[1] + .125), 100000, 1, 0),
                        ('zero_budget', target, 0, 0, 1)):
                    machine.mem_write(target_ptr, struct.pack('<ff', *destination))
                    value = run(0x6f148100, system, route, source_ptr, target_ptr, mask_ptr, budget, radius_ptr, 0)
                    count = read(route + 0x1c)[0]
                    point = struct.unpack('<ff', machine.mem_read(route_data, 8))
                    if (value != wanted_result or count != 1 or point != (destination if wanted_result else source) or
                        read(system + 0x6c)[0] != wanted_pops):
                        raise RuntimeError(f'request edge case failed: {label}')
                    edge_cases.append(dict(case=label, size_class=size_class, result=value, pops=wanted_pops, point=point))
        if len(records) % 64 == 0:
            print(f'{len(records)} retail footprint searches and repeats checked', flush=True)
    report = dict(binary_sha256=digest, cases=len(records), repeated_searches=len(records), complete_requests=len(records), request_edge_cases=edge_cases, mismatches=[],
                  reached=sum(r['cost'] is not None for r in records), exhausted=sum(r['cost'] is None for r in records),
                  seed=None if args.corridors or args.objects or args.passages or args.queue_composition else 12717085, dimensions=[width, height], start=start, goal=goal,
                  scope='original core loop and full setup/search/reconstruction request; allocation/reset/stamp reuse; direct initialized storage and -1/0/1 runtime constants; static terrain and optional mixed object chains; Dijkstra reference uses recovered footprint graph; no path-owned admission or smoothing',
                  searches=records, budget_cases=len(budget_cases))
    if args.queue_composition:
        report.update(queue_pop_records=pop_records,queue_stale=sum(r[2]!=r[3] for r in pop_records),
            queue_reopens=len([r for r in pop_records if r[2]==r[3]])-len({r[1] for r in pop_records if r[2]==r[3]}),
            queue_equal_keys=len(pop_records)-len({r[0] for r in pop_records}),
            queue_route_words=read(route_data,route_count*2),shortest_reference_cost=expected)
        if engine:
            engine.pathing_fine_queue_trace.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ObjectInput),ctypes.POINTER(ctypes.c_uint32)]
            terrain=(ctypes.c_uint8*(width*height))(*(2 if (x,y) in blocked else 0 for y in range(height) for x in range(width)))
            out=(ctypes.c_uint32*(1+10*2048))()
            q=(ctypes.c_uint32*11)(width,height,*start,*goal,2048,0,0x02000002,0,0)
            engine.pathing_fine_queue_trace(q,ctypes.byref(ObjectInput(terrain,None)),out)
            actual=[list(out[1+10*i:11+10*i]) for i in range(out[0])]
            assert actual==pop_records,(next(((i,a,b) for i,(a,b) in enumerate(zip(actual,pop_records)) if a!=b),None))
            report['queue_engine_pops']=len(actual)
        frozen=json.loads(Path(__file__).with_name('fixtures').joinpath('retail-fine-queue-1.27.json').read_text())
        assert pop_records==frozen['pops'] and report['queue_route_words']==frozen['route_words']
        budget_input=frozen['budget_goal_input']
        terrain=bytes.fromhex(budget_input['cells'])
        machine.mem_write(cells,b''.join(struct.pack('<I',(v<<24)|0xffffff) for v in terrain))
        machine.mem_write(bitmap,bytes(1024))
        budget_result=run(0x6f148100,system,route,source_ptr,target_ptr,mask_ptr,700,radius_ptr,0)
        count=read(route+0x1c)[0]
        budget_words=read(route_data,count*2)
        assert budget_result==0 and read(system+0x6c)[0]==701 and read(system+0x98)[0]==0
        assert budget_words[:2]==[0x422e0000,0x422e0000] #43.5,43.5; fractional goal not published.
        report.update(budget_goal_result=budget_result,budget_goal_work=701,budget_goal_route_words=budget_words)
        if engine:
            engine.pathing_fine_request_words.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ObjectInput),ctypes.POINTER(ctypes.c_uint32)]
            terrain_c=(ctypes.c_uint8*len(terrain)).from_buffer_copy(terrain)
            q=(ctypes.c_uint32*15)(width,height,*start,*goal,700,0,0x02000002,0,0,*read(source_ptr,2),*read(target_ptr,2))
            out=(ctypes.c_uint32*(6+2*16386))()
            engine.pathing_fine_request_words(q,ctypes.byref(ObjectInput(terrain_c,None)),out)
            assert out[0]==0 and out[1]==701 and list(out[6:6+2*out[3]])==budget_words
    if args.stamp_wrap:
        def wrap_request(mask,cls):
            write(mask_ptr,mask);machine.mem_write(radius_ptr,struct.pack('<f',.25+.5*cls))
            result=run(0x6f148100,system,route,source_ptr,target_ptr,mask_ptr,100000,radius_ptr,0)
            count=read(route+0x1c)[0]
            return dict(result=result,work=read(system+0x6c)[0],nodes=read(system+0x40)[0],
                nearest=read(nodes+read(system+0x9c)[0]*36,2),distance=read(system+0x98)[0],
                route_words=read(route_data,count*2),node_state=[
                    [v[0],v[1],v[5],v[6],v[2],v[7],0 if v[3]==0xffffffff else 1 if v[3]==0xfffffffe else 2]
                    for v in (read(nodes+36*i,8) for i in range(read(system+0x40)[0]))])
        # One pre-call fixture seed near the boundary; every subsequent stamp
        # mutation is original14ad50's ushort increment. Metadata comes from the
        # completed original requests above and is retained across all four.
        write(system+0x20,(read(system+0x20)[0]&0xffff0000)|0xfffe)
        reused=[]
        for cls,mask,stamp in zip(range(4),(0x02000002,0x04000004,0x40000040,0x80000080),(65535,0,1,2)):
            row=wrap_request(mask,cls)
            assert read(system+0x20)[0]&0xffff==stamp
            row.update(cls=cls,mask=mask,stamp=stamp)
            reused.append(row)
        # Fresh metadata is a clean control, not a substitute for the retained
        # sequence: original reset/setup/node creation still execute normally.
        for i,row in enumerate(reused):
            machine.mem_write(cells,b''.join(struct.pack('<I',(v<<24)|0xffffff) for v in terrain))
            machine.mem_write(bitmap,bytes(1024));write(tilemap+0x88,0)
            write(system+0x20,100+i)
            clean=wrap_request(row['mask'],row['cls'])
            assert clean=={k:v for k,v in row.items() if k not in ('cls','mask','stamp')}
            if engine:
                terrain_c=(ctypes.c_uint8*len(terrain)).from_buffer_copy(terrain)
                q=(ctypes.c_uint32*15)(width,height,*start,*goal,2048,row['cls'],row['mask'],0,0,*read(source_ptr,2),*read(target_ptr,2))
                out=(ctypes.c_uint32*(6+2*16386))()
                engine.pathing_fine_request_words(q,ctypes.byref(ObjectInput(terrain_c,None)),out)
                assert list(out[:3])==[row['result'],row['work'],row['nodes']]
                assert list(out[6:6+2*out[3]])==row['route_words']
                engine.pathing_fine_node_state.argtypes=[ctypes.POINTER(ctypes.c_uint32)]
                state=(ctypes.c_uint32*(1+7*16384))()
                engine.pathing_fine_node_state(state)
                assert [list(state[1+7*j:8+7*j]) for j in range(state[0])]==row['node_state']
        report.update(stamp_wrap_cases=reused,stamp_wrap_reused_requests=4,stamp_wrap_clean_requests=4,
            stamp_wrap_engine_requests=4 if engine else 0)
        if args.wrap_fixture:
            frozen_wrap=json.loads(args.wrap_fixture.read_text())
            assert reused==frozen_wrap['cases']
            assert hashlib.sha256(terrain).hexdigest()==frozen_wrap['terrain_sha256']
    if args.passages:
        report.update(passage_cases=len(records),passage_shapes=len(shapes),movement_lanes=4,subcell_offsets=4,
            passage_endpoint_cases=2*len(passage_records),
            passage_words_sha256=hashlib.sha256(json.dumps(passage_records,sort_keys=True,separators=(',',':')).encode()).hexdigest())
    if engine:
        report.update(engine_queries=len(records), engine_repeats=len(records),
                      engine_partial_queries=len(budget_cases), engine_partial_repeats=len(budget_cases),
                      engine_sha256=hashlib.sha256(args.engine_library.read_bytes()).hexdigest())
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    if args.fixture:
        fixture = dict(binary_sha256=digest, seed=None if args.corridors or args.objects or args.passages or args.queue_composition else 12717085, dimensions=[width, height], start=start, goal=goal,
                       scope='original full fine-search cell route, cost, pops and allocated nodes; initialized static/object chains; excludes public admission and smoothing',
                       profiles=profiles,
                       budget_cases=budget_cases,
                       maps={name: bytes(int((x, y) in blocked) for y in range(height) for x in range(width)).hex()
                             for name, blocked in fixtures}, cases=engine_cases)
        args.fixture.parent.mkdir(parents=True, exist_ok=True)
        args.fixture.write_text(json.dumps(fixture, separators=(',', ':')) + '\n')
    print(f'{len(records)} full retail fine-grid searches plus {len(records)} stamp-reuse repeats and {len(records)} complete requests; all pass')


if __name__ == '__main__':
    main()
