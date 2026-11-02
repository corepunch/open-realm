#!/usr/bin/env python3
"""FOOT-03 / BASE-02.2 research rig: original fine-map consumers under Unicorn.

Builds on the copied SEP-03 harness (foot03_spatial_harness_copy.py): original
PathRegistry_Construct 04a6b0, PathOwner_Construct 157610, spatial map 14c280/14c990,
fine search system 147600 and CPmRegion creation 14cf20.  Only the three Storm memory
imports are replaced by host storage (see the copied harness).  Fixture-supplied state is
listed in FIXTURE_NOTES and repeated in every report.  No retail bytes are stored here.
"""
import struct

import foot03_spatial_harness_copy as H

FIXTURE_NOTES = [
    'owner+238 (fine map) and owner+24c (fine system) are installed by the fixture, matching 15ab60 15af7c..15af8a',
    'fine-system+1c = fine map is written by the fixture, matching 15ab60 15af84..15af8a',
    'four hierarchy maps owner+23c..248 are supplied storage (data,+3c dims,+54 bounds,+64/+68 scale); their constructors are not executed',
    'global 6fd3c82c game object supplied with world bounds +6c..+78 (origin 0.0)',
    'original startup initializers 006e90/006ea0/070d80(6fd3c7a8,32) executed before construction',
    'fixture resets between independent matrix cases: cell words, link count/free head, record count and object counters',
]

QUERIES = {  # name: (path query word, hierarchy lane mask)
    'ground': 0x02000002, 'flight': 0x04000004, 'float': 0x40000040, 'amph': 0x80000080,
    'build': 0x08000008, 'item': 0x10000010,
}
LANES = [0x06000006, 0x80000080, 0x40000040, 0x04000004]  # 15d0e0 masks at 6fce4570, shifts 0/2/4/6
MOVER_TAG = 0x60706375


class Rig:
    def __init__(self, binary, width=16, height=16):
        self.e = e = H.Emu(binary)
        # original registered startup initializers (32.0/16.0 world-cell scalars, as in widget-masks oracle)
        e.call(0x6f006e90)
        e.call(0x6f006ea0)
        e.call(0x6f070d80, 0x6fd3c7a8, edx=32)
        self.world = w = H.World(e, width, height)
        self.map, self.owner = w.map, w.owner
        self.width, self.height = width, height
        self.sys = e.fixture(0x200)
        e.call(0x6f147600, self.sys)
        assert e.esp_after == 4
        e.w(self.sys + 0x1c, self.map)
        e.w(self.owner + 0x238, self.map)
        e.w(self.owner + 0x24c, self.sys)
        self.game = e.fixture(0x100)
        e.w(0x6fd3c82c, self.game)
        e.uc.mem_write(self.game + 0x6c, struct.pack('<4f', 0, 0, 0, 0))
        # supplied hierarchy storage (constructors not executed)
        self.levels = []
        base_w, base_h = (width + 16) // 2 + 1, (height + 16) // 2 + 1
        for level in range(4):
            hm = e.fixture(0x100)
            lw, lh = base_w >> level, base_h >> level
            data = e.fixture(lw * lh * 8 + 16)
            e.w(hm + 0x28, data)
            e.w(hm + 0x3c, lw, lh)
            e.w(hm + 0x54, 0, 0, lh, lw)
            e.uc.mem_write(hm + 0x64, struct.pack('<2f', 2 << level, 1 / (2 << level)))
            e.w(self.owner + 0x23c + 4 * level, hm)
            self.levels.append((hm, data, lw, lh))
        self.xy = e.fixture(0x40)
        self.cells = e.r(self.map + 0x28)

    # ---- state helpers --------------------------------------------------
    def reset_map(self, cells=None):
        e, m = self.e, self.map
        if cells is None:
            e.uc.mem_write(self.cells, b'\xff\xff\xff\x00' * (self.width * self.height))
        else:
            for x, y in cells:
                e.w(self.cells + 4 * (y * self.width + x), 0x00ffffff)
        e.w(m + 0x88, 0)          # link vector count
        e.w(m + 0xac, 0xffffff)   # free-list head
        e.w(m + 0xb0, 0)          # outstanding record count
        dirty = e.r(m + 0x98)
        for i in range(e.r(m + 0xa8)):
            e.w(dirty + 4 * i, 0)

    def set_terrain(self, x, y, high):
        word = self.e.r(self.cells + 4 * (y * self.width + x))
        self.e.w(self.cells + 4 * (y * self.width + x), (word & 0xffffff) | (high << 24))

    def new_object(self, payload=0):
        """Original 14cf20 (ECX map, stack payload, descriptor) -> CPmRegion with +34..+40 zero."""
        return self.world.create_object(payload)

    def new_static_object(self, payload=0):
        """Original 14cf70: 14cf20 then OR +40 with 10000000 (static/region object)."""
        e = self.e
        obj = e.call(0x6f14cf70, self.map, payload, 0)
        assert e.esp_after == 12
        return obj

    def record(self, x, y, obj, inserted):
        """Original 14d9e0 SpatialMap_PrependCellRecord (x, y, obj, kind word) RET10."""
        e = self.e
        e.call(0x6f14d9e0, self.map, x, y, obj, 0x01000000 if inserted else 0)
        assert e.esp_after == 20

    def metadata(self, x, y):
        self.world.metadata(x, y, 0, 0)

    def chain(self, x, y):
        return self.world.chain(y * self.width + x)

    def mover(self):
        mv = self.world.make_mover()
        return mv

    # ---- consumers -------------------------------------------------------
    def fine_cell(self, x, y, query, mode=0, target=0):
        """1489a0 PathFine_TestOccupiedCell (ECX fine system, x, y) RET8 -> 1 clear / 0 blocked."""
        e, s = self.e, self.sys
        e.w(s + 0xa4, query)
        e.w(s + 0xa8, target)
        e.w(s + 0xcc, 0, 0, mode)
        result = e.call(0x6f1489a0, s, x, y)
        assert e.esp_after == 12
        return dict(clear=result, target_seen=e.r(s + 0xcc), blocked_flag=e.r(s + 0xd0))

    def hier_cell(self, x, y, lane):
        """1493d0 wrapper (ECX fine system, xy*, mask*) RET8 -> 148e90; 1 clear / 0 blocked."""
        e = self.e
        e.w(self.xy, x, y, lane)
        result = e.call(0x6f1493d0, self.sys, self.xy, self.xy + 8)
        assert e.esp_after == 12
        return result

    def collect(self, x, y, query):
        """148ad0 PathFine_CollectCellBlockers (ECX fine, x, y, vector) RET0c into fine+ac vector."""
        e, s = self.e, self.sys
        vec = s + 0xac
        e.w(vec + 0x1c, 0)
        e.w(s + 0xa4, query)
        e.call(0x6f148ad0, s, x, y, vec)
        assert e.esp_after == 16
        count = e.r(vec + 0x1c)
        data = e.r(vec + 0xc)
        return [e.r(data + 4 * i) for i in range(count)]

    def union(self, x, y):
        """148060 (ECX fine, xy*) RET4 -> 149170 with query 0: cell high byte | eligible categories."""
        e = self.e
        e.w(self.xy, x, y)
        result = e.call(0x6f148060, self.sys, self.xy)
        assert e.esp_after == 8
        return result

    def perimeter(self, x, y, offset, width, query, mode=0, target=0):
        """148d00 PathFine_ReadBlockedPerimeter (ECX fine, x, y, offset, width) RET10 -> blocked bits."""
        e, s = self.e, self.sys
        e.w(s + 0xa4, query)
        e.w(s + 0xa8, target)
        e.w(s + 0xcc, 0, 0, mode)
        result = e.call(0x6f148d00, s, x, y, offset, width)
        assert e.esp_after == 20
        return result

    def segment(self, entry, x, y, crossing, query, mode=0):
        """149440/149630/149970/149cc0 class segment tests (ECX fine, xy*, crossing) RET8."""
        e, s = self.e, self.sys
        e.w(s + 0xa4, query)
        e.w(s + 0xcc, 0, 0, mode)
        e.w(self.xy, x, y)
        result = e.call(entry, s, self.xy, crossing)
        assert e.esp_after == 12
        return result

    def footprint(self, x, y, query, klass, mode=1):
        """1492b0 PathFine_TestFootprintCells (ECX fine, xy*, mask*, class) RET0c."""
        e, s = self.e, self.sys
        e.w(s + 0xd4, mode)
        e.w(self.xy, x, y, query)
        result = e.call(0x6f1492b0, s, self.xy, self.xy + 8, klass)
        assert e.esp_after == 16
        return result

    def point_query(self, x, y, query, bridge=0):
        """04df50 PathWorld_TestPointQuery fastcall ECX X*, EDX Y*, stack mask*, bridge; RET8.

        Forces fine+d4=1 around 149320 and suppresses the bridge mover's fine object via 05bd30.
        Returns 1 when blocked.  World point = fine centre * 32 with origin 0.
        """
        e = self.e
        e.uc.mem_write(self.xy + 0x10, struct.pack('<2f', (x + 0.5) * 32, (y + 0.5) * 32))
        e.w(self.xy + 0x18, query)
        e.w(self.sys + 0xd4, 0x5a5a)
        result = e.call(0x6f04df50, self.xy + 0x10, self.xy + 0x18, bridge, edx=self.xy + 0x14)
        assert e.esp_after == 12
        assert e.r(self.sys + 0xd4) == 0x5a5a, 'endpoint mode not restored'
        assert e.r(0) == 0, 'SEH chain not restored'
        return result

    def rebuild_hierarchy(self):
        """15d360 PathMaps_UpdateRectangle(owner, NULL rectangle = fine bounds, mode0) RET8."""
        e = self.e
        for hm, data, lw, lh in self.levels:
            e.uc.mem_write(data, bytes(lw * lh * 8))
        e.call(0x6f15d360, self.owner, 0, 0)
        assert e.esp_after == 12

    def hier_class(self, x, y, level=0):
        """Two-bit lane classes of hierarchy cell containing fine (x,y) at a level (byte7)."""
        hm, data, lw, lh = self.levels[level]
        scale = 2 << level
        cx, cy = x // scale, y // scale
        byte = self.e.r(data + 8 * (cy * lw + cx) + 4) >> 24
        return dict(ground=(byte >> 6) & 3, amph=(byte >> 4) & 3, float=(byte >> 2) & 3, flight=byte & 3)

    def register_mover(self, mover):
        """Original 1c53f0 registry activation of a mover (descriptor +24=-1) -> identity +14/+18."""
        e = self.e
        desc = e.fixture(0x40)
        e.w(desc + 0x24, 0xffffffff, 0xffffffff)
        e.call(0x6f1c53f0, mover, desc)
        assert e.esp_after == 8
        bridge = e.fixture(0x20)
        e.w(bridge + 8, e.r(mover + 0x14), e.r(mover + 0x18))
        return bridge
