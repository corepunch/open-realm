#!/usr/bin/env python3
"""ROUTE-02.2 research oracle: ordered next-step blocker candidates at the
retail 32-token cap, their resolver consequences, and waypoint selection
before/after an obstruction change between two selector calls.

Original game.dll 1.27.1.7085 code only (Unicorn); synthetic maps, objects,
movers, paths, groups and registry. No retail routine is stubbed or replaced.

Parts
  A  complete 6f166140 next-step collection: 4 classes x 9 direction codes,
     visited-cell order (hook at 6f148ad0 entry) and the capped candidate
     vector against an independent model (cell order x lazy-chain order,
     per-cell stamps, null tokens, 32 cap).
  B  complete 6f166140 -> 6f168360 resolver over capped ordered vectors:
     which peers are visited, assigned or ignored at positions 0..39.
  C  complete 6f167bf0/6f165e60 waypoint selection over object occupancy and
     a sequence of obstruction changes between successive selector calls.
  D  memory-write audit of complete 6f167bf0 and 6f166140 calls: which
     addresses the original code writes while sampling (no cell/link/object
     occupancy writes besides stamps and the self +40 counter).
  E  composition: an object with moving flag 20000000 on the next-step cell
     is ignored by selection while fine+d4==0 but collected by 6f166140.
"""
import argparse
import hashlib
import itertools
import json
import struct
from pathlib import Path

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
CODES = [0, 1, 2, 3, 4, 6, 8, 9, 12]
TAG = 0x60706375


def strip_model(cls, x, y, code):
    """Cells passed to 6f148ad0 by 6f149560/1497c0/149b00/149e60, in order."""
    row = lambda x0, yy, n: [(x0 + i, yy) for i in range(n)]
    col = lambda xx, y0, n: [(xx, y0 + i) for i in range(n)]
    rect = lambda x0, y0, w, h: [c for j in range(h) for c in row(x0, y0 + j, w)]
    if cls == 0:
        extra = {3: [(x - 1, y), (x, y + 1)], 6: [(x - 1, y), (x, y - 1)],
                 9: [(x + 1, y), (x, y + 1)], 12: [(x + 1, y), (x, y - 1)]}
        return [(x, y)] + extra.get(code, [])
    n = cls + 1
    table = {
        1: {1: row(x-1, y-1, 2), 2: col(x, y-1, 2), 3: row(x-2, y-1, 3) + col(x, y, 2), 4: row(x-1, y, 2),
            6: row(x-2, y, 3) + col(x, y-2, 2), 8: col(x-1, y-1, 2), 9: row(x-1, y-1, 3) + col(x-1, y, 2),
            12: row(x-1, y, 3) + col(x-1, y-2, 2)},
        2: {1: row(x-1, y-1, 3), 2: col(x+1, y-1, 3), 3: row(x-2, y-1, 4) + col(x+1, y, 3), 4: row(x-1, y+1, 3),
            6: row(x-2, y+1, 4) + col(x+1, y-2, 3), 8: col(x-1, y-1, 3), 9: row(x-1, y-1, 4) + col(x-1, y, 3),
            12: row(x-1, y+1, 4) + col(x-1, y-2, 3)},
        3: {1: row(x-2, y-2, 4), 2: col(x+1, y-2, 4), 3: row(x-3, y-2, 5) + col(x+1, y-1, 4), 4: row(x-2, y+1, 4),
            6: row(x-3, y+1, 5) + col(x+1, y-3, 4), 8: col(x-2, y-2, 4), 9: row(x-2, y-2, 5) + col(x-2, y-1, 4),
            12: row(x-2, y+1, 5) + col(x-2, y-3, 4)},
    }[cls]
    off = 1 if cls < 3 else 2
    return table.get(code, rect(x - off, y - off, n, n))


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--expected', type=Path, help='write frozen expected-ROUTE-02.2.json')
    ap.add_argument('--no-observers', action='store_true', help='control: no code/write hooks; skip Part D')
    args = ap.parse_args()
    binary = args.binary.read_bytes()
    digest = hashlib.sha256(binary).hexdigest()
    if digest != SHA:
        ap.error('unsupported binary; requires game.dll 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', binary, opt + o)[0] for o in (28, 56))
    m = Uc(UC_ARCH_X86, UC_MODE_32)
    m.mem_map(base, (size + 4095) & ~4095)
    m.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        sec = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
        va, cnt, off = struct.unpack_from('<III', binary, sec + 12)
        if cnt:
            m.mem_write(base + va, binary[off:off + cnt])
    SYS = 0x10000000
    m.mem_map(SYS, 0x20000)
    m.mem_map(0x20000000, 0x10000)
    m.mem_map(0, 0x1000)  # FS:[0] SEH chain (FS base 0 in Unicorn)
    stack, stop = 0x20008000, 0x30000000
    W, H = 32, 32
    fine, grid, cells, links, entries = SYS + 0x1000, SYS + 0x1200, SYS + 0x2000, SYS + 0x3000, SYS + 0x5000
    objects = [SYS + 0x6000 + n * 0x80 for n in range(96)]
    movers = [SYS + 0xA000 + n * 0x100 for n in range(48)]
    paths = [SYS + 0xD000 + n * 0x100 for n in range(48)]
    groups = [SYS + 0x10000 + n * 0x100 for n in range(4)]
    registry, slots = SYS + 0x10800, SYS + 0x10900
    points, src, dst, out = SYS + 0x11000, SYS + 0x11100, SYS + 0x11110, SYS + 0x11120
    self_mover, self_path, self_obj = movers[47], paths[47], objects[95]

    def w(a, *v):
        m.mem_write(a, struct.pack('<' + 'I' * len(v), *(x & 0xffffffff for x in v)))

    def r(a, n=1):
        return list(struct.unpack('<' + 'I' * n, m.mem_read(a, 4 * n)))

    def f(a, *v):
        m.mem_write(a, struct.pack('<' + 'f' * len(v), *v))

    def run(entry, this, *stackargs):
        w(stack, stop, *stackargs)
        m.reg_write(UC_X86_REG_ESP, stack)
        m.reg_write(UC_X86_REG_ECX, this)
        m.emu_start(entry, stop, count=5000000)
        assert m.reg_read(UC_X86_REG_EIP) == stop
        assert m.reg_read(UC_X86_REG_ESP) == stack + 4 + 4 * len(stackargs), hex(entry)  # RET n check
        return m.reg_read(UC_X86_REG_EAX)

    m.mem_write(0x6fd3c740, struct.pack('<4f', -1, 0, 1, 2))
    w(0x6fd68610, registry)

    def reset_world(mask=0x02000001):
        m.mem_write(SYS, bytes(0x20000))
        w(fine + 0x1c, grid)
        w(fine + 0xb8, entries)          # candidate table data   (fine+ac +0c)
        w(fine + 0xc0, 0, 32, 0)          # growth 0, capacity 32, count 0 (6f147340 constructor values)
        w(grid + 0x28, cells)
        w(grid + 0x3c, W, H)
        w(grid + 0x78, links)
        w(grid + 0xb4, 1000)
        m.mem_write(cells, struct.pack('<I', 0xffffff) * (W * H))
        w(registry + 0xc, slots)
        w(registry + 0x1c, 64)
        for i, obj in enumerate(movers + groups):
            w(slots + i * 8, -2, obj)
            w(obj + 0x14, i, 1000 + i)
        for i, mv in enumerate(movers):
            w(mv + 0x10, TAG)
            w(mv + 0x9c, 48 + 1, 1000 + 48 + 1)  # peers: group 1
            w(mv + 0xa8, paths[i])
            w(paths[i] + 0xa8, -1, -1)
            w(paths[i] + 0x88, 0)
        w(self_mover + 0x9c, 48, 1000 + 48)       # self: group 0
        f(self_mover + 0x80, 1.0, 0.0)
        w(self_path + 0x9c, mask, self_obj)
        w(self_path + 0x88, 0)
        w(self_obj + 0x30, self_mover, 0x01000001, 0)
        w(0x6fd53a84, fine)
        w(0x6fd53a88, grid)
        w(0x6fd53a8c, self_mover)

    link_count = [0]

    def link_cell(cell, chain):
        """chain: list of (kind, objptr) in stored order."""
        x, y = cell
        if not chain:
            return
        first = link_count[0]
        for k, (kind, obj) in enumerate(chain):
            idx = first + k
            nxt = idx + 1 if k + 1 < len(chain) else 0xffffff
            w(links + idx * 8, kind << 24 | nxt, obj)
        link_count[0] += len(chain)
        old = r(cells + (y * W + x) * 4)[0]
        w(cells + (y * W + x) * 4, (old & 0xff000000) | first)

    def obj_init(obj, payload, cat=0x01000001, stamp=0, flags=0):
        w(obj + 0x30, payload, cat, stamp)
        w(obj + 0x40, flags)

    visits = []
    hv = None if args.no_observers else m.hook_add(UC_HOOK_CODE, lambda uc, a, s, d: visits.append(
        tuple(struct.unpack('<2i', uc.mem_read(uc.reg_read(UC_X86_REG_ESP) + 4, 8)))), begin=0x6f148ad0, end=0x6f148ad0)
    m.ctl_flush_tb()

    def step_endpoints(code):
        sx = 1 if code & 2 else -1 if code & 8 else 0
        sy = 1 if code & 4 else -1 if code & 1 else 0
        if code == 0:
            return (16.25, 16.25), (16.5, 16.5)
        return (16.5, 16.5), (16.5 + 3 * sx, 16.5 + 3 * sy)

    # ---------------- Part A: strip order + capped vector -----------------
    partA = []
    for cls, code, fill in itertools.product(range(4), CODES, ['under', 'over']):
        reset_world(); link_count[0] = 0
        w(0x6fd53a80, cls)
        s, d = step_endpoints(code)
        x, y = 17 if code & 2 else 15 if code & 8 else 16, 17 if code & 4 else 15 if code & 1 else 16
        cellsv = strip_model(cls, x, y, code)
        # Population: shared object S at the head of every visited cell (per-cell
        # stamps -> one token per cell); per-cell distinct movers; one cell blocked
        # by terrain (null token, chain unread); special records in the first cell.
        per = 1 if fill == 'under' else -(-40 // len(cellsv))
        shared = objects[0]
        obj_init(shared, movers[0])
        nxt = 1
        terrain_cell = cellsv[len(cellsv) // 2] if len(cellsv) > 1 else None
        expected_chain = {}
        for ci, cell in enumerate(cellsv):
            chain = [(1, shared)]
            for k in range(per):
                o = objects[nxt]; mv = movers[1 + (nxt - 1) % 46]
                obj_init(o, mv)
                chain.append((1, o)); nxt += 1
            if ci == 0:
                specials = []
                for kind, payload, cat, stamp, flg in [
                        (2, movers[1], 0x01000001, 0, 0),           # kind2 metadata: skipped (still not stamped)
                        (1, movers[2], 0x00000001, 0, 0),           # inactive (+34 bit24 clear): skipped
                        (1, movers[3], 0x01000001, 0xffffffff, 0),  # dead stamp: skipped
                        (1, movers[4], 0x01000002, 0, 0),           # mask mismatch: skipped
                        (1, movers[5], 0x01000001, 0, 1),           # suppressed counter: skipped
                        (1, movers[6], 0x01000001, 0, 0x80000000),  # suppressed bit31: skipped
                        (1, movers[7], 0x01000001, 0, 0x20000000),  # moving flag: COLLECTED
                        (1, movers[8], 0x01000001, 0, 0x10000000),  # bit28: collected
                        (1, objects[90], 0x01000001, 0, 0),         # non-mover payload: null token
                        (1, 0, 0x01000001, 0, 0)]:                  # null payload: null token
                    o = objects[nxt]; nxt += 1
                    obj_init(o, payload, cat, stamp, flg)
                    specials.append((kind, o))
                chain = chain[:1] + specials + [(1, chain[1][1])] + [(1, chain[1][1])] + chain[2:]  # duplicate within one cell
            if 0 <= cell[0] < W and 0 <= cell[1] < H:
                link_cell(cell, chain)
            expected_chain[cell] = chain
        if terrain_cell and 0 <= terrain_cell[0] < W:
            v = r(cells + (terrain_cell[1] * W + terrain_cell[0]) * 4)[0]
            w(cells + (terrain_cell[1] * W + terrain_cell[0]) * 4, 0x02000000 | (v & 0xffffff))
        # independent model
        stamps = {o: r(o + 0x38)[0] for o in objects}
        counter = 1000
        model = []
        for cell in cellsv:
            inb = 0 <= cell[0] < W and 0 <= cell[1] < H
            if not inb or cell == terrain_cell:
                if len(model) < 32:
                    model.append(0)
                continue
            chain = expected_chain[cell]
            counter += 1
            for kind, o in chain:
                cat = r(o + 0x34)[0]; flg = r(o + 0x40)[0]; pay = r(o + 0x30)[0]
                if kind == 2:
                    continue
                if stamps[o] == 0xffffffff or not cat & 0x01000000 or stamps[o] == counter:
                    continue
                stamps[o] = counter
                if kind == 1 and not flg & 0x8fffffff and cat & 0x01000001 & 0xffffff and len(model) < 32:
                    model.append(pay if pay and r(pay + 0x10)[0] == TAG else 0)
        # stale previous vector must be cleared by 6f166140
        w(fine + 0xc8, 5); w(entries, *[0xdead0000 + i for i in range(5)])
        f(src, *s); f(dst, *d)
        visits.clear()
        res = run(0x6f166140, self_path, src, dst) & 0xff
        count = r(fine + 0xc8)[0]
        vec = r(entries, count) if count else []
        if args.no_observers:
            visits[:] = cellsv
        assert visits == cellsv, (cls, code, visits, cellsv)
        assert count == len(model) and vec == model, (cls, code, fill, count, len(model))
        assert res == int(count == 0)
        assert r(grid + 0xb4)[0] == counter
        assert {o: r(o + 0x38)[0] for o in objects} == stamps
        assert r(self_obj + 0x40)[0] == 0 and r(0)[0] == 0 and r(fine + 0xa4)[0] == 0x02000001
        assert r(fine + 0xc4)[0] == 32 and r(fine + 0xc0)[0] == 0
        idx = {mv: i for i, mv in enumerate(movers)}
        partA.append(dict(cls=cls, code=code, fill=fill, source=list(s), destination=list(d),
                          visited=[list(c) for c in visits], terrain_cell=list(terrain_cell) if terrain_cell else None,
                          count=count, truncated=count == 32 and fill == 'over',
                          tokens=[('M%d' % idx[v]) if v else 'null' for v in vec], result=res,
                          stamp_counter=counter))

    # ---------------- Part B: resolver consequences of capped order -------
    partB = []

    def resolver_case(name, peers, self_speed=1.0):
        """peers: dict chain-position -> (speed, group, cls, preblocked). All
        other positions hold stationary movers. Class0, x+ step, one cell."""
        reset_world(); link_count[0] = 0
        w(0x6fd53a80, 0)
        f(self_mover + 0x80, self_speed, 0.0)
        chain = []
        for pos in range(40):
            o = objects[1 + pos]; mv = movers[pos]
            obj_init(o, mv)
            f(mv + 0x80, 0.0, 0.0)
            chain.append((1, o))
            if pos in peers:
                sp, grp, pcls, pre = peers[pos]
                f(mv + 0x80, sp, 0.0)
                w(mv + 0x9c, 48 + grp, 1000 + 48 + grp)
                w(paths[pos] + 0x88, pcls << 16)
                if pre:
                    w(paths[pos] + 0xa8, 46, 1000 + 46)  # already waiting on a live mover
        link_cell((17, 16), chain)
        w(self_path + 0x94, 0)
        f(src, 16.5, 16.5); f(dst, 19.5, 16.5)
        res = run(0x6f166140, self_path, src, dst) & 0xff
        count = r(fine + 0xc8)[0]
        state = dict(name=name, result=res, count=count,
                     self_blocker=r(self_path + 0xa8, 2), self_delay=r(self_path + 0x94)[0],
                     peers={str(p): dict(blocker=r(paths[p] + 0xa8, 2), delay=r(paths[p] + 0x94)[0]) for p in sorted(peers)})
        partB.append(state)
        return state

    SELF_ID = [47, 1047]
    NONE = [0xffffffff, 0xffffffff]
    for p in [0, 15, 31, 32, 39]:
        st = resolver_case('fast_peer_at_%d' % p, {p: (3.0, 1, 0, False)})
        kept = p < 32
        assert st['count'] == 32 and st['result'] == 0
        assert st['self_blocker'] == ([p, 1000 + p] if kept else NONE)
        assert st['self_delay'] == (4 if kept else 0)
        assert st['peers'][str(p)] == dict(blocker=NONE, delay=0)
    for a, b in [(5, 20), (20, 5), (5, 32), (32, 5), (31, 32)]:
        st = resolver_case('slow_%d_fast_%d' % (a, b), {a: (0.5, 1, 0, False), b: (3.0, 1, 0, False)})
        slow_visited = a < 32 and (b >= 32 or a < b)
        fast_kept = b < 32
        assert st['self_blocker'] == ([b, 1000 + b] if fast_kept else NONE), st
        assert st['self_delay'] == (4 if fast_kept else 0)
        assert st['peers'][str(a)] == (dict(blocker=SELF_ID, delay=20) if slow_visited else dict(blocker=NONE, delay=0)), st
    st = resolver_case('forty_slow_peers', {p: (0.5, 1, 0, False) for p in range(40)})
    for p in range(40):
        assert st['peers'][str(p)] == (dict(blocker=SELF_ID, delay=20) if p < 32 else dict(blocker=NONE, delay=0))
    assert st['self_blocker'] == NONE and st['self_delay'] == 0 and st['result'] == 0
    st = resolver_case('preblocked_fast_then_fast', {3: (3.0, 1, 0, True), 9: (3.0, 1, 0, False)})
    assert st['self_blocker'] == [9, 1009] and st['peers']['3'] == dict(blocker=[46, 1046], delay=0)
    st = resolver_case('class_mismatch_slow', {4: (0.5, 1, 2, False)})
    assert st['self_blocker'] == [4, 1004] and st['self_delay'] == 4
    st = resolver_case('same_group_slow', {4: (0.5, 0, 0, False)})
    assert st['self_blocker'] == [4, 1004] and st['self_delay'] == 4

    # ---------------- Part C: waypoint selection + obstruction changes ----
    partC = []
    route = [(20.25 - 2 * n, 16.75) for n in range(5)]  # index0 farthest; index4 == source

    def select(cls, d4=0, edits=()):
        w(0x6fd53a80, cls)
        w(fine + 0xd4, d4)
        w(self_path + 0x40, points); w(self_path + 0x74, 4)
        m.mem_write(points, b''.join(struct.pack('<2f', *p) for p in route))
        f(src, 12.25, 16.75)
        idx = run(0x6f167bf0, self_path, src)
        assert r(self_path + 0x74)[0] == 4 and r(self_obj + 0x40)[0] == 0 and r(0)[0] == 0
        run(0x6f165e60, self_path, src, out)
        assert r(self_path + 0x74)[0] == idx and r(out, 2) == list(struct.unpack('<2I', struct.pack('<2f', *route[idx])))
        return idx

    variants = [('none', None), ('static', 0), ('moving20', 0x20000000), ('moving40', 0x40000000),
                ('bit28', 0x10000000), ('counter1', 1), ('bit31', 0x80000000), ('self', 'self'),
                ('mask_mismatch', 'mask'), ('inactive', 'inactive'), ('dead', 'dead'), ('kind2', 'kind2')]
    # Obstruction cell (17,16): sampled by segments to index1 (18.25) and index0, not index2 (16.25).
    for cls, (vname, vflag), d4, cell in itertools.product(range(4), variants, [0, 1], [(17, 16), (19, 16), (15, 16)]):
        reset_world(); link_count[0] = 0
        if vflag is not None:
            o = self_obj if vflag == 'self' else objects[10]
            if vflag != 'self':
                obj_init(o, movers[10], 0x01000002 if vflag == 'mask' else 0x00000001 if vflag == 'inactive' else 0x01000001,
                         0xffffffff if vflag == 'dead' else 0, vflag if isinstance(vflag, int) else 0)
            link_cell(cell, [(2 if vflag == 'kind2' else 1, o)])
        idx = select(cls, d4)
        partC.append(dict(cls=cls, variant=vname, d4=d4, cell=list(cell), selected=idx))
    # Sequence of changes between successive selector calls (same pose/route).
    sequences = []
    for cls in range(4):
        reset_world(); link_count[0] = 0
        seq = []
        seq.append(('clear', select(cls)))
        obj_init(objects[10], movers[10]); link_cell((17, 16), [(1, objects[10])])
        seq.append(('insert_static_17_16', select(cls)))
        w(objects[10] + 0x40, 0x20000000)
        seq.append(('flag_moving', select(cls)))
        seq.append(('flag_moving_d4_1', select(cls, d4=1)))
        w(objects[10] + 0x40, 0)
        seq.append(('flag_cleared', select(cls)))
        w(cells + (16 * W + 17) * 4, 0xffffff)
        seq.append(('unlinked', select(cls)))
        w(cells + (16 * W + 13) * 4, 0x02ffffff)
        seq.append(('terrain_13_16', select(cls)))
        sequences.append(dict(cls=cls, steps=[dict(state=a, selected=b) for a, b in seq]))

    # ---------------- Part D: write audit ---------------------------------
    writes = []
    hw = None if args.no_observers else m.hook_add(UC_HOOK_MEM_WRITE, lambda uc, acc, a, sz, val, d: writes.append((a, sz)))

    def classify(a):
        if 0x20000000 <= a < 0x20010000: return 'stack'
        if a < 0x1000: return 'fs0_seh'
        if a == fine + 0xa4: return 'fine+a4 query mask'
        if fine <= a < fine + 0x100: return 'fine+%x' % (a - fine)
        if grid <= a < grid + 0x100: return 'grid+%x' % (a - grid)
        if entries <= a < entries + 0x100: return 'candidate entries'
        for o in objects:
            if o <= a < o + 0x80: return ('self' if o == self_obj else 'object') + '+%x' % (a - o)
        for i, p in enumerate(paths):
            if p <= a < p + 0x100: return ('self_path' if p == self_path else 'peer_path') + '+%x' % (a - p)
        if cells <= a < cells + W * H * 4: return 'CELLS'
        if links <= a < links + 0x2000: return 'LINKS'
        if out <= a < out + 8: return 'output'
        return hex(a)
    audit = {}
    for name, entry in ([] if args.no_observers else [('select_167bf0', 0x6f167bf0), ('commit_165e60', 0x6f165e60), ('collect_166140', 0x6f166140)]):
        reset_world(); link_count[0] = 0
        w(0x6fd53a80, 2)
        for k in range(6):
            obj_init(objects[20 + k], movers[20 + k], flags=[0, 0x20000000, 1, 0, 0x40000000, 0][k])
            f(movers[20 + k] + 0x80, 0.5 * k, 0)
            link_cell((13 + k, 16), [(1, objects[20 + k]), (1, self_obj)])
        w(self_path + 0x40, points); w(self_path + 0x74, 4)
        m.mem_write(points, b''.join(struct.pack('<2f', *p) for p in route))
        f(src, 12.25, 16.75); f(dst, 20.25, 16.75)
        writes.clear()
        if entry == 0x6f166140: run(entry, self_path, src, dst)
        elif entry == 0x6f165e60: run(entry, self_path, src, out)
        else: run(entry, self_path, src)
        kinds = sorted({classify(a) for a, s in writes} - {'stack'})
        audit[name] = kinds
        assert not any(k in ('CELLS', 'LINKS') or k.endswith('+34') or k.endswith('+30') or k == 'object+40' for k in kinds), kinds
    if hw is not None:
        m.hook_del(hw)

    # ---------------- Part E: selection vs collection asymmetry -----------
    partE = []
    for cls, flg, cell in itertools.product(range(4), [0, 0x20000000], [(13, 16), (14, 16)]):
        reset_world(); link_count[0] = 0
        obj_init(objects[10], movers[10], flags=flg)
        f(movers[10] + 0x80, 3.0, 0.0)
        link_cell(cell, [(1, objects[10])])
        idx = select(cls)
        f(dst, *route[idx])
        res = run(0x6f166140, self_path, src, dst) & 0xff
        partE.append(dict(cls=cls, flags=flg, cell=list(cell), selected=idx, collect_result=res, count=r(fine + 0xc8)[0],
                          self_blocker=r(self_path + 0xa8, 2), self_delay=r(self_path + 0x94)[0]))
    # ---------------- Part F: path/search target identity is not exempt ---
    # 6f167bf0/6f166140 increment only path+a0 (self); path+a4 target and the
    # fine-system target fine+a8 left by the last search are not suppressed.
    partF = []
    for cls, where in itertools.product(range(4), ['segment_17_16', 'strip_14_16']):
        reset_world(); link_count[0] = 0
        tgt = objects[10]
        obj_init(tgt, movers[10]); f(movers[10] + 0x80, 3.0, 0.0)
        w(self_path + 0xa4, tgt); w(fine + 0xa8, tgt)
        link_cell((17, 16) if where.startswith('segment') else (14, 16), [(1, tgt)])
        idx = select(cls)
        seen = r(fine + 0xcc)[0]
        f(dst, *route[0])
        res = run(0x6f166140, self_path, src, dst) & 0xff
        partF.append(dict(cls=cls, where=where, selected=idx, fine_cc_target_seen=seen, collect_result=res,
                          count=r(fine + 0xc8)[0], tokens=r(entries, r(fine + 0xc8)[0]) == [movers[10]] * r(fine + 0xc8)[0],
                          self_blocker=r(self_path + 0xa8, 2), target_counter=r(tgt + 0x40)[0]))
    if hv is not None:
        m.hook_del(hv)

    expected = dict(task='ROUTE-02.2', binary_sha256=SHA, map=[W, H], query_mask=0x02000001,
                    candidate_table=dict(growth=0, capacity=32, append_admission='count<32 before 6f148550'),
                    partA=partA, partB=partB, partC=partC, sequences=sequences, write_audit=audit, partE=partE, partF=partF)
    blob = json.dumps(expected, sort_keys=True, separators=(',', ':')).encode()
    if args.expected:
        args.expected.write_bytes(blob + b'\n')
    report = dict(binary_sha256=SHA, passed=True, partA_cases=len(partA), partA_truncated=sum(c['truncated'] for c in partA),
                  partB_cases=len(partB), partC_cases=len(partC), sequences=len(sequences), partE_cases=len(partE),
                  write_audit=audit, expected_sha256=hashlib.sha256(blob + b'\n').hexdigest(),
                  partC_summary=sorted({(c['variant'], c['d4'], tuple(c['cell']), c['selected']) for c in partC if c['cls'] == 0}),
                  sequences_summary=sequences, partE=partE, partF=partF)
    args.report.write_text(json.dumps(report, indent=1, default=list) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k not in ('partC_summary',)}, indent=1, default=list))


if __name__ == '__main__':
    main()
