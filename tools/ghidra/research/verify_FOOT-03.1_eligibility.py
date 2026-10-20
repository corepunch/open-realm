#!/usr/bin/env python3
"""FOOT-03.1: original fine-map object eligibility at the four query consumers.

Executes unmodified retail game.dll 1.27.1.7085 code under Unicorn (see foot03_rig.py for
the replaced Storm imports and fixture-supplied state) and compares every result with the
independent model in this file:

  fine     1489a0 PathFine_TestOccupiedCell      (fine search 148d00, segment 1494xx..149cc0,
                                                   endpoint 1492b0/149320/149370; mode = fine+d4)
  hier     148e90 via 1493d0 (hierarchy 15d0e0)  (adaptive base classification)
  collect  148ad0 PathFine_CollectCellBlockers  (next-step/segment yielding 166140)
  union    149170 via 148060 (query 0)          (world cell flag query 04c650, not one of the four)

Matrices: every single-link attribute combination, every ordered two-record chain over two
objects, the 148e90 49-link examination cap, composed consumers (perimeter, four segment
classes, four endpoint footprint classes in both modes, 04df50 point query with bridge
suppression, full 15d360 hierarchy rebuild) and per-producer category witnesses.
"""
import argparse
import hashlib
import itertools
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from foot03_rig import Rig, QUERIES, LANES, MOVER_TAG, FIXTURE_NOTES  # noqa: E402

STAMP = 1000  # fixture map stamp before each call; predicates increment it first


class Obj:
    def __init__(self, cat=0, active=True, live='live', flags=0, mover=True):
        self.cat, self.active, self.live, self.flags, self.mover = cat, active, live, flags, mover


def model(pred, links, objs, query, mode=0, target=None):
    """Independent model of the four predicates.  links: head-first [(kind, objIndex)]."""
    seen = set()
    target_seen = 0
    tokens = []
    union = 0
    stamp_now = STAMP + 1
    for position, (kind, o) in enumerate(links, start=1):
        if pred == 'hier' and position >= 50:
            return dict(clear=1)
        if kind == 2:
            continue
        ob = objs[o]
        if ob.live == 'dead' or not ob.active:
            continue
        if pred == 'hier' and not ob.flags & 0x10000000:
            continue
        already = o in seen or ob.live == 'collide'
        if already:
            continue
        seen.add(o)
        if kind != 1:
            continue
        if pred == 'fine' and target == o:
            target_seen = 1
        if ob.flags & 0x8fffffff:
            continue
        if pred == 'fine' and not mode and ob.flags & 0x60000000:
            continue
        if pred == 'union':
            union |= ob.cat & 0xffffff
            continue
        if not ob.cat & query & 0xffffff:
            continue
        if pred == 'collect':
            tokens.append(('mover', o) if ob.mover else ('null', o))
            continue
        if pred == 'fine':
            return dict(clear=0, target_seen=target_seen, blocked_flag=1)
        return dict(clear=0)
    if pred == 'fine':
        return dict(clear=1, target_seen=target_seen, blocked_flag=0)
    if pred == 'collect':
        return dict(tokens=tokens)
    if pred == 'union':
        return dict(union=union)
    return dict(clear=1)


class Oracle:
    def __init__(self, binary):
        self.r = Rig(binary)
        self.e = self.r.e
        self.pool = []  # (object, mover)
        for n in range(4):
            mv = self.r.mover()
            self.pool.append((self.r.new_object(mv), mv))
        self.nonmover = self.e.fixture(0x40)  # payload without mover tag at +10
        self.cases = 0
        self.mismatches = []

    def install(self, objs):
        e = self.e
        out = []
        for (obj, mv), ob in zip(self.pool, objs):
            e.w(obj + 0x30, mv if ob.mover else self.nonmover)
            e.w(obj + 0x34, ((0x01000000 if ob.active else 0) | (ob.cat & 0xffffff)))
            e.w(obj + 0x38, {'live': 0, 'dead': 0xffffffff, 'collide': STAMP + 1}[ob.live])
            e.w(obj + 0x3c, 0)
            e.w(obj + 0x40, ob.flags)
            out.append(obj)
        return out

    def build(self, links, objs, x=5, y=5):
        r = self.r
        r.reset_map([(x, y)])
        addresses = self.install(objs)
        for kind, o in reversed(links):  # prepend writer: emit tail first
            if kind == 2:
                r.metadata(x, y)
            elif kind in (0, 1):
                r.record(x, y, addresses[o], kind == 1)
            else:  # synthetic unknown kind (no producer found); written directly
                m = r.map
                links_base = self.e.r(m + 0x78)
                count = self.e.r(m + 0x88)
                cell = r.cells + 4 * (y * r.width + x)
                head = self.e.r(cell)
                self.e.w(links_base + 8 * count, (kind << 24) | (head & 0xffffff), addresses[o])
                self.e.w(m + 0x88, count + 1)
                self.e.w(cell, (head & 0xff000000) | count)
        got = [(k, addresses.index(p) if p in addresses else None) for _, k, p in r.chain(x, y)]
        expect = [(k, o if k != 2 else None) for k, o in links]
        assert [k for k, _ in got] == [k for k, _ in expect], (got, expect)
        return addresses

    def run(self, pred, links, objs, query, mode=0, target=None, x=5, y=5):
        e, r = self.e, self.r
        addresses = self.build(links, objs, x, y)
        e.w(r.map + 0xb4, STAMP)
        tgt = addresses[target] if target is not None else 0
        if pred == 'fine':
            got = r.fine_cell(x, y, query, mode, tgt)
        elif pred == 'hier':
            got = dict(clear=r.hier_cell(x, y, query))
        elif pred == 'collect':
            raw = r.collect(x, y, query)
            tokens = []
            for t in raw:
                if t == 0:
                    tokens.append(('null', None))
                else:
                    tokens.append(('mover', [mv for _, mv in self.pool].index(t)))
            got = dict(tokens=tokens)
        else:
            got = dict(union=r.union(x, y))
        expect = model(pred, links, objs, query, mode, target)
        if pred == 'collect':  # null tokens carry no identity in retail
            expect = dict(tokens=[(k, o if k == 'mover' else None) for k, o in expect['tokens']])
        self.cases += 1
        if got != expect:
            self.mismatches.append(dict(pred=pred, links=links, objs=[vars(o) for o in objs], query=hex(query),
                                        mode=mode, target=target, got=got, expected=expect))
            if len(self.mismatches) > 50:
                raise SystemExit(json.dumps(self.mismatches[:5], indent=1))
        return got


FLAGS = [0, 1, 2, 0x0fffffff, 0x10000000, 0x10000001, 0x20000000, 0x40000000, 0x60000000,
         0x80000000, 0x30000000, 0x50000000, 0x70000000, 0x90000000]


def single_link_matrix(o):
    rows = 0
    for pred in ('fine', 'hier', 'collect', 'union'):
        modes = (0, 1) if pred == 'fine' else (0,)
        targets = (None, 0) if pred == 'fine' else (None,)
        queries = [0x02000002, 0x04000004] if pred != 'union' else [0]
        for kind, active, live, flags, cat, mover, mode, target, query in itertools.product(
                (0, 1, 2, 3), (False, True), ('live', 'dead', 'collide'), FLAGS, (0, 0x02, 0xca, 0x04),
                (True, False), modes, targets, queries):
            if pred == 'hier':
                query = {0x02000002: 0x06000006}.get(query, query)
            o.run(pred, [(kind, 0)], [Obj(cat, active, live, flags, mover)], query, mode, target)
            rows += 1
    return rows


def two_record_matrix(o):
    """All ordered chains of length 1..3 over two objects with removal/insertion records."""
    rows = 0
    variants = [Obj(0xca), Obj(0xc2, flags=0x10000000, mover=False), Obj(0xca, flags=0x20000000),
                Obj(0xca, flags=1), Obj(0x00), Obj(0xc2, flags=0x10000001, mover=False)]
    for length in (1, 2, 3):
        for chain in itertools.product([(0, 0), (1, 0), (0, 1), (1, 1)], repeat=length):
            for a, b in itertools.product(range(len(variants)), repeat=2):
                objs = [variants[a], variants[b]]
                for pred in ('fine', 'hier', 'collect', 'union'):
                    query = {'hier': 0x06000006, 'union': 0}.get(pred, 0x02000002)
                    for mode in ((0, 1) if pred == 'fine' else (0,)):
                        o.run(pred, list(chain), objs, query, mode, 1 if pred == 'fine' else None)
                        rows += 1
    return rows


def link_cap(o):
    """148e90 examines at most 49 links; the others have no cap."""
    out = []
    blocker = Obj(0xc2, flags=0x10000000, mover=False)
    fillers = {
        'inactive static (CTriggerRegion-like, +34 active bit clear)': Obj(0, active=False, flags=0x10000000, mover=False),
        'removal records of a live unit': None,
        'search metadata': 'meta',
    }
    for name, filler in fillers.items():
        for n in (47, 48, 49, 50):
            if filler == 'meta':
                links, objs = [(2, None)] * n + [(1, 0)], [blocker]
            elif filler is None:
                links, objs = [(0, 1)] * n + [(1, 0)], [blocker, Obj(0xca)]
            else:
                links, objs = [(1, 1)] * n + [(1, 0)], [blocker, filler]
            res = {}
            for pred, query in (('fine', 0x02000002), ('hier', 0x06000006), ('collect', 0x02000002), ('union', 0)):
                if filler == 'meta':
                    # metadata links need no object; model index None is skipped before object access
                    res[pred] = o.run(pred, [(k, 0 if v is None else v) for k, v in links], objs, query)
                else:
                    res[pred] = o.run(pred, links, objs, query)
            out.append(dict(filler=name, links_before_blocker=n, blocker_position=n + 1,
                            fine_clear=res['fine']['clear'], hierarchy_clear=res['hier']['clear'],
                            collector_tokens=len(res['collect']['tokens']), union=hex(res['union']['union'])))
    return out


def composed(o):
    """Consumers that wrap 1489a0 / 148e90: geometry, mode and suppression at the call sites."""
    r, e = o.r, o.e
    out = dict(perimeter=[], segments=[], footprints=[], point_query=[], hierarchy=[])
    unit_obj, unit_mover = o.pool[0]
    region = r.new_static_object(0)
    kinds = {
        'unit foot ca': (unit_obj, 0x010000ca, 0),
        'unit moving ca': (unit_obj, 0x010000ca, 0x20000000),
        'unit group-transient ca': (unit_obj, 0x010000ca, 0x40000000),
        'unit suppressed ca': (unit_obj, 0x010000ca, 1),
        'flyer 0': (unit_obj, 0x01000000, 0),
        'unbuild/landmine 08': (unit_obj, 0x01000008, 0),
        'item 18': (unit_obj, 0x01000018, 0),
        'captain virtual c2': (unit_obj, 0x010000c2, 0),
        'widget region c2': (region, 0x010000c2, 0x10000000),
        'widget region 10': (region, 0x01000010, 0x10000000),
        'widget region 08': (region, 0x01000008, 0x10000000),
        'widget region 04': (region, 0x01000004, 0x10000000),
        'widget region suppressed c2': (region, 0x010000c2, 0x10000001),
        'trigger region (inactive)': (region, 0x00000000, 0x10000000),
    }
    cx, cy = 6, 6
    for name, (obj, w34, w40) in kinds.items():
        r.reset_map()
        e.w(obj + 0x34, w34)
        e.w(obj + 0x38, 0)
        e.w(obj + 0x3c, 0)
        e.w(obj + 0x40, w40)
        r.record(cx, cy, obj, True)
        lane_results = {}
        for qname, query in QUERIES.items():
            row = dict(kind=name, query=qname)
            # fine search expansion around node (cx+1,cy) for class 0 (offset1,width3) in mode 0
            row['perimeter_class0_mode0'] = r.perimeter(cx + 1, cy, 1, 3, query, 0)
            row['perimeter_class0_mode1'] = r.perimeter(cx + 1, cy, 1, 3, query, 1)
            for klass, entry in enumerate((0x6f149440, 0x6f149630, 0x6f149970, 0x6f149cc0)):
                row[f'segment_class{klass}_full_mode0'] = r.segment(entry, cx, cy, 0, query, 0)
                row[f'footprint_class{klass}_mode1'] = r.footprint(cx, cy, query, klass, 1)
                row[f'footprint_class{klass}_mode0'] = r.footprint(cx, cy, query, klass, 0)
            row['point_query_blocked'] = r.point_query(cx, cy, query, 0)
            lane_results[qname] = row
            out['perimeter'].append(row)
        r.rebuild_hierarchy()
        out['hierarchy'].append(dict(kind=name, level0=r.hier_class(cx, cy, 0), level1=r.hier_class(cx, cy, 1)))
    # self-suppression through the real bridge (05bd30) in 04df50
    bridge = r.register_mover(unit_mover)
    e.w(unit_mover + 0x98, unit_obj)
    for w40 in (0, 0x20000000):
        r.reset_map()
        e.w(unit_obj + 0x34, 0x010000ca)
        e.w(unit_obj + 0x38, 0)
        e.w(unit_obj + 0x40, w40)
        r.record(cx, cy, unit_obj, True)
        out['point_query'].append(dict(flags40=hex(w40), no_bridge=r.point_query(cx, cy, 0x02000002, 0),
                                       own_bridge=r.point_query(cx, cy, 0x02000002, bridge),
                                       flags40_after=hex(e.r(unit_obj + 0x40))))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    o = Oracle(args.binary)
    single = single_link_matrix(o)
    double = two_record_matrix(o)
    cap = link_cap(o)
    comp = composed(o)
    here = Path(__file__).resolve().parent
    report = dict(
        task='FOOT-03.1', binary_sha256=o.e.binary_sha256,
        sources={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in
                 (Path(__file__).resolve(), here / 'foot03_rig.py', here / 'foot03_spatial_harness_copy.py')},
        fixture_notes=FIXTURE_NOTES, storm_import_calls=len(o.e.log),
        cases=o.cases, single_link_cases=single, chain_cases=double, mismatches=o.mismatches,
        link_cap=cap, composed=comp)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(report, indent=1, sort_keys=True) + '\n'
    args.report.write_text(text)
    print(f'{o.cases} predicate cases ({single} single-link, {double} chain); {len(o.mismatches)} mismatches; '
          f'report sha256 {hashlib.sha256(text.encode()).hexdigest()}')
    return 1 if o.mismatches else 0


if __name__ == '__main__':
    raise SystemExit(main())
