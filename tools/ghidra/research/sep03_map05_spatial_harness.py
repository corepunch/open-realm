#!/usr/bin/env python3
"""Shared original-code harness for SEP-03.x / MAP-05.x spatial-storage research.

Runs unmodified retail game.dll 1.27.1.7085 code under Unicorn. The only replaced
code is the three external Storm memory imports (thunks 6f07c6d2 SMemAlloc/401,
6f07c6d8 SMemReAlloc/405, 6f07c678 SMemFree/403), which receive host storage with
Storm's zero-fill flag (8) honoured. Optional *labelled interventions* make an
import return NULL; Storm itself never does that (it calls SErrDisplayError and
ExitProcess(1), see the SEP-03.3 handoff), so intervention results are never
reachable evidence. No retail bytes are stored in this file.
"""
import hashlib
import json
import struct
from pathlib import Path

SHA256 = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
SALLOC, SREALLOC, SFREE = 0x6f07c6d2, 0x6f07c6d8, 0x6f07c678
OWNER_GLOBAL, REGISTRY_GLOBAL, REGISTRY_ALIAS = 0x6fd53a48, 0x6fd68610, 0x6fd6860c
EMPTY_RECT = (-128000, -128000, -128000, -128000)
SENTINEL = 0xffffff


class Emu:
    def __init__(self, binary_path):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_INVALID
        from unicorn import x86_const as X
        self.X = X
        binary = Path(binary_path).read_bytes()
        self.binary_sha256 = hashlib.sha256(binary).hexdigest()
        if self.binary_sha256 != SHA256:
            raise SystemExit('requires game.dll 1.27.1.7085')
        pe = struct.unpack_from('<I', binary, 0x3c)[0]
        opt = pe + 24
        base, size = (struct.unpack_from('<I', binary, opt + o)[0] for o in (28, 56))
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(base, (size + 4095) & ~4095)
        uc.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
        for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
            sec = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
            va, count, off = struct.unpack_from('<III', binary, sec + 12)
            if count:
                uc.mem_write(base + va, binary[off:off + count])
        uc.mem_map(0, 0x1000)                 # FS:[0] SEH chain (Unicorn FS base 0)
        # Any other access to page 0 is a NULL+offset dereference: stop and report it.
        from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE

        def null_page(machine, access, address, size, value, data):
            self.fault = dict(access=access, address=address, eip=machine.reg_read(X.UC_X86_REG_EIP), null_page=1)
            machine.emu_stop()
        uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, null_page, begin=4, end=0xfff)
        uc.mem_map(0x10000000, 0x200000)      # fixture objects
        uc.mem_map(0x20000000, 0x20000)       # stack
        uc.mem_map(0x40000000, 0x8000000)     # host storage behind Storm imports
        self.stack, self.stop = 0x2001f000, 0x30000000
        self.next_block = 0x40000000
        self.allocations = {}
        self.log = []                         # every import call, in order
        self.fail = None                      # labelled intervention: callable(kind, size) -> bool
        self.watch = {}                       # address -> callback(emu)
        self.fixture_next = 0x10000000
        for a, v in ((0x6fd3c740, -1.0), (0x6fd3c744, 0.0), (0x6fd3c748, 1.0)):
            uc.mem_write(a, struct.pack('<f', v))
        for a in (SALLOC, SREALLOC, SFREE):
            uc.hook_add(UC_HOOK_CODE, self._storm, begin=a, end=a)

        def invalid(machine, access, address, size, value, data):
            self.fault = dict(access=access, address=address, eip=machine.reg_read(X.UC_X86_REG_EIP))
            return False
        self.fault = None
        uc.hook_add(UC_HOOK_MEM_INVALID, invalid)

    # ---- raw memory -------------------------------------------------------
    def w(self, address, *values):
        self.uc.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def r(self, address, count=1):
        v = struct.unpack('<' + 'I' * count, self.uc.mem_read(address, 4 * count))
        return v[0] if count == 1 else list(v)

    def f(self, address):
        return struct.unpack('<f', self.uc.mem_read(address, 4))[0]

    def fixture(self, size, fill=0):
        address = self.fixture_next
        self.fixture_next += (size + 0xff) & ~0xff
        self.uc.mem_write(address, bytes([fill]) * size)
        return address

    # ---- Storm imports (host storage; optional labelled NULL intervention) --
    def _storm(self, uc, address, size, data):
        X = self.X
        sp = uc.reg_read(X.UC_X86_REG_ESP)
        ret = self.r(sp)
        if address == SFREE:
            ptr, = self.r(sp + 4, 1),
            freed = self.allocations.pop(ptr, None)
            self.log.append(dict(op='free', ptr=ptr, size=freed, caller=ret))
            result, argc = 1, 4
        else:
            if address == SALLOC:
                wanted, _file, _line, flags = self.r(sp + 4, 4)
                old, argc = None, 4
            else:
                old, wanted, _file, _line, flags = self.r(sp + 4, 5)
                argc = 5
                if old in (0, 0xffffffff) or old not in self.allocations:
                    old = None
            kind = 'alloc' if address == SALLOC else 'realloc'
            if self.fail and self.fail(kind, wanted):
                result = 0
                self.log.append(dict(op=kind, size=wanted, flags=flags, result=0, caller=ret,
                                     intervention='labelled NULL return; unreachable in retail Storm'))
            else:
                result = self.next_block
                self.next_block += (wanted + 0xfff) & ~0xfff
                if self.next_block > 0x48000000:
                    raise RuntimeError('host arena exhausted')
                fill = b'\0' if flags & 8 else b'\xa5'
                self.uc.mem_write(result, fill * wanted)
                prior = 0
                if old is not None:
                    prior = self.allocations.pop(old)
                    self.uc.mem_write(result, bytes(self.uc.mem_read(old, min(prior, wanted))))
                self.allocations[result] = wanted
                self.log.append(dict(op=kind, size=wanted, previous=prior, old=old or 0, flags=flags,
                                     result=result, caller=ret))
        uc.reg_write(X.UC_X86_REG_EAX, result)
        uc.reg_write(X.UC_X86_REG_ESP, sp + 4 + 4 * argc)
        uc.reg_write(X.UC_X86_REG_EIP, ret)

    # ---- calls --------------------------------------------------------------
    def call(self, entry, ecx=0, *args, edx=0, budget=50_000_000):
        X = self.X
        self.w(self.stack, self.stop, *args)
        self.uc.reg_write(X.UC_X86_REG_ESP, self.stack)
        self.uc.reg_write(X.UC_X86_REG_ECX, ecx)
        self.uc.reg_write(X.UC_X86_REG_EDX, edx)
        self.fault = None
        try:
            self.uc.emu_start(entry, self.stop, count=budget)
        except Exception as error:
            raise RuntimeError(f'original {entry:#x} faulted: {error} {self.fault}') from error
        if self.fault and self.fault.get('null_page'):
            raise RuntimeError(f'original {entry:#x} faulted: NULL-page access {self.fault}')
        if self.uc.reg_read(X.UC_X86_REG_EIP) != self.stop:
            raise RuntimeError(f'original {entry:#x} exceeded instruction budget')
        # callee-cleanup check: the original RET n must return ESP to stack + 4 + 4*len(args)
        self.esp_after = self.uc.reg_read(X.UC_X86_REG_ESP) - self.stack
        return self.uc.reg_read(X.UC_X86_REG_EAX)


class World:
    """Owner + registry + one spatial map, all created by original constructors."""

    def __init__(self, emu, width=8, height=8, factory=False):
        self.e = e = emu
        # Registry: original PathRegistry_Construct publishes 6fd68610; creator aliases 6fd6860c.
        self.registry = e.fixture(0x100)
        e.call(0x6f04a6b0, self.registry)
        assert e.r(REGISTRY_GLOBAL) == self.registry
        e.w(REGISTRY_ALIAS, self.registry)
        # Owner: original PathOwner_Construct (ECX owner, stack seed, RET4).
        self.owner = e.fixture(0x1000)
        e.call(0x6f157610, self.owner, 7085)
        assert e.esp_after == 8, e.esp_after
        e.w(OWNER_GLOBAL, self.owner)
        self.pool = self.owner + 0x5d8
        # Spatial map: original C++ constructor 6f14c280 then descriptor init 6f14c990
        # (registration, cell/dirty storage, maintenance timer request).
        if factory:
            self.map = self.factory_map(width, height)
        else:
            self.map = e.fixture(0x100) + 4
            e.call(0x6f14c280, self.map)
            desc = e.fixture(0x40)
            e.w(desc + 0x10, width, height)
            e.w(desc + 0x24, 0xffffffff, 0xffffffff)
            e.call(0x6f14c990, self.map, desc)
            assert e.esp_after == 8
        self.width, self.height = width, height

    def factory_map(self, width, height):
        """Original spatial-map factory 6f14efe0 (ECX out, EDX descriptor, stack init flag; RET4):
        pops owner+598 pool or constructs a raw element (6f151580 -> 6f14c280), then vtable+c init."""
        e = self.e
        out, desc = e.fixture(16), e.fixture(0x40)
        e.w(desc + 0x10, width, height)
        e.w(desc + 0x24, 0xffffffff, 0xffffffff)
        e.call(0x6f14efe0, out, 1, edx=desc)
        assert e.esp_after == 8
        self.width, self.height = width, height
        return e.r(out)

    def release_map(self):
        """Original virtual release, map vtable 6fa90a44 slot +10 = 6f14cac0 (ECX map, stack 0; RET4)."""
        e = self.e
        assert e.r(self.map) == 0x6fa90a44
        e.call(e.r(0x6fa90a44 + 0x10), self.map, 0)
        assert e.esp_after == 8

    def map_pool(self):
        e, p = self.e, self.owner + 0x598
        free, at = [], e.r(p + 0x14)
        while at:
            free.append(at + 4); at = e.r(at)
        return dict(element_size=e.r(p), per_block=e.r(p + 4), recycled=free, live=e.r(p + 0x18), created=e.r(p + 0x1c))

    # map views --------------------------------------------------------------
    def fields(self):
        e, m = self.e, self.map
        return dict(link_data=e.r(m + 0x78), link_growth=e.r(m + 0x80), link_capacity=e.r(m + 0x84),
                    link_count=e.r(m + 0x88), free_head=e.r(m + 0xac), records=e.r(m + 0xb0),
                    stamp=e.r(m + 0xb4), dirty_words=e.r(m + 0xa8), dirty_data=e.r(m + 0x98),
                    cells=e.r(m + 0x38), timer_request=e.r(m + 0xb8))

    def chain(self, cell):
        e, m = self.e, self.map
        links = e.r(m + 0x78)
        index, out, seen = e.r(e.r(m + 0x28) + 4 * cell) & SENTINEL, [], set()
        while index != SENTINEL:
            if index in seen or len(out) > 100000:
                raise RuntimeError('cyclic cell chain')
            seen.add(index)
            word, payload = e.r(links + 8 * index, 2)
            out.append((index, word >> 24, payload))
            index = word & SENTINEL
        return out

    def free_list(self):
        e, m = self.e, self.map
        links, index, out = e.r(m + 0x78), e.r(m + 0xac), []
        while index != SENTINEL:
            if index in out:
                raise RuntimeError('cyclic free list')
            out.append(index)
            index = e.r(links + 8 * index) & SENTINEL
        return out

    def dirty(self):
        e, m = self.e, self.map
        words = e.r(m + 0xa8)
        data = e.r(m + 0x98)
        bits = [e.r(data + 4 * i) for i in range(words)]
        return sorted(32 * i + b for i, word in enumerate(bits) for b in range(32) if word >> b & 1)

    def pool_state(self):
        e, p = self.e, self.pool
        free, at = [], e.r(p + 0x14)
        while at:
            free.append(at + 4)
            at = e.r(at)
            if len(free) > 100000:
                raise RuntimeError('cyclic pool')
        return dict(element_size=e.r(p), per_block=e.r(p + 4), raw_allocated=e.r(p + 8), block_head=e.r(p + 0xc),
                    raw_free_head=e.r(p + 0x10), recycled=free, live=e.r(p + 0x18), created=e.r(p + 0x1c))

    # original operations -----------------------------------------------------
    def create_object(self, mover):
        """Original 6f14cf20(ECX map, stack mover, descriptor) -> spatial object (RET8)."""
        e = self.e
        desc = e.fixture(0x40)
        e.w(desc + 0x24, 0xffffffff, 0xffffffff)
        obj = e.call(0x6f14cf20, self.map, mover, desc)
        assert e.esp_after == 12
        return obj

    def update(self, obj, rect):
        e = self.e
        if not hasattr(self, '_urect'):
            self._urect = e.fixture(16)
        r = self._urect
        e.w(r, *rect)
        e.call(0x6f14e770, obj, r)
        assert e.esp_after == 8

    def retire(self, obj):
        self.e.call(0x6f14dae0, obj)
        assert self.e.esp_after == 4

    def metadata(self, x, y, lo, hi):
        e = self.e
        e.call(0x6f14d890, self.map, x, y, lo, hi)
        assert e.esp_after == 20

    def compact_dirty(self):
        self.e.call(0x6f14df20, self.map)
        assert self.e.esp_after == 4

    def compact_all(self):
        self.e.call(0x6f14dfc0, self.map)
        assert self.e.esp_after == 4

    def query(self, rect, mover_query=None):
        """Original separation rectangle traversal 6f170c00(ECX query, map, rect) RET8."""
        e = self.e
        if mover_query is None:
            if not hasattr(self, '_query'):
                self._query, self._qrect = self.make_query(), e.fixture(16)
            mover_query = self._query
        q = mover_query
        entries = e.r(q + 0xc)
        e.w(q + 0x1c, 0)
        r = self._qrect
        e.w(r, *rect)
        e.call(0x6f170c00, q, self.map, r)
        assert e.esp_after == 12
        return [e.r(entries + 8 * n) for n in range(e.r(q + 0x1c))]

    def make_query(self):
        e = self.e
        q = e.fixture(0x80)
        entries = e.fixture(8 * 4096)
        e.w(q + 0xc, entries)
        e.w(q + 0x14, 0, 4096, 0)
        e.w(q + 0x20, 0)
        e.w(q + 0x40, 0)
        e.w(q + 0x50, 0)
        return q

    def make_mover(self):
        """Synthetic mover accepted by the original 6f16e830 filter (fields documented in SEP ledger)."""
        e = self.e
        mover = e.fixture(0x200)
        e.w(mover + 0x10, 0x60706375, 0)
        e.w(mover + 0x90, 0x3f800000)
        e.w(mover + 0xac, mover + 0x100)
        e.w(mover + 0xc0, 0)
        e.w(mover + 0x120, 0)
        return mover


def fill_links(w, target, big=(0, 0, 16, 16), alt=(32, 32, 48, 48), park=(60, 0)):
    """Advance a fresh map's link high-water count (+88) to exactly `target` using ordinary
    original updates of two filler objects (no direct writes)."""
    filler = w.create_object(w.make_mover())
    flip = 0
    while w.fields()['link_count'] + 512 <= target:
        w.update(filler, big if flip == 0 else alt); flip ^= 1
    small = w.create_object(w.make_mover())
    y, x = park
    w.update(small, (y, x, y + 1, x + 1))
    pos = 0
    while w.fields()['link_count'] < target:
        if target - w.fields()['link_count'] == 1:
            extra = w.create_object(w.make_mover()); w.update(extra, (y + 3, x + 3, y + 4, x + 4))
        else:
            pos ^= 1; w.update(small, (y, x + pos, y + 1, x + pos + 1))
    assert w.fields()['link_count'] == target
    return [filler, small]


def dump(path, payload):
    text = json.dumps(payload, indent=1, sort_keys=True) + '\n'
    Path(path).write_text(text)
    return hashlib.sha256(text.encode()).hexdigest()
