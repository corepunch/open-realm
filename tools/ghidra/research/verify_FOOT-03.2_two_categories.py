#!/usr/bin/env python3
"""FOOT-03.2: two eligible categories in one fine cell, removed in both orders (original code).

Producers (unmodified retail code under Unicorn; see foot03_rig.py for imports/fixtures):
  unit  A: CPmRegion 14cf20 + 15ed40 active-bit instruction (6f15ee0a), registry activation
           1c53f0, category/query publication 05c7e0 (ca/2), footprint 160590 (hfoo radius
           0.96875 at fine (7,7) -> cells 6..7 x 6..7)
  widget B: region collection 064460 (masks c2/10/08 -> 14cf70 static objects), raster
           22e9c0 + 652b40 with a 3x3 texture of pixel da centred on fine (6.5,6.5)
Removals:
  unit  'retire'  14dae0 SpatialObject_Retire (mover release path)
        'move'    160590 to fine (12,12)
  widget 'retire' 063b40 collection retirement (Widget_RetirePathing 650c00 core; static objects
                  emit no removal records)
         'unraster' 22e9c0 + 650a70 removal raster (vtable+150 6514d0 core)
Each step evaluates every consumer at shared cell C=(6,6) and records object/map counters;
the run ends with original dirty-cell (14df20) and full (14dfc0) compaction.
"""
import argparse
import hashlib
import itertools
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from foot03_rig import Rig, FIXTURE_NOTES  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX  # noqa: E402

C = (6, 6)
QUERY = dict(ground=0x02000002, flight=0x04000004, float=0x40000040, amph=0x80000080, build=0x08000008,
             item=0x10000010)


class Scene:
    def __init__(self, binary):
        self.r = r = Rig(binary)
        self.e = e = r.e
        r.reset_map()
        e.uc.mem_write(r.game + 0x6c, struct.pack('<4f', 0, 0, 512, 512))
        # unit A
        self.mover = r.mover()
        self.unit = r.new_object(self.mover)
        e.w(self.mover + 0x98, self.unit)
        e.uc.reg_write(UC_X86_REG_EAX, self.unit)
        e.uc.emu_start(0x6f15ee0a, 0x6f15ee11)  # original 15ed40 instruction: or [eax+34],01000000
        self.bridge = r.register_mover(self.mover)
        e.w(self.mover + 0xa8, 0)  # no owned path: 05c770 publishes nothing
        e.call(0x6f05c7e0, self.bridge, 0xca, 2)
        assert e.esp_after == 12
        self.radius, self.pos = e.fixture(8), e.fixture(8)
        e.uc.mem_write(self.radius, struct.pack('<f', 0.96875))
        # widget B
        self.coll, self.list = e.fixture(0x40), e.fixture(0x40)
        e.w(self.coll, 4, 0, self.list)
        self.widget = e.fixture(0x40)
        e.w(self.widget + 0xc, 0xffffffff, 0xffffffff)
        masks = e.fixture(0x10)
        e.w(masks, 0xc2, 0x10, 0x08)
        e.call(0x6f064460, self.coll, self.widget, 3, masks)
        assert e.esp_after == 16
        self.regions = [e.r(self.list + 4 * i) for i in range(3)]
        self.texture, pixels = e.fixture(0x40), e.fixture(0x10)
        e.w(self.texture + 8, 3, 3)
        e.w(self.texture + 0x20, pixels)
        e.uc.mem_write(pixels, bytes([0xda] * 9))
        self.centre = e.fixture(8)
        e.uc.mem_write(self.centre, struct.pack('<2f', 208, 208))
        self.names = {self.unit: 'A.unit', self.regions[0]: 'B.c2', self.regions[1]: 'B.10', self.regions[2]: 'B.08',
                      self.mover: 'A.mover', 0: 'null'}

    # producers --------------------------------------------------------------
    def unit_at(self, x, y):
        e = self.e
        e.uc.mem_write(self.pos, struct.pack('<2f', x, y))
        e.call(0x6f160590, self.mover, self.radius, self.pos)
        assert e.esp_after == 12

    def raster(self, callback):
        e = self.e
        e.call(0x6f22e9c0, self.texture, self.centre, 0, callback, self.coll)
        assert e.esp_after == 20

    def insert(self, who):
        if who == 'unit':
            self.unit_at(7.0, 7.0)
        else:
            self.raster(0x6f652b40)

    def remove(self, who, how):
        e = self.e
        if who == 'unit':
            if how == 'retire':
                e.call(0x6f14dae0, self.unit)
                assert e.esp_after == 4
            else:
                self.unit_at(12.0, 12.0)
        else:
            if how == 'retire':
                e.call(0x6f063b40, self.coll)
                assert e.esp_after == 4
            else:
                self.raster(0x6f650a70)

    # observation ------------------------------------------------------------
    def observe(self, label):
        r, e = self.r, self.e
        x, y = C
        chain = [(kind, self.names.get(p, hex(p))) for _, kind, p in r.chain(x, y)]
        objects = {}
        for obj in [self.unit] + self.regions:
            objects[self.names[obj]] = dict(w34=hex(e.r(obj + 0x34)), w38=hex(e.r(obj + 0x38)),
                                            refs3c=e.r(obj + 0x3c) & 0xffffff, w40=hex(e.r(obj + 0x40)),
                                            identity=[hex(v) for v in e.r(obj + 0x14, 2)])
        m = r.map
        stamp = e.r(m + 0xb4)
        out = dict(step=label, chain_C=chain, objects=objects, map_records_b0=e.r(m + 0xb0),
                   link_count_88=e.r(m + 0x88), free_links=len(r.world.free_list()),
                   dirty_C=(y * r.width + x) in r.world.dirty())
        fine = {}
        for q, word in QUERY.items():
            fine[q] = dict(mode0=r.fine_cell(x, y, word, 0)['clear'], mode1=r.fine_cell(x, y, word, 1)['clear'])
        out['fine_search_cell_clear'] = fine
        out['fine_perimeter_node7_6_class0'] = {q: r.perimeter(x + 1, y, 1, 3, w, 0) for q, w in QUERY.items()}
        out['segment_clear'] = {q: dict(class0=r.segment(0x6f149440, x, y, 0, w, 0),
                                        class1=r.segment(0x6f149630, x, y, 0, w, 0)) for q, w in QUERY.items()}
        out['endpoint_footprint_clear_mode1'] = {q: dict(class0=r.footprint(x, y, w, 0, 1),
                                                         class1=r.footprint(x, y, w, 1, 1)) for q, w in QUERY.items()}
        out['endpoint_point_query_blocked'] = {q: dict(no_bridge=r.point_query(x, y, w, 0),
                                                       unit_bridge=r.point_query(x, y, w, self.bridge))
                                               for q, w in QUERY.items()}
        out['collector_tokens_ground'] = [self.names.get(t, hex(t)) for t in r.collect(x, y, QUERY['ground'])]
        out['union_categories'] = hex(r.union(x, y))
        r.rebuild_hierarchy()
        out['hierarchy_after_full_rebuild'] = dict(level0=r.hier_class(x, y, 0), level1=r.hier_class(x, y, 1))
        # consumers stamp objects: restore nothing (stamps are part of retail behaviour) but report it
        out['map_stamp_b4_before_observation'] = stamp
        return out


def scenario(binary, first, unit_how, widget_how, removal_first):
    s = Scene(binary)
    steps = [s.observe('empty')]
    second = 'widget' if first == 'unit' else 'unit'
    s.insert(first)
    steps.append(s.observe(f'insert {first}'))
    s.insert(second)
    steps.append(s.observe(f'insert {second}'))
    order = [removal_first, 'widget' if removal_first == 'unit' else 'unit']
    for who in order:
        how = unit_how if who == 'unit' else widget_how
        s.remove(who, how)
        steps.append(s.observe(f'remove {who} by {how}'))
    s.e.call(0x6f14df20, s.r.map)
    steps.append(s.observe('dirty-cell compaction 14df20'))
    s.e.call(0x6f14dfc0, s.r.map)
    steps.append(s.observe('full compaction 14dfc0'))
    return dict(insert_first=first, unit_removal=unit_how, widget_removal=widget_how, removal_first=removal_first,
                steps=steps, storm_calls=[dict(op=c['op'], size=c.get('size')) for c in s.e.log])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--expected', type=Path, help='write normalized expected JSON')
    args = parser.parse_args()
    results = []
    for first, unit_how, widget_how, removal_first in itertools.product(
            ('unit', 'widget'), ('retire', 'move'), ('retire', 'unraster'), ('unit', 'widget')):
        results.append(scenario(args.binary, first, unit_how, widget_how, removal_first))
    # repeat determinism: rerun the first scenario and require identical observations
    repeat = scenario(args.binary, 'unit', 'retire', 'retire', 'unit')
    assert json.dumps(repeat['steps'], sort_keys=True) == json.dumps(results[0]['steps'], sort_keys=True)
    here = Path(__file__).resolve().parent
    binary_sha = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    report = dict(task='FOOT-03.2', binary_sha256=binary_sha, fixture_notes=FIXTURE_NOTES,
                  sources={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in
                           (Path(__file__).resolve(), here / 'foot03_rig.py', here / 'foot03_spatial_harness_copy.py')},
                  shared_cell=C, scenarios=results, repeat_identical=True)
    text = json.dumps(report, indent=1, sort_keys=True) + '\n'
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(text)
    print(f'{len(results)} scenarios x {len(results[0]["steps"])} steps; report sha256 {hashlib.sha256(text.encode()).hexdigest()}')
    if args.expected:
        expected = dict(task='FOOT-03.2', binary_sha256=binary_sha, shared_cell=C,
                        scenarios=[dict({k: v for k, v in sc.items() if k not in ('steps', 'storm_calls')},
                                        steps=[{k: v for k, v in st.items() if k != 'map_stamp_b4_before_observation'}
                                               for st in sc['steps']]) for sc in results])
        etext = json.dumps(expected, indent=1, sort_keys=True) + '\n'
        args.expected.write_text(etext)
        print('expected sha256', hashlib.sha256(etext.encode()).hexdigest())
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
