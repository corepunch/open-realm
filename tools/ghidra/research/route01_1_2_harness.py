#!/usr/bin/env python3
"""Shared original-code harness for the ROUTE-01.1 / ROUTE-01.2 research handoffs.

Unchanged retail x86 (game.dll 1.27.1.7085) runs under Unicorn for every
pathing operation: fine-system constructor 147600, CLrPath constructor 1657c0
(its two route tables: capacity 0, growth 0x80), terrain setter 04d870->054000,
hierarchy producer 15d360, fine request 166e90->148100->147dc0, adaptive
request 166c30->162cb0->162a30, public advance 165ae0 and its callees.

Supplied preconditions (explicit, not producers):
  * the three external Storm imports reached through thunks 6f07c6d2 (alloc,
    ordinal401), 6f07c6d8 (realloc, ordinal405) and 6f07c678 (free, ordinal403)
    receive host storage (same convention as verify_wc3_pathing_storage.py);
  * the four runtime BSS float constants used by every accepted oracle
    (6fd3c740=-1, 6fd3c744=0, 6fd3c748=1, 6fd53a74=-128000.0078125);
  * owner/adaptive/fine map headers with the constructor's padded hierarchy
    dimensions (as acc_research_harness.py / verify_wc3_pathing_adaptive.py),
    preallocated adaptive node/heap storage, scheduler bucket words, a
    registry and mover record sufficient for 165ae0's context install.
No retail instruction is replaced or skipped.
"""
import hashlib
import struct
from pathlib import Path

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
CONSTANTS = {0x6fd3c740: -1.0, 0x6fd3c744: 0.0, 0x6fd3c748: 1.0, 0x6fd53a74: -128000.0078125}
STORM = {0x6f07c6d2: 4, 0x6f07c6d8: 5, 0x6f07c678: 4}
FINE_BUCKET = 0x6fd53a90 + 0x54
ACC_BUCKET = 0x6fd53a90 + 0x38


def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


def fw(v):
    return struct.unpack('<I', struct.pack('<f', v))[0]


def wf(w):
    return struct.unpack('<f', struct.pack('<I', w & 0xffffffff))[0]


def sha256_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class Retail:
    def __init__(self, binary_path, fine_side=64):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
        from unicorn import x86_const as R
        self.R, self.UC_HOOK_CODE = R, UC_HOOK_CODE
        binary = Path(binary_path).read_bytes()
        self.digest = hashlib.sha256(binary).hexdigest()
        if self.digest != SHA:
            raise SystemExit('unsupported binary; requires game.dll 1.27.1.7085')
        pe = struct.unpack_from('<I', binary, 0x3c)[0]
        opt = pe + 24
        base, size = (struct.unpack_from('<I', binary, opt + o)[0] for o in (28, 56))
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(base, (size + 4095) & ~4095)
        uc.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
        for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
            section = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
            va, count, offset = struct.unpack_from('<III', binary, section + 12)
            if count:
                uc.mem_write(base + va, binary[offset:offset + count])
        uc.mem_map(0, 0x1000)               # fs:[0] ExceptionList slot (fs base 0)
        uc.mem_map(0x10000000, 0x4000000)
        uc.mem_map(0x20000000, 0x10000)
        uc.mem_map(0x40000000, 0x8000000)   # Storm host arena
        self.stack, self.stop = 0x20008000, 0x30000000
        for address, value in CONSTANTS.items():
            uc.mem_write(address, struct.pack('<f', value))
        self.allocations, self.storm_log, self._block = {}, [], 0x40000000
        for address in STORM:
            uc.hook_add(UC_HOOK_CODE, self._storm, begin=address, end=address)
        self._next = 0x10100000
        self.fine_side = fine_side
        (self.owner, self.system, self.fine_system, self.fine, self.game,
         self.path, self.mover, self.group, self.registry, self.slots,
         self.candidates) = (self.alloc(0x400) for _ in range(11))
        self.points = self.alloc(0x100)
        self.cells = self.alloc(fine_side * fine_side * 4)
        self.bitmap = self.alloc(max(0x1000, fine_side * fine_side // 8))
        self.sides = [((fine_side + 16) // 2 + 1) >> level for level in range(4)]
        self.maps = [self.alloc(0x100) for _ in range(4)]
        self.data = [self.alloc(side * side * 8) for side in self.sides]
        self.acc_nodes = self.alloc(65536 * 36)
        self.acc_heap = self.alloc(65536 * 4 * 12)
        w = self.write
        w(0x6fd53a48, self.owner)
        w(0x6fd3c82c, self.game)
        uc.mem_write(self.game + 0x6c, struct.pack('<ff', 0, 0))
        # Original fine-system constructor; the map header is supplied.
        self.run(0x6f147600, self.fine_system)
        w(self.fine_system + 0x1c, self.fine)
        w(self.fine + 0x28, self.cells)
        w(self.fine + 0x38, fine_side * fine_side, fine_side, fine_side)
        w(self.fine + 0x54, 0, 0, fine_side, fine_side)
        w(self.fine + 0x98, self.bitmap)
        w(self.fine + 0xac, 0xffffff)
        w(self.fine + 0xa8, 2048)
        w(self.fine + 0x6c, 0, -1, 0, -1, 0, 1024, 0, 0)  # empty cell-link collection
        w(self.owner + 0x24c, self.fine_system)
        w(self.owner + 0x23c, *self.maps)
        w(self.owner + 0x250, self.system)
        w(self.owner + 0x538, 100)
        for level, (tilemap, storage, side) in enumerate(zip(self.maps, self.data, self.sides)):
            w(tilemap + 0x28, storage)
            w(tilemap + 0x3c, side, side)
            uc.mem_write(tilemap + 0x64, struct.pack('<ff', 2 << level, 1 / (2 << level)))
            w(self.system + 0x1c + level * 4, tilemap)
        w(self.system + 0x5c, self.acc_nodes)
        w(self.system + 0x68, 65536, 0)
        w(self.system + 0x7c, self.acc_heap)
        w(self.system + 0x88, 65536 * 4, 0)
        w(self.system + 0xac + 0xc, self.candidates)  # next-step candidate list used by 166140
        w(self.system + 0xac + 0x18, 64, 0)
        w(self.cells, *([0xffffff] * fine_side * fine_side))
        # Registry/mover/group for 165ae0's context install (refill-oracle convention).
        w(0x6fd68610, self.registry)
        w(self.registry + 0xc, self.slots)
        w(self.registry + 0x1c, 2)
        for n, item in enumerate([self.mover, self.group]):
            w(self.slots + n * 8, -2, item)
            w(item + 0x14, n, 100 + n)
        w(self.mover + 0x9c, 1, 101)
        w(self.mover + 0xa8, self.path)
        w(self.group + 0x38, 1)
        uc.mem_write(0x6fd54190, struct.pack('<f', 144))

    # -- external Storm imports (host storage only) --------------------------------
    def _storm(self, uc, address, size, user):
        R = self.R
        sp = uc.reg_read(R.UC_X86_REG_ESP)
        count = STORM[address]
        argv = self.read(sp + 4, count)
        if address == 0x6f07c678:
            self.allocations.pop(argv[0], None)
            result = 1
            self.storm_log.append(dict(op='free', block=argv[0]))
        else:
            old = argv[0] if address == 0x6f07c6d8 else None
            wanted = argv[1] if old is not None else argv[0]
            result = self._block
            self._block += (wanted + 4095) & ~4095 or 4096
            if self._block > 0x48000000:
                raise RuntimeError('Storm host arena exhausted')
            if old is not None:
                prior = self.allocations.pop(old, 0)
                uc.mem_write(result, bytes(uc.mem_read(old, min(prior, wanted))))
            self.allocations[result] = wanted
            self.storm_log.append(dict(op='realloc' if old is not None else 'alloc', bytes=wanted,
                                       caller=self.read(sp)[0]))
        uc.reg_write(R.UC_X86_REG_EAX, result)
        uc.reg_write(R.UC_X86_REG_ESP, sp + 4 + 4 * count)
        uc.reg_write(R.UC_X86_REG_EIP, self.read(sp)[0])

    # -- memory --------------------------------------------------------------------
    def alloc(self, size):
        address = self._next
        self._next = (self._next + size + 0xfff) & ~0xfff
        if self._next > 0x13f00000:
            raise RuntimeError('harness arena exhausted')
        return address

    def write(self, address, *values):
        self.uc.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def read(self, address, count=1):
        return list(struct.unpack('<' + 'I' * count, self.uc.mem_read(address, count * 4)))

    def run(self, entry, ecx, *arguments, edx=None, budget=400000000):
        R = self.R
        self.write(self.stack, self.stop, *arguments)
        self.uc.reg_write(R.UC_X86_REG_ESP, self.stack)
        self.uc.reg_write(R.UC_X86_REG_ECX, ecx)
        if edx is not None:
            self.uc.reg_write(R.UC_X86_REG_EDX, edx)
        self.uc.emu_start(entry, self.stop, count=budget)
        if self.uc.reg_read(R.UC_X86_REG_EIP) != self.stop:
            raise RuntimeError(f'retail call {entry:#x} exceeded instruction budget')
        esp = self.uc.reg_read(R.UC_X86_REG_ESP)
        return self.uc.reg_read(R.UC_X86_REG_EAX), esp - (self.stack + 4)

    def hook(self, address, fn):
        h = self.uc.hook_add(self.UC_HOOK_CODE, fn, begin=address, end=address)
        self.uc.ctl_flush_tb()
        return h

    def unhook(self, h):
        self.uc.hook_del(h)

    # -- producers --------------------------------------------------------------------
    def terrain(self, fx, fy, mask, blocked):
        """Original 04d870(ECX=&X, EDX=&Y, mask, blocked) at a fine-cell centre (origin 0)."""
        self.uc.mem_write(self.points, struct.pack('<ff', (fx + .5) * 32, (fy + .5) * 32))
        self.run(0x6f04d870, self.points, mask, blocked, edx=self.points + 4)

    def block_fine(self, cells, masks=(2, 4, 0x40, 0x80)):
        for fx, fy in cells:
            for mask in masks:
                self.terrain(fx, fy, mask, 1)

    def clear_map(self):
        self.write(self.cells, *([0xffffff] * self.fine_side * self.fine_side))

    def rebuild(self):
        return self.run(0x6f15d360, self.owner, 0, 0)

    # -- path object --------------------------------------------------------------------
    def construct_path(self):
        """Original CLrPath constructor 1657c0 (fastcall ECX)."""
        self.uc.mem_write(self.path, bytes(0x100))
        self.run(0x6f1657c0, self.path)

    def table(self, offset):
        """(data, growth, capacity, count) of route table path+offset (0x34 fine, 0x54 coarse)."""
        t = self.read(self.path + offset, 8)
        return dict(data=t[3], growth=t[5], capacity=t[6], count=t[7])

    def words(self, offset):
        t = self.table(offset)
        return self.read(t['data'], 2 * t['count']) if t['count'] else []

    def set_buckets(self, fine_work=0, acc_work=0, fine_limit=1100, acc_limit=900):
        self.write(FINE_BUCKET, 700 | 1 << 16, fine_limit, fine_work, 0, 0, 0, 0)
        self.write(ACC_BUCKET, 400 | 2 << 16, acc_limit, acc_work, 0, 0, 0, 0)

    # -- node readers --------------------------------------------------------------------
    def fine_chain(self, node):
        base = self.read(self.fine_system + 0x30)[0]
        out = []
        while node != 0xffffffff:
            x, y = self.read(base + 36 * node, 2)
            out.append((x, y))
            node = self.read(base + 36 * node + 0x1c)[0]
            if len(out) > 70000:
                raise RuntimeError('fine parent cycle')
        return out

    def acc_chain(self, node):
        out = []
        while node != 0xffffffff:
            x, y = self.read(self.acc_nodes + 36 * node, 2)
            meta = self.uc.mem_read(self.acc_nodes + 36 * node + 0x20, 4)
            out.append((x, y, meta[2], meta[3]))
            node = self.read(self.acc_nodes + 36 * node + 0x1c)[0]
            if len(out) > 70000:
                raise RuntimeError('adaptive parent cycle')
        return out


# -- independent reconstruction models (float32, no retail code) -----------------------
def model_fine(chain, source, goal):
    pts = [(f32(x + .5), f32(y + .5)) for x, y in chain]
    pts[-1] = tuple(source)
    import math
    if (math.floor(pts[0][0]), math.floor(pts[0][1])) == (math.floor(goal[0]), math.floor(goal[1])):
        pts[0] = tuple(goal)
    return [fw(v) for p in pts for v in p]


def model_acc(chain, size, source, goal):
    offset = 1.25 if size == 2 else .75
    pts = []
    for x, y, level, tag in chain:
        if size == 2 and level:
            edge = lambda v: ((v >> level) << level) + (1 << level) - 1
            if x == edge(x):
                x -= 1
            if y == edge(y):
                y -= 1
        pts.append((f32(x + offset), f32(y + offset)))
        if tag:
            pts.append((CONSTANTS[0x6fd53a74], float(tag)))
    pts[-1] = tuple(source)
    pts[0] = tuple(goal)
    return [fw(v) for p in pts for v in p]
