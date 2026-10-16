#!/usr/bin/env python3
"""Shared original-code harness for the ACC-01.1/01.2/02.2/04.1 research handoffs.

Everything that changes adaptive state runs unchanged retail x86 under Unicorn:
terrain setter 04d870->054000, full/rectangle hierarchy producer 15d360,
Way Gate source publication 04e360->15c000->15bf60->15c030, record producers
04e210 (active bit) and 04e550 (destination), and full adaptive request 162cb0.
Supplied preconditions (explicit, not producers): empty fine storage
(0xffffff low link word, zero flags), map/owner/search headers with the
constructor's padded dimensions, preallocated node/heap/route storage, the
four runtime float constants already used by the accepted oracles.

Coverage is recorded at conditional-jump granularity with a UC_HOOK_BLOCK
over the adaptive search range: when the previous executed block ended with a
Jcc (from the read-only Ghidra disassembly cache), the following block start is
classified as the taken target or the fall-through. This is observation only;
it never alters registers or memory.
"""
import hashlib
import json
import struct
from pathlib import Path

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
GHIDRA_CACHE = Path('/GitHub/wc3-analysis/reports/pathfinding-1.27/research/_ghidra/cache')

# Adaptive search functions reached from 162cb0 (read-only Ghidra callee walk,
# research/ACC-01.1/callgraph-162cb0.json), excluding container/heap helpers.
SEARCH_FUNCTIONS = {
    '6f1625f0': 'PathAcc_FindCellNode', '6f163ef0': 'PathAcc_CreateCellNode',
    '6f164020': 'PathAcc_RelaxNode', '6f165220': 'PathAcc_RelaxSpecialEdge',
    '6f163f50': 'PathAcc_Search', '6f1643d0': 'PathAcc_ExpandBaseNode',
    '6f1644d0': 'PathAcc_ExpandCoarseNode', '6f163260': 'PathAcc_ExpandEastSide',
    '6f163bc0': 'PathAcc_ExpandNorthSide', '6f1648f0': 'PathAcc_ExpandSouthSide',
    '6f165090': 'PathAcc_ExpandWestSide', '6f163970': 'coarse NE corner',
    '6f164350': 'coarse SE corner', '6f164730': 'coarse SW corner', '6f163a80': 'coarse NW corner',
    '6f163ae0': 'base N cardinal', '6f163180': 'PathAcc_RelaxBaseEast', '6f164810': 'base S cardinal',
    '6f164f90': 'base W cardinal', '6f1638c0': 'base NE corner', '6f1642b0': 'base SE corner',
    '6f164670': 'base SW corner', '6f163a40': 'base NW corner', '6f1653a0': 'special edge lookup',
    '6f1631e0': 'size2 base east strip', '6f163370': 'PathAcc_Size2EastOccupancy',
    '6f1635b0': 'PathAcc_Size2EastBoundary', '6f163910': 'PathAcc_Size2BaseNorthEastRight',
    '6f1639e0': 'PathAcc_Size2CoarseNorthEastRight', '6f163b60': 'PathAcc_Size2BaseNorthRight',
    '6f163cd0': 'PathAcc_Size2NorthRight', '6f163d40': 'PathAcc_Size2BaseNorthPromotedOdd',
    '6f163dd0': 'PathAcc_Size2NorthBoundary', '6f164300': 'PathAcc_Size2BaseSouthEastDiagonal',
    '6f164870': 'PathAcc_Size2BaseSouthStrip', '6f164e50': 'PathAcc_Size2SouthBoundary',
    '6f1651a0': 'PathAcc_Size2WestDown', '6f1653e0': 'PathAcc_Size2BaseWestPromotedOdd',
    '6f165470': 'PathAcc_Size2WestBoundary', '6f162b80': 'PathAcc_TestBaseCell',
    '6f164c30': 'PathAcc_SetupSearch', '6f162cb0': 'PathAcc_BuildRoute', '6f164a20': 'search start',
    '6f1641d0': 'closed unlink', '6f162730': 'close node',
}


def load_jccs():
    """Return {jcc_va: (function, target, fallthrough, text)} from cached Ghidra disassembly."""
    out, ends = {}, {}
    for function in SEARCH_FUNCTIONS:
        path = GHIDRA_CACHE / f'{function}-disassemble_function.txt'
        rows = json.loads(path.read_text())['instructions']
        rows = sorted(rows, key=lambda r: int(r['address'], 16))
        for index, row in enumerate(rows):
            text = row['instruction']
            mnemonic = text.split()[0]
            address = int(row['address'], 16)
            if mnemonic.startswith('J') and mnemonic != 'JMP':
                target = int(text.split()[-1], 16)
                fall = int(rows[index + 1]['address'], 16)
                out[address] = (function, target, fall, text)
                ends[fall] = address
    return out


class Retail:
    """Original adaptive subsystem over supplied empty fine storage and padded headers."""

    def __init__(self, binary_path, fine_side=64, origin=(0.0, 0.0), node_capacity=65536):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
        from unicorn import x86_const as R
        self.R = R
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
        uc.mem_map(0x10000000, 0x4000000)
        uc.mem_map(0x20000000, 0x10000)
        self.stack, self.stop = 0x20008000, 0x30000000
        for address, value in {'6fd3c740': -1.0, '6fd3c744': 0.0, '6fd3c748': 1.0, '6fd53a74': -128000.0078125}.items():
            uc.mem_write(int(address, 16), struct.pack('<f', value))
        self._next = 0x10100000
        self.system, self.owner, self.fine_system, self.fine, self.game = (self.alloc(0x400) for _ in range(5))
        self.special = self.alloc(256 * 12)
        self.rect, self.points = self.alloc(0x40), self.alloc(0x40)
        self.route = self.alloc(0x40)
        self.fine_side = fine_side
        self.cells = self.alloc(fine_side * fine_side * 4)
        self.sides = [((fine_side + 16) // 2 + 1) >> level for level in range(4)]
        self.maps = [self.alloc(0x100) for _ in range(4)]
        self.data = [self.alloc(side * side * 8) for side in self.sides]
        self.nodes = self.alloc(node_capacity * 36)
        self.heap = self.alloc(node_capacity * 4 * 12)
        self.route_data = self.alloc(65536 * 8)
        self.node_capacity = node_capacity
        self.write(0x6fd53a48, self.owner)
        self.write(0x6fd3c82c, self.game)
        uc.mem_write(self.game + 0x6c, struct.pack('<ff', *origin))
        self.origin = origin
        self.write(self.owner + 0x24c, self.fine_system)
        self.write(self.fine_system + 0x1c, self.fine)
        self.write(self.owner + 0x23c, *self.maps)
        self.write(self.owner + 0x250, self.system)
        self.write(self.fine + 0x28, self.cells)
        self.write(self.fine + 0x3c, fine_side, fine_side)
        self.write(self.fine + 0x54, 0, 0, fine_side, fine_side)
        self.write(self.system + 0x3c, self.special)
        for level, (tilemap, storage, side) in enumerate(zip(self.maps, self.data, self.sides)):
            self.write(tilemap + 0x28, storage)
            self.write(tilemap + 0x3c, side, side)
            uc.mem_write(tilemap + 0x64, struct.pack('<ff', 2 << level, 1 / (2 << level)))
            self.write(self.system + 0x1c + level * 4, tilemap)
        self.write(self.system + 0x5c, self.nodes)
        self.write(self.system + 0x68, node_capacity, 0)
        self.write(self.system + 0x7c, self.heap)
        self.write(self.system + 0x88, node_capacity * 4, 0)
        self.write(self.route + 0xc, self.route_data)
        self.write(self.route + 0x18, 65536, 0)
        self.write(self.cells, *([0xffffff] * fine_side * fine_side))
        self.jccs = None
        self.coverage = None
        self.hooks = []

    # -- memory helpers -------------------------------------------------
    def alloc(self, size):
        address = self._next
        self._next = (self._next + size + 0xfff) & ~0xfff
        if self._next > 0x14000000:
            raise RuntimeError('harness arena exhausted')
        return address

    def write(self, address, *values):
        self.uc.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def read(self, address, count=1):
        return list(struct.unpack('<' + 'I' * count, self.uc.mem_read(address, count * 4)))

    def run(self, entry, ecx, *arguments, edx=None):
        R = self.R
        self.write(self.stack, self.stop, *arguments)
        self.uc.reg_write(R.UC_X86_REG_ESP, self.stack)
        self.uc.reg_write(R.UC_X86_REG_ECX, ecx)
        if edx is not None:
            self.uc.reg_write(R.UC_X86_REG_EDX, edx)
        self.uc.emu_start(entry, self.stop, count=200000000)
        if self.uc.reg_read(R.UC_X86_REG_EIP) != self.stop:
            raise RuntimeError(f'retail call {entry:#x} exceeded instruction budget')
        return self.uc.reg_read(R.UC_X86_REG_EAX)

    # -- original producers ----------------------------------------------
    def terrain(self, fx, fy, mask, blocked):
        """Original SetTerrainPathable core 04d870(ECX=&X,EDX=&Y,mask,blocked) at a fine-cell centre."""
        x = self.origin[0] + (fx + 0.5) * 32
        y = self.origin[1] + (fy + 0.5) * 32
        self.uc.mem_write(self.points, struct.pack('<ff', x, y))
        self.run(0x6f04d870, self.points, mask, blocked, edx=self.points + 4)

    def block_fine(self, cells, masks=(2, 4, 0x40, 0x80)):
        for fx, fy in cells:
            for mask in masks:
                self.terrain(fx, fy, mask, 1)

    def rebuild(self, rectangle=None, mode=0):
        """Original 15d360(owner, rectangle or NULL, mode)."""
        if rectangle is None:
            return self.run(0x6f15d360, self.owner, 0, mode)
        self.write(self.rect + 0x20, *rectangle)
        return self.run(0x6f15d360, self.owner, self.rect + 0x20, mode)

    def publish_gate(self, marker, box):
        """Original 04e360(ECX=marker, EDX=&{minY,minX,maxY,maxX}) world rectangle; box=(minX,minY,maxX,maxY)."""
        self.uc.mem_write(self.rect, struct.pack('<4f', box[1], box[0], box[3], box[2]))
        self.run(0x6f04e360, marker, edx=self.rect)

    def gate_active(self, identity, enabled):
        """Original 04e210(ECX=id, EDX=enabled)."""
        self.run(0x6f04e210, identity, edx=enabled)

    def gate_destination(self, identity, world):
        """Original 04e550(ECX=id, EDX=&worldXY)."""
        self.uc.mem_write(self.points + 0x10, struct.pack('<ff', *world))
        self.run(0x6f04e550, identity, edx=self.points + 0x10)

    # -- state readers -----------------------------------------------------
    def cell_word(self, level, x, y):
        side = self.sides[level]
        return self.read(self.data[level] + 8 * (y * side + x) + 4)[0]

    def class_of(self, level, x, y, lane):
        return (self.cell_word(level, x, y) >> (30 - lane)) & 3

    def marker(self, x, y):
        return (self.cell_word(0, x, y) >> 16) & 0xff

    def class_bytes(self):
        out = []
        for storage, side in zip(self.data, self.sides):
            raw = bytes(self.uc.mem_read(storage, side * side * 8))
            out.append(raw[7::8])
        return out

    def marker_bytes(self, level=0):
        side = self.sides[level]
        return bytes(self.uc.mem_read(self.data[level], side * side * 8))[6::8]

    def fine_word(self, fx, fy):
        return self.read(self.cells + 4 * (fy * self.fine_side + fx))[0]

    # -- coverage ------------------------------------------------------------
    def enable_coverage(self):
        from unicorn import UC_HOOK_BLOCK
        self.jccs = load_jccs()
        self.coverage = {}
        state = {'last_end': None}
        jcc_by_end = {}
        for address, (_, target, fall, _) in self.jccs.items():
            jcc_by_end[address] = (target, fall)

        def block(uc, address, size, user):
            previous = state['last_jcc']
            if previous is not None:
                target, fall = jcc_by_end[previous]
                if address == target and address == fall:
                    key = (previous, 'both')
                elif address == target:
                    key = (previous, 'taken')
                elif address == fall:
                    key = (previous, 'fall')
                else:
                    key = (previous, f'other:{address:x}')
                self.coverage[key] = self.coverage.get(key, 0) + 1
            state['last_jcc'] = None
            # find whether this block ends with a known Jcc
            last = self._block_last.get((address, size))
            if last is None:
                last = self._find_last(address, size)
                self._block_last[(address, size)] = last
            state['last_jcc'] = last

        state['last_jcc'] = None
        self._block_last = {}
        self._jcc_set = set(self.jccs)
        self.uc.hook_add(UC_HOOK_BLOCK, block, begin=0x6f1625f0, end=0x6f165600)
        self._cov_state = state

    def _find_last(self, address, size):
        for jcc in self._jcc_set:
            if address <= jcc < address + size:
                # A Jcc is always the final instruction of a translation block.
                text = self.jccs[jcc][3]
                if jcc + self._jcc_len(jcc) == address + size:
                    return jcc
        return None

    def _jcc_len(self, jcc):
        return self.jccs[jcc][2] - jcc

    def enable_events(self):
        """Passive semantic events: relax call sites with parent/child levels,
        lookup call sites with query level and result, coarse representative clamps."""
        from unicorn import UC_HOOK_CODE
        R = self.R
        self.events = {}
        pending = []

        def bump(key):
            self.events[key] = self.events.get(key, 0) + 1

        def node_level(index):
            return self.uc.mem_read(self.nodes + 36 * index + 0x22, 1)[0]

        def relax(uc, address, size, user):
            sp = uc.reg_read(R.UC_X86_REG_ESP)
            ret, child, parent = self.read(sp, 3)
            kind = 'special' if address == 0x6f165220 else 'relax'
            bump((kind, f'{ret - 5:08x}', node_level(parent), node_level(child), self.read(self.system + 0x90)[0]))

        def lookup_enter(uc, address, size, user):
            sp = uc.reg_read(R.UC_X86_REG_ESP)
            ret, level, x, y = self.read(sp, 4)
            pending.append((ret - 5, level))

        def lookup_leave(uc, address, size, user):
            site, level = pending.pop()
            result = self.signed(uc.reg_read(R.UC_X86_REG_EAX))
            outcome = result if result < 0 else f'node{node_level(result)}'
            bump(('lookup', f'{site:08x}', level, outcome, self.read(self.system + 0x90)[0]))

        def coarse(uc, address, size, user):
            sp = uc.reg_read(R.UC_X86_REG_ESP)
            node, level, x, y = self.read(sp + 4, 4)
            stored = self.read(self.system + 0x90)[0]
            x0, y0, side = (x >> level) << level, (y >> level) << level, 1 << level
            clamp_x = stored == 2 and x > x0 + side - 2
            clamp_y = stored == 2 and y > y0 + side - 2
            bump(('coarse', level, stored, bool(clamp_x), bool(clamp_y)))

        self.uc.hook_add(UC_HOOK_CODE, relax, begin=0x6f164020, end=0x6f164020)
        self.uc.hook_add(UC_HOOK_CODE, relax, begin=0x6f165220, end=0x6f165220)
        self.uc.hook_add(UC_HOOK_CODE, lookup_enter, begin=0x6f1625f0, end=0x6f1625f0)
        for ret in (0x6f162670, 0x6f1626f9, 0x6f162705):
            self.uc.hook_add(UC_HOOK_CODE, lookup_leave, begin=ret, end=ret)
        self.uc.hook_add(UC_HOOK_CODE, coarse, begin=0x6f1644d0, end=0x6f1644d0)

    def reset_coverage_state(self):
        if self.coverage is not None:
            self._cov_state['last_jcc'] = None

    # -- request -------------------------------------------------------------------
    def search(self, lane, source, target, budget=100000, size_input=0, warp=0, trace=False):
        self.uc.mem_write(self.points + 0x20, struct.pack('<ff', *source))
        self.uc.mem_write(self.points + 0x28, struct.pack('<ff', *target))
        edges = []
        hook = None
        if trace:
            from unicorn import UC_HOOK_CODE
            R = self.R

            def relax(uc, address, size, user):
                sp = uc.reg_read(R.UC_X86_REG_ESP)
                caller, child, parent = self.read(sp, 3)
                edges.append((address, caller, child, parent))
            hook = [self.uc.hook_add(UC_HOOK_CODE, relax, begin=a, end=a) for a in (0x6f164020, 0x6f165220)]
        self.reset_coverage_state()
        result = self.run(0x6f162cb0, self.system, lane, self.route, self.points + 0x20, self.points + 0x28, budget, size_input, warp)
        self.reset_coverage_state()
        if hook:
            for h in hook:
                self.uc.hook_del(h)
        count = self.read(self.route + 0x1c)[0]
        node_count = self.read(self.system + 0x6c)[0]
        if count > 65536 or node_count > self.node_capacity:
            raise RuntimeError('invalid adaptive result dimensions')
        nodes = []
        for i in range(node_count):
            v = self.read(self.nodes + 36 * i, 9)
            state = 0 if v[3] == 0xffffffff else 1 if v[3] == 0xfffffffe else 2
            nodes.append(dict(x=v[0], y=v[1], generation=v[2], state=state, g=v[5], h=v[6], parent=v[7] if v[7] < 0x80000000 else v[7] - (1 << 32),
                              source_marker=v[8] & 0xff, level=(v[8] >> 16) & 0xff, incoming=(v[8] >> 24) & 0xff))
        row = dict(result=result, work=self.read(self.system + 0x9c)[0], node_count=node_count,
                   route_words=self.read(self.route_data, count * 2),
                   route=[list(struct.unpack('<ff', self.uc.mem_read(self.route_data + 8 * i, 8))) for i in range(count)],
                   source_node=self.signed(self.read(self.system + 0xc4)[0]), goal_node=self.signed(self.read(self.system + 0xc8)[0]),
                   nearest_node=self.signed(self.read(self.system + 0xd0)[0]), nearest_distance2=self.read(self.system + 0xcc)[0],
                   adjusted_target=list(struct.unpack('<ff', self.uc.mem_read(self.system + 0xac, 8))),
                   warp_count=self.read(self.system + 0xa0)[0], nodes=nodes)
        if trace:
            row['relaxations'] = [dict(kind='special' if a == 0x6f165220 else 'ordinary', caller=f'{c:08x}', child=ch, parent=p) for a, c, ch, p in edges]
        return row

    @staticmethod
    def signed(v):
        return v - (1 << 32) if v >= 0x80000000 else v


def sha256_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()
