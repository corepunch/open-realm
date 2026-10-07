#!/usr/bin/env python3
"""MAP-04.1/MAP-04.2 original-code oracle: nested self/target exclusions.

Executes unmodified game.dll 1.27.1.7085 code in Unicorn (no stubs, no replaced
branches) over synthetic preallocated 64x64 fine / 32-16-8-4 hierarchy maps:

* Path_RequestAcceleratedRoute 6f166c30: self/target rectangle clear (15d360
  mode1) -> PathAcc_BuildRoute -> rebuild (mode0) self, then target.
* Path_RequestFineRoute 6f166e90: self/target occupancy counter (+40) increments
  -> PathFine_BuildRoute -> target, then self decrement.

Two overlapping objects (self A, target B), an overlapping bystander C, blocked
terrain inside A, A&B, B&C, at a rounded-coverage edge and far away. Every
hierarchy class byte (all four levels, four 2-bit lanes) is snapshotted at
every exclusion boundary through read-only code hooks; every fine cell in the
object neighbourhood is re-queried with original PathFine_TestOccupiedCell
under each observed counter state. MAP-04.2 adds a pending fine terrain edit
(original PathCell_EditTerrainFlags 6f054000, no hierarchy publication) before
a request and checks what the request's restoration publishes.
"""
import argparse, hashlib, itertools, json, struct
from pathlib import Path

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
W = H = 64
SIDE = 32
TERRAIN = {  # fine (x, y): high-byte terrain flags
    (17, 17): 0xff,   # inside A only
    (19, 18): 0xff,   # inside A and B
    (21, 21): 0x02,   # inside B and C, ground lane only
    (23, 20): 0xff,   # outside every object, inside B's rounded coverage (x=floor(22/2)*2+1)
    (40, 40): 0xff,   # far control
}
# Rectangles are WC3PathIntegerRectangle (min_y, min_x, max_y, max_x), half-open.
RECTS = {'A': (16, 16, 20, 20), 'B': (18, 19, 23, 22), 'C': (21, 21, 25, 26)}
WINDOW = (12, 12, 30, 30)  # fine cells re-queried: x0, y0, x1, y1 (half-open)


def cells_of(rect):
    y0, x0, y1, x1 = rect
    return [(x, y) for y in range(y0, y1) for x in range(x0, x1)]


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EDX
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--expected', type=Path, help='write frozen expected JSON')
    ap.add_argument('--reference', type=Path, help='compare against frozen expected JSON')
    ap.add_argument('--edit-expected', type=Path, help='write the MAP-04.2 pending-edit cases as their own frozen JSON')
    ap.add_argument('--edit-reference', type=Path, help='compare MAP-04.2 pending-edit cases against frozen JSON')
    ap.add_argument('--engine-fixture', type=Path, help='export complete native cell/counter stages for engine tests')
    args = ap.parse_args()
    binary = args.binary.read_bytes()
    if hashlib.sha256(binary).hexdigest() != SHA:
        ap.error('requires game.dll 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', binary, opt + o)[0] for o in (28, 56))
    m = Uc(UC_ARCH_X86, UC_MODE_32)
    m.mem_map(base, (size + 4095) & ~4095)
    m.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        sec = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
        va, count, off = struct.unpack_from('<III', binary, sec + 12)
        if count:
            m.mem_write(base + va, binary[off:off + count])
    m.mem_map(0, 0x1000)
    for r in (0x10000000, 0x10400000, 0x10800000, 0x10a00000):
        m.mem_map(r, 0x200000)
    m.mem_map(0x20000000, 0x10000)
    stack, stop = 0x20008000, 0x30000000

    def write(a, *v):
        m.mem_write(a, struct.pack('<%dI' % len(v), *(x & 0xffffffff for x in v)))

    def read(a, n=1):
        return list(struct.unpack('<%dI' % n, m.mem_read(a, 4 * n)))

    def run(entry, ecx, *stack_args, edx=None):
        write(stack, stop, *stack_args)
        m.reg_write(UC_X86_REG_ESP, stack)
        m.reg_write(UC_X86_REG_ECX, ecx)
        if edx is not None:
            m.reg_write(UC_X86_REG_EDX, edx)
        m.emu_start(entry, stop, count=50000000)
        if m.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError('instruction budget exceeded at %x' % entry)
        return m.reg_read(UC_X86_REG_EAX)

    system, tilemap = 0x10000000, 0x10000200
    source_ptr, goal_ptr = 0x10000400, 0x10000410
    path, owner, acc = 0x10110000, 0x10111000, 0x10112000
    maps = [0x10400000 + n * 0x100 for n in range(4)]
    storage = [0x10410000 + n * 0x10000 for n in range(4)]
    acc_nodes, acc_heap, coarse, acc_source, acc_target = 0x10480000, 0x10500000, 0x10580000, 0x10590000, 0x10590010
    cells, bitmap, nodes, links, heap, route_data = 0x10800000, 0x10810000, 0x10820000, 0x10880000, 0x10900000, 0x10980000
    objects = {k: 0x10a00000 + n * 0x100 for n, k in enumerate('ABC')}
    fine_bucket, acc_bucket = 0x6fd53a90 + 0x54, 0x6fd53a90 + 0x38
    m.mem_write(0x6fd53a74, struct.pack('<f', -128000.0078125))
    run(0x6f0040d0, 0)
    write(0x6fd53a48, owner)
    write(owner + 0x23c, *maps)
    write(owner + 0x24c, system, acc)
    write(owner + 0x538, 100)

    def classes():
        out = []
        for lev in range(4):
            side = SIDE >> lev
            data = bytes(m.mem_read(storage[lev], side * side * 8))
            out.extend(data[c * 8 + 7] for c in range(side * side))
        return out

    def where(index):
        for lev in range(4):
            n = (SIDE >> lev) ** 2
            if index < n:
                return lev, index % (SIDE >> lev), index // (SIDE >> lev)
            index -= n

    def diff(before, after):
        return [[*where(i), b, a] for i, (b, a) in enumerate(zip(before, after)) if a != b]

    def build(fixture):
        """Fine terrain, ordered cell links, objects and the full original hierarchy."""
        m.mem_write(system, bytes(0x400))
        m.mem_write(tilemap, bytes(0x100))
        m.mem_write(bitmap, bytes(W * H // 8))
        terrain = dict(TERRAIN)
        words = [((terrain.get((x, y), 0)) << 24) | 0xffffff for y in range(H) for x in range(W)]
        # Links: each covered cell gets a chain in fixture['link_order'].
        link_words = []
        for (x, y) in sorted({c for k in fixture['present'] for c in cells_of(RECTS[k])}, key=lambda c: (c[1], c[0])):
            chain = [k for k in fixture['link_order'] if k in fixture['present'] and (x, y) in cells_of(RECTS[k])]
            head = len(link_words)
            for n, k in enumerate(chain):
                nxt = head + n + 1 if n + 1 < len(chain) else 0xffffff
                link_words.append((0x01000000 | nxt, objects[k]))
            words[y * W + x] = (words[y * W + x] & 0xff000000) | head
        m.mem_write(cells, struct.pack('<%dI' % len(words), *words))
        for n, (w0, w1) in enumerate(link_words):
            write(links + n * 8, w0, w1)
        write(system + 0x1c, tilemap, 1)
        write(system + 0x30, nodes)
        write(system + 0x3c, 4096, 0)
        write(system + 0x50, heap)
        write(system + 0x5c, 32768, 1, -3, 100000, 0)
        write(tilemap + 0x28, cells)
        write(tilemap + 0x3c, W, H)
        write(tilemap + 0x54, 0, 0, H, W)
        write(tilemap + 0x78, links)
        write(tilemap + 0x84, 8192, len(link_words))
        write(tilemap + 0x98, bitmap)
        write(tilemap + 0xac, 0xffffff)
        for k, obj in objects.items():
            m.mem_write(obj, bytes(0x80))
            write(obj + 0x1c, *RECTS[k])
            write(obj + 0x34, 0x01000006, 0)
            write(obj + 0x40, fixture['occupancy'][k])
        m.mem_write(acc, bytes(0x400))
        for lev, (tm, data) in enumerate(zip(maps, storage)):
            side = SIDE >> lev
            m.mem_write(tm, bytes(0x100))
            m.mem_write(data, bytes(side * side * 8))
            write(tm + 0x28, data)
            write(tm + 0x3c, side, side)
            m.mem_write(tm + 0x64, struct.pack('<2f', 2 << lev, 1 / (2 << lev)))
            write(acc + 0x1c + 4 * lev, tm)
        write(acc + 0x5c, acc_nodes)
        write(acc + 0x68, 4096, 0)
        write(acc + 0x7c, acc_heap)
        write(acc + 0x88, 65536, 0)
        run(0x6f15d360, owner, 0, 0)  # original full publication from fine queries

    def set_path(self_key, target_key, lane=0, cls=0):
        m.mem_write(path, bytes(0x100))
        goal = (59.25, 59.75)
        m.mem_write(path + 0x1c, struct.pack('<4f', *goal, *goal))
        write(path + 0x40, route_data)
        write(path + 0x4c, 1024, 0)
        write(path + 0x60, coarse)
        write(path + 0x6c, 1024, 0, -1, -1)
        write(path + 0x84, 700 | 400 << 16)
        write(path + 0x88, 0x200000 | lane << 30)
        write(path + 0x9c, 0x02000000 | 0x06)
        write(path + 0xa0, objects[self_key] if self_key else 0, objects[target_key] if target_key else 0)
        m.mem_write(path + 0xb4, struct.pack('<f', .25 + .5 * cls))
        write(fine_bucket, 700 | 1 << 16, 1100, 0, 0, 0, 0, 0)
        write(acc_bucket, 400 | 2 << 16, 900, 0, 0, 0, 0, 0)
        write(owner, 0, 0)
        m.mem_write(source_ptr, struct.pack('<2f', 8.25, 8.75))
        m.mem_write(goal_ptr, struct.pack('<2f', *goal))
        m.mem_write(acc_source, struct.pack('<2f', 8.25 / 2, 8.75 / 2))
        m.mem_write(acc_target, struct.pack('<2f', goal[0] / 2, goal[1] / 2))

    def counters():
        return {k: read(obj + 0x40)[0] for k, obj in objects.items()}

    def fine_cells(state, mask, target_key):
        """Original 1489a0 over the window with the given counters; restores counters/stamps."""
        saved = {k: bytes(m.mem_read(obj, 0x80)) for k, obj in objects.items()}
        saved_sys = bytes(m.mem_read(system, 0x100))
        saved_map = bytes(m.mem_read(tilemap, 0x100))
        for k, v in state.items():
            write(objects[k] + 0x40, v)
        write(system + 0xa4, mask, objects[target_key] if target_key else 0)
        result = []
        x0, y0, x1, y1 = WINDOW
        for y in range(y0, y1):
            row = []
            for x in range(x0, x1):
                write(system + 0xcc, 0, 0, 0)  # target_seen, obstruction, endpoint mode
                ok = run(0x6f1489a0, system, x, y)
                row.append(int(ok) | read(system + 0xcc)[0] << 1)
            result.append(row)
        for k, obj in objects.items():
            m.mem_write(obj, saved[k])
        m.mem_write(system, saved_sys)
        m.mem_write(tilemap, saved_map)
        return result

    # ---------------- hooks (read-only observers) ----------------
    events = []

    def hook(uc, address, size, data):
        if address == 0x6f15d360:
            esp = uc.reg_read(UC_X86_REG_ESP)
            rect_ptr, mode = read(esp + 4, 2)
            who = next((k for k, o in objects.items() if o + 0x1c == rect_ptr), hex(rect_ptr))
            events.append(dict(at='15d360', object=who, mode=mode, rect=read(rect_ptr, 4) if rect_ptr else None))
        elif address in RETURNS_ACC:
            events.append(dict(at=RETURNS_ACC[address], classes=classes(), counters=counters()))
        elif address in FINE_POINTS:
            events.append(dict(at=FINE_POINTS[address], counters=counters()))
    RETURNS_ACC = {0x6f166d3a: 'after_clear_self', 0x6f166d51: 'after_clear_target', 0x6f162cb0: 'search_entry',
                   0x6f166ddc: 'after_rebuild_self', 0x6f166df3: 'after_rebuild_target'}
    FINE_POINTS = {0x6f166f69: 'after_self_inc', 0x6f166f80: 'after_target_inc', 0x6f148100: 'search_entry',
                   0x6f167044: 'after_target_dec', 0x6f16704b: 'after_self_dec'}
    roles = [('A', 'B'), ('B', 'A'), ('A', 'A'), ('A', None), (None, 'B')]
    occupancies = {'dynamic': dict(A=0, B=0, C=0), 'hierarchy': dict(A=0x10000000, B=0x10000000, C=0x10000000),
                   'outer_scope': dict(A=1, B=0x10000001, C=0x10000000)}
    link_orders = ['ABC', 'BAC', 'CBA']

    def finals(fixture, self_key, target_key):
        """Observer-free control: complete requests with no hooks installed."""
        build(fixture)
        set_path(self_key, target_key)
        r1 = run(0x6f166c30, path, acc_source, acc_target, 1)
        out = dict(coarse_result=r1, classes=hashlib.sha256(bytes(classes())).hexdigest(),
                   coarse=read(coarse, 2 * read(path + 0x70)[0]))
        build(fixture)
        set_path(self_key, target_key)
        r2 = run(0x6f166e90, path, source_ptr, goal_ptr)
        out.update(fine_result=r2, fine=read(route_data, 2 * read(path + 0x50)[0]), counters=counters(),
                   fine_index=read(path + 0x74)[0], flags=read(path + 0x88)[0])
        return out
    m.ctl_flush_tb()
    control = {}
    for (self_key, target_key), (occ_name, occ), order in itertools.product(roles, occupancies.items(), link_orders):
        control[self_key, target_key, occ_name, order] = finals(dict(present='ABC', link_order=order, occupancy=occ), self_key, target_key)
    hooks = [m.hook_add(UC_HOOK_CODE, hook, begin=a, end=a) for a in [0x6f15d360, *RETURNS_ACC, *FINE_POINTS]]
    m.ctl_flush_tb()

    coarse_cases, fine_cases, edit_cases, engine_cases = [], [], [], []
    control_equal = 0
    for (self_key, target_key), (occ_name, occ), order in itertools.product(roles, occupancies.items(), link_orders):
        fixture = dict(present='ABC', link_order=order, occupancy=occ)
        # ---- coarse scope ----
        build(fixture)
        baseline = classes()
        set_path(self_key, target_key)
        events.clear()
        result = run(0x6f166c30, path, acc_source, acc_target, 1)
        final = classes()
        calls = [e for e in events if e['at'] == '15d360']
        snaps = [e for e in events if e['at'] != '15d360']
        # Independent primitive replay on a fresh identical world.
        build(fixture)
        replay = {}
        sequence = [(k, 1) for k in (self_key, target_key) if k] + [(k, 0) for k in (self_key, target_key) if k]
        for label, k, mode in [('after_clear_self', self_key, 1), ('after_clear_target', target_key, 1),
                               ('after_rebuild_self', self_key, 0), ('after_rebuild_target', target_key, 0)]:
            if k:
                run(0x6f15d360, owner, objects[k] + 0x1c, mode)
            replay[label] = classes()
        steps = {s['at']: s['classes'] for s in snaps if s['at'] != 'search_entry'}
        assert [s['at'] for s in snaps] == ['after_clear_self', 'after_clear_target', 'search_entry',
                                            'after_rebuild_self', 'after_rebuild_target']
        assert steps == replay, ('coarse primitive replay', self_key, target_key, occ_name, order)
        assert final == baseline, ('coarse restoration', self_key, target_key, occ_name, order)
        assert all(s['counters'] == occ for s in snaps), 'coarse scope must not touch +40'
        assert [(c['object'], c['mode']) for c in calls] == sequence
        search = next(s for s in snaps if s['at'] == 'search_entry')['classes']
        # Base coverage model: inclusive floor(min/2)..floor(max/2) on each axis, cleared.
        covered = set()
        for k in {self_key, target_key} - {None}:
            y0, x0, y1, x1 = RECTS[k]
            covered |= {(x, y) for y in range(y0 // 2, y1 // 2 + 1) for x in range(x0 // 2, x1 // 2 + 1)}
        base_search = {(x, y): search[y * SIDE + x] for y in range(SIDE) for x in range(SIDE)}
        assert all(base_search[c] == 0 for c in covered)
        assert all(base_search[(x, y)] == baseline[y * SIDE + x] for (x, y) in base_search if (x, y) not in covered)
        after_self = next((s['classes'] for s in snaps if s['at'] == 'after_rebuild_self'), None)
        premature = []
        if self_key and target_key and self_key != target_key and after_self is not None:
            y0, x0, y1, x1 = RECTS[target_key]
            tcov = {(x, y) for y in range(y0 // 2, y1 // 2 + 1) for x in range(x0 // 2, x1 // 2 + 1)}
            premature = sorted([x, y, after_self[y * SIDE + x]] for (x, y) in tcov
                               if after_self[y * SIDE + x] != 0)
        coarse_cases.append(dict(
            self=self_key, target=target_key, occupancy=occ_name, link_order=order, result=result,
            calls=[[c['object'], c['mode'], c['rect']] for c in calls],
            baseline_nonzero=[[*where(i), v] for i, v in enumerate(baseline) if v],
            steps={s['at']: diff(baseline, s['classes']) for s in snaps},
            final_equals_baseline=final == baseline, covered_base_cells=len(covered),
            target_cells_restored_before_target_rebuild=premature,
            coarse_count=read(path + 0x70)[0], coarse_words=read(coarse, 2 * read(path + 0x70)[0]),
            flags=read(path + 0x88)[0]))
        # ---- fine scope ----
        build(fixture)
        set_path(self_key, target_key)
        before = counters()
        events.clear()
        mask = read(path + 0x9c)[0]
        result = run(0x6f166e90, path, source_ptr, goal_ptr)
        after = counters()
        points = [e for e in events if e['at'] in FINE_POINTS.values()]
        n = read(path + 0x50)[0]
        route = read(route_data, 2 * n)
        search_state = next(p['counters'] for p in points if p['at'] == 'search_entry')
        # The search must equal a direct 148100 request under the observed in-scope counters.
        build(fixture)
        for k, v in search_state.items():
            write(objects[k] + 0x40, v)
        write(route_data - 0x100 + 0xc, route_data)  # scratch route header below route_data
        hdr = route_data - 0x100
        write(hdr + 0x18, 1024, 0)
        radius_ptr, mask_ptr = 0x10000430, 0x10000420
        m.mem_write(radius_ptr, struct.pack('<f', .25))
        write(mask_ptr, mask)
        direct = run(0x6f148100, system, hdr, source_ptr, goal_ptr, mask_ptr, 700, radius_ptr,
                     objects[target_key] if target_key else 0)
        assert read(hdr + 0x1c)[0] == n and read(route_data, 2 * n) == route, ('fine direct replay', self_key, target_key, occ_name, order)
        states = {'before': before, **{p['at']: p['counters'] for p in points}, 'after': after}
        build(fixture)
        grids = {k: fine_cells(v, mask, target_key) for k, v in states.items()}
        changed = {k: [[WINDOW[0] + x, WINDOW[1] + y, grids['before'][y][x], g[y][x]]
                       for y in range(len(g)) for x in range(len(g[0])) if g[y][x] != grids['before'][y][x]]
                   for k, g in grids.items() if k != 'before'}
        assert after == before, ('fine counters restored', self_key, target_key, occ_name, order)
        c = control[self_key, target_key, occ_name, order]
        observed_final = coarse_cases[-1]
        assert (c['coarse_result'], c['coarse'], c['classes']) == (observed_final['result'], observed_final['coarse_words'],
                                                                   hashlib.sha256(bytes(final)).hexdigest()), 'coarse control'
        assert (c['fine_result'], c['fine'], c['counters'], c['fine_index']) == (result, route, after, read(path + 0x74)[0]), 'fine control'
        control_equal += 1
        assert grids['after'] == grids['before']
        engine_cases.append(dict(self=self_key, target=target_key, occupancy=occ_name, link_order=order,
            fine_cells=grids, fine_counters=states, coarse_cells={s['at']: s['classes'] for s in snaps}))
        order_seen = [p['at'] for p in points]
        fine_cases.append(dict(
            self=self_key, target=target_key, occupancy=occ_name, link_order=order, result=result,
            counter_states={k: {o: hex(v) for o, v in s.items()} for k, s in states.items()},
            observer_order=order_seen, changed_cells_vs_before=changed,
            route_count=n, route_words=route, direct_search_result=direct,
            fine_index=read(path + 0x74)[0], flags=read(path + 0x88)[0]))

    # ---- MAP-04.2: one restoration assertion per reachable exit (and pre-acquire exits) ----
    exit_cases = []
    ring = [(x, 54) for x in range(54, 64)] + [(54, y) for y in range(55, 64)]
    for exit_name, kind in [('coarse_exact', 'coarse'), ('coarse_partial', 'coarse'), ('coarse_interval_denied', 'coarse'),
                            ('coarse_admission_denied', 'coarse'), ('fine_exact', 'fine'), ('fine_partial', 'fine'),
                            ('fine_interval_denied', 'fine'), ('fine_admission_denied', 'fine')]:
        fixture = dict(present='ABC', link_order='ABC', occupancy=occupancies['hierarchy'])
        build(fixture)
        if exit_name.endswith('partial'):
            for (x, y) in ring:  # enclose the goal; publish fully so the hierarchy knows the wall
                run(0x6f054000, cells + (y * W + x) * 4, 1, edx=0xff)
            run(0x6f15d360, owner, 0, 0)
        baseline, before = classes(), counters()
        set_path('A', 'B')
        if kind == 'fine' and not exit_name.endswith('partial'):
            near = (12.25, 9.75)  # reachable within the 700-pop fine budget
            m.mem_write(goal_ptr, struct.pack('<2f', *near))
            m.mem_write(path + 0x1c, struct.pack('<4f', *near, *near))
        if exit_name == 'coarse_interval_denied':
            write(path + 0x80, 100)
        if exit_name == 'fine_interval_denied':
            write(path + 0x7c, 100)
        if exit_name == 'coarse_admission_denied':
            write(acc_bucket, 400 | 2 << 16, 900, 901, 0, 0, 0, 0)
        if exit_name == 'fine_admission_denied':
            write(fine_bucket, 700 | 1 << 16, 1100, 1101, 0, 0, 0, 0)
        events.clear()
        if kind == 'coarse':
            result = run(0x6f166c30, path, acc_source, acc_target, 1)
        else:
            result = run(0x6f166e90, path, source_ptr, goal_ptr)
        trace = [(e['at'], e.get('object'), e.get('mode')) for e in events]
        row = dict(exit=exit_name, result=result, flags=hex(read(path + 0x88)[0]),
                   classes_restored=classes() == baseline, counters_restored=counters() == before,
                   exclusion_events=[e[0] if e[0] != '15d360' else '15d360:%s:%d' % (e[1], e[2]) for e in trace],
                   request_times=[read(path + 0x7c)[0], read(path + 0x80)[0]],
                   adjusted=read(path + 0x24, 2), coarse_count=read(path + 0x70)[0], fine_count=read(path + 0x50)[0])
        assert row['classes_restored'] and row['counters_restored'], row
        if 'denied' in exit_name:
            assert result == 0 and not [e for e in trace if e[0] == '15d360' or e[0] in FINE_POINTS.values()], row
        exit_cases.append(row)
    assert [r['exit'] for r in exit_cases if int(r['flags'], 16) & 0x20000000] == ['coarse_partial']
    assert [r['exit'] for r in exit_cases if int(r['flags'], 16) & 0x10000000] == ['fine_partial']

    # ---- MAP-04.2: pending terrain edit published by the request restoration ----
    for (self_key, target_key), edit_cell, flags in itertools.product(
            [('A', 'B'), (None, 'B'), ('A', None)], [(22, 19), (23, 22), (30, 30)], [0xff, 0x02]):
        fixture = dict(present='ABC', link_order='ABC', occupancy=occupancies['dynamic'])
        build(fixture)
        baseline = classes()
        x, y = edit_cell
        run(0x6f054000, cells + (y * W + x) * 4, 1, edx=flags)  # original setter, no hierarchy call
        assert classes() == baseline
        fine_word = read(cells + (y * W + x) * 4)[0]
        set_path(self_key, target_key)
        events.clear()
        run(0x6f166c30, path, acc_source, acc_target, 1)
        after_request = classes()
        published = diff(baseline, after_request)
        # Reference: what a full publication would show; the request publishes only its coverage.
        run(0x6f15d360, owner, 0, 0)
        full = classes()
        covered = set()
        for k in {self_key, target_key} - {None}:
            y0, x0, y1, x1 = RECTS[k]
            covered |= {(cx, cy) for cy in range(y0 // 2, y1 // 2 + 1) for cx in range(x0 // 2, x1 // 2 + 1)}
        inside = (x // 2, y // 2) in covered
        assert (after_request == full) == (inside or full == baseline), (self_key, target_key, edit_cell)
        edit_cases.append(dict(self=self_key, target=target_key, edit_cell=list(edit_cell), flags=flags,
                               fine_word=hex(fine_word), edit_base_cell_in_request_coverage=inside,
                               published_by_request=published, pending_after_request=diff(after_request, full)))
    for h in hooks:
        m.hook_del(h)
    payload = dict(version=1, binary_sha256=SHA, scope=__doc__.strip(), terrain={'%d,%d' % k: v for k, v in TERRAIN.items()},
                   rectangles=RECTS, rect_order='min_y,min_x,max_y,max_x half-open', window=WINDOW,
                   hierarchy_dims=[SIDE >> l for l in range(4)], fine_dims=[W, H],
                   coarse_cases=coarse_cases, fine_cases=fine_cases, edit_cases=edit_cases)
    blob = json.dumps(payload, sort_keys=True, separators=(',', ':')) + '\n'
    if args.expected:
        args.expected.write_text(blob)
    if args.reference:
        assert json.loads(args.reference.read_text()) == json.loads(blob), 'frozen expected differs'
    edit_blob = json.dumps(dict(version=1, binary_sha256=SHA, entry='6f166c30', setter='6f054000',
                                scope='Pending fine terrain edit (original 054000, no hierarchy call) before a complete original '
                                      'coarse request; restoration rebuild (15d360 mode0) publishes it only inside the rounded '
                                      'self/target coverage. Synthetic 64-cell maps; fixture as MAP-04.1.',
                                terrain={'%d,%d' % k: v for k, v in TERRAIN.items()}, rectangles=RECTS,
                                cases=edit_cases, exit_cases=exit_cases), sort_keys=True, separators=(',', ':')) + '\n'
    if args.edit_expected:
        args.edit_expected.write_text(edit_blob)
    if args.edit_reference:
        assert json.loads(args.edit_reference.read_text()) == json.loads(edit_blob), 'frozen edit cases differ'
    summary = dict(binary_sha256=SHA, passed=True, observer_free_control_equal=control_equal, coarse_cases=len(coarse_cases), fine_cases=len(fine_cases),
                   edit_cases=len(edit_cases), payload_sha256=hashlib.sha256(blob.encode()).hexdigest(),
                   edit_payload_sha256=hashlib.sha256(edit_blob.encode()).hexdigest(),
                   premature_target_restorations=sum(bool(c['target_cells_restored_before_target_rebuild']) for c in coarse_cases),
                   edit_published=sum(bool(c['published_by_request']) for c in edit_cases))
    if args.engine_fixture:
        args.engine_fixture.write_text(json.dumps(dict(binary_sha256=SHA, cases=engine_cases),
            sort_keys=True, separators=(',', ':'))+'\n')
    args.report.write_text(json.dumps(summary, indent=1) + '\n')
    print(json.dumps(summary, indent=1))


if __name__ == '__main__':
    main()
