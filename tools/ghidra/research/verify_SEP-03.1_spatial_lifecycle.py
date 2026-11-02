#!/usr/bin/env python3
"""SEP-03.1: original spatial insertion/removal in both orders, metadata/dead records,
cleanup thresholds and maintenance cadence.

Everything runs through original constructors and mutators (PathRegistry_Construct,
PathOwner_Construct, spatial-map ctor 6f14c280 + init 6f14c990, object factory 6f14cf20,
6f14e770 rectangle update, 6f14dae0 retire, 6f14d890 metadata link, 6f14df20/6f14dfc0
compaction, 6f170c00 separation query, maintenance timer drain 6f0523d0). Only the three
Storm memory imports receive host storage. An independent Python model predicts every
chain, count, free list and destruction point; disagreement aborts.
"""
import argparse
import itertools
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sep03_map05_spatial_harness as H  # noqa: E402

W = 8


def cells_of(rect):
    y0, x0, y1, x1 = rect
    return [y * W + x for y in range(max(0, y0), min(W, y1)) for x in range(max(0, x0), min(W, x1))]


class Model:
    """Independent prediction of lazy chains (newest first), counts and compaction."""

    def __init__(self):
        self.chains = {c: [] for c in range(W * W)}
        self.rect = {}
        self.records = 0
        self.per_obj = {}
        self.dirty = set()
        self.dead = set()
        self.flag = set()

    def emit(self, rect, obj, kind):
        for c in cells_of(rect):
            self.chains[c].insert(0, (kind, obj))
            self.records += 1
            self.per_obj[obj] = self.per_obj.get(obj, 0) + 1
            if kind == 0:
                self.dirty.add(c)

    def update(self, obj, new):
        old = self.rect.get(obj, (-1, -1, -1, -1))
        if old == new:
            return
        oc, nc = set(cells_of(old)), set(cells_of(new))
        for c in sorted(oc - nc):
            self.chains[c].insert(0, (0, obj)); self.records += 1; self.dirty.add(c)
            self.per_obj[obj] = self.per_obj.get(obj, 0) + 1
        for c in sorted(nc - oc):
            self.chains[c].insert(0, (1, obj)); self.records += 1
            self.per_obj[obj] = self.per_obj.get(obj, 0) + 1
        self.rect[obj] = new

    def metadata(self, cell, payload):
        self.chains[cell].insert(0, (2, payload)); self.records += 1; self.dirty.add(cell)

    def retire(self, obj):
        if obj not in self.flag:
            self.emit(self.rect.get(obj, (-1, -1, -1, -1)), obj, 0)
        self.dead.add(obj)
        return self.per_obj.get(obj, 0) == 0     # destroyed immediately

    def compact(self, cells):
        destroyed = []
        for c in cells:
            kept, seen = [], set()
            for kind, payload in self.chains[c]:
                if kind == 1 and payload not in seen and payload not in self.dead:
                    kept.append((kind, payload)); seen.add(payload)
                else:
                    self.records -= 1
                    if kind != 2:
                        self.per_obj[payload] -= 1
                        if payload in self.dead and self.per_obj[payload] == 0:
                            destroyed.append((c, payload))
                        if payload not in self.dead:
                            seen.add(payload)
            self.chains[c] = kept
        return destroyed


def snapshot(w, names):
    e = w.e
    chains = {}
    for c in range(W * W):
        ch = w.chain(c)
        if ch:
            chains[c] = [[i, k, names.get(p, f'meta:{p:08x}')] for i, k, p in ch]
    f = w.fields()
    return dict(chains=chains, records=f['records'], link_count=f['link_count'], link_capacity=f['link_capacity'],
                free_list=w.free_list(), dirty=w.dirty(), stamp=f['stamp'],
                objects={n: dict(refs=e.r(o + 0x3c), stamp=e.r(o + 0x38), rect=[v - (1 << 32) if v >> 31 else v for v in e.r(o + 0x1c, 4)],
                                 flags=e.r(o + 0x40)) for o, n in names.items() if not str(n).startswith('meta')})


def check(w, model, names):
    inv = {v: k for k, v in names.items()}
    for c in range(W * W):
        actual = [(k, p) for _, k, p in w.chain(c)]
        expect = [(k, inv[p] if k != 2 else p) for k, p in model.chains[c]]
        assert actual == expect, ('chain', c, actual, expect)
    assert w.fields()['records'] == model.records
    assert set(w.dirty()) == model.dirty, (w.dirty(), model.dirty)


def run_order_case(binary, insert_order, remove_order, remove_mode):
    e = H.Emu(binary)
    e.call(0x6f003c40)  # original static initializer of the maintenance period
    w = H.World(e, W, W)
    destroyed, compacting = [], [None]
    e.uc.hook_add(__import__('unicorn').UC_HOOK_CODE,
                  lambda uc, a, s, d: compacting.__setitem__(0, e.r(uc.reg_read(e.X.UC_X86_REG_ESP) + 4)),
                  begin=0x6f14e050, end=0x6f14e050)
    e.uc.hook_add(__import__('unicorn').UC_HOOK_CODE,
                  lambda uc, a, s, d: destroyed.append((compacting[0], uc.reg_read(e.X.UC_X86_REG_ECX))),
                  begin=0x6f14d750, end=0x6f14d750)
    movers = {n: w.make_mover() for n in 'PQR'}
    objs, names = {}, {}
    rects = dict(P=(2, 2, 4, 4), Q=(2, 2, 5, 5), R=(3, 0, 6, 3))
    model = Model()
    steps = []
    for n in insert_order + 'R':
        objs[n] = w.create_object(movers[n])
        names[objs[n]] = n
    for n in insert_order + 'R':
        w.update(objs[n], rects[n]); model.update(n, rects[n]); check(w, model, names)
        steps.append(dict(op=f'insert {n} {list(rects[n])}', **snapshot(w, names)))
    query_after_inserts = [names[o] for o in w.query((0, 0, 8, 8))]
    for x, y, lo, hi in ((2, 2, 0x11, 0x22), (3, 3, 0x33, 0x44)):
        w.metadata(x, y, lo, hi)
        payload = (hi << 16) | lo
        model.metadata(y * W + x, payload)
        names[payload] = f'meta:{payload:08x}'
        check(w, model, names)
        steps.append(dict(op=f'metadata ({x},{y}) {lo:#x}/{hi:#x}', **snapshot(w, names)))
    # Overlap-preserving move keeps P's records in retained cells; then a move that leaves
    # and re-enters cell 18 re-prepends P ahead of Q.
    w.update(objs['P'], (2, 2, 4, 5)); model.update('P', (2, 2, 4, 5)); check(w, model, names)
    steps.append(dict(op='move P [2,2,4,5] (retains cell 18)', **snapshot(w, names)))
    query_after_retaining_move = [names[o] for o in w.query((0, 0, 8, 8))]
    w.update(objs['P'], (2, 3, 4, 5)); model.update('P', (2, 3, 4, 5)); check(w, model, names)
    w.update(objs['P'], (2, 2, 4, 5)); model.update('P', (2, 2, 4, 5)); check(w, model, names)
    steps.append(dict(op='move P [2,3,4,5] then back [2,2,4,5] (leave/re-enter cell 18)', **snapshot(w, names)))
    query_before = [names[o] for o in w.query((0, 0, 8, 8))]
    for n in remove_order:
        if remove_mode == 'empty-then-retire':
            w.update(objs[n], (-1, -1, -1, -1)); model.update(n, (-1, -1, -1, -1)); check(w, model, names)
            steps.append(dict(op=f'empty {n}', **snapshot(w, names)))
        before = len(destroyed)
        immediate = model.retire(n)
        w.retire(objs[n])
        assert (len(destroyed) > before) == immediate
        check(w, model, names)
        steps.append(dict(op=f'retire {n}', destroyed_now=len(destroyed) > before, **snapshot(w, names)))
    query_after_removal = [names[o] for o in w.query((0, 0, 8, 8))]
    expected_destroyed = model.compact(sorted(model.dirty))
    model.dirty = set()
    destroyed.clear()
    w.compact_dirty()
    check(w, model, names)
    assert [(c, names[o]) for c, o in destroyed] == expected_destroyed, (destroyed, expected_destroyed)
    steps.append(dict(op='dirty compaction', destroyed=[[c, names[o]] for c, o in destroyed],
                      pool=w.pool_state(), **snapshot(w, names)))
    query_after_cleanup = [names[o] for o in w.query((0, 0, 8, 8))]
    w.compact_all()
    check(w, model, names)
    steps.append(dict(op='full compaction', **snapshot(w, names)))
    return dict(insert_order=insert_order, remove_order=remove_order, remove_mode=remove_mode,
                query_after_inserts=query_after_inserts, query_after_retaining_move=query_after_retaining_move,
                query_before_removal=query_before,
                free_list_after_cleanup=steps[-2]['free_list'], destroyed_order=steps[-2]['destroyed'], query_after_removal=query_after_removal,
                query_after_cleanup=query_after_cleanup, steps=steps,
                pool_recycled=[names.get(p, hex(p)) for p in w.pool_state()['recycled']])


def run_lifetime_cases(binary):
    import unicorn
    out = {}
    e = H.Emu(binary)
    e.call(0x6f003c40)
    w = H.World(e, W, W)
    destroyed = []
    e.uc.hook_add(unicorn.UC_HOOK_CODE, lambda uc, a, s, d: destroyed.append(uc.reg_read(e.X.UC_X86_REG_ECX)),
                  begin=0x6f14d750, end=0x6f14d750)
    # (a) never inserted -> destroyed inside retire (6f14dab0 at 6f14db23).
    a = w.create_object(w.make_mover())
    w.retire(a)
    out['never_inserted'] = dict(destroyed_in_retire=destroyed == [a], pool_head_is_object=w.pool_state()['recycled'][:1] == [a])
    # (b) region-flagged object (+40 bit 0x10000000): retire emits no removal records, no dirty bits;
    # dirty compaction cannot see its records; the full sweep reclaims them and destroys it.
    destroyed.clear()
    b = w.create_object(w.make_mover())
    w.update(b, (0, 0, 2, 2))
    e.w(b + 0x40, 0x10000000)
    w.compact_dirty()
    before = w.fields()['records']
    w.retire(b)
    records_after_retire = w.fields()['records']
    dirty_after = w.dirty()
    w.compact_dirty()
    lingering = [c for c in range(W * W) if w.chain(c)]
    destroyed_after_dirty = list(destroyed)
    query_skips_dead = w.query((0, 0, 8, 8)) == []
    w.compact_all()
    out['flagged_region_style'] = dict(records_before=before, records_after_retire=records_after_retire,
                                       dirty_after_retire=dirty_after, chains_after_dirty_compaction=lingering,
                                       destroyed_after_dirty=[hex(x) for x in destroyed_after_dirty],
                                       destroyed_after_full=destroyed == [b], query_skips_dead_records=query_skips_dead,
                                       records_after_full=w.fields()['records'])
    return out


def run_thresholds(binary):
    rows = []
    for start in (0x7ffffffe, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff):
        for driver in ('dirty', 'all'):
            e = H.Emu(binary)
            w = H.World(e, W, W)
            o = w.create_object(w.make_mover())
            w.update(o, (0, 0, 1, 3))      # 3 insertion records, cells 0..2, none dirty
            q = w.create_object(w.make_mover())
            w.update(q, (0, 0, 1, 1)); w.update(q, (0, 1, 1, 2))  # removal in cell 0 -> dirty {0}
            e.w(w.map + 0xb4, start)
            (w.compact_dirty if driver == 'dirty' else w.compact_all)()
            rows.append(dict(start=f'{start:08x}', driver=driver, final=f"{w.fields()['stamp']:08x}",
                             object_stamps=[f'{e.r(x + 0x38):08x}' for x in (o, q)], dirty_after=w.dirty()))
    return rows


def run_cadence(binary):
    e = H.Emu(binary)
    e.call(0x6f003c40)
    period_word = e.r(0x6fd53a4c)
    floor_word = e.r(0x6fcd539c)
    w = H.World(e, W, W)
    req = w.fields()['timer_request']
    clock = w.owner + 0x164
    request = dict(deadline=f'{e.r(req + 4):08x}', period=f'{e.r(req + 8):08x}', clock=e.r(req + 0xc) == clock,
                   flags=f'{e.r(req + 0x10):08x}', sequence=e.r(req + 0x14), map=e.r(req + 0x18) == w.map,
                   kind=e.r(req + 0x1c))
    import unicorn
    fired = []
    e.uc.hook_add(unicorn.UC_HOOK_CODE, lambda uc, a, s, d: fired.append(e.r(clock + 0x40)),
                  begin=0x6f14df20, end=0x6f14df20)
    rounds = []
    for _ in range(12):
        deadline = e.r(req + 4)
        e.w(clock + 0x40, deadline - 1)            # one ulp before: no callback
        e.call(0x6f0523d0, clock)
        early = len(fired)
        e.w(clock + 0x40, deadline)
        e.call(0x6f0523d0, clock)
        rounds.append(dict(deadline=f'{deadline:08x}', deadline_value=struct.unpack('<f', struct.pack('<I', deadline))[0],
                           early_callbacks=early, due_callbacks=len(fired) - early, next=f'{e.r(req + 4):08x}',
                           flags=f'{e.r(req + 0x10):08x}'))
        fired.clear()
    return dict(period_word=f'{period_word:08x}', period_value=struct.unpack('<f', struct.pack('<I', period_word))[0],
                floor_word=f'{floor_word:08x}', initial_request=request, rounds=rounds)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--expected', type=Path, help='freeze/compare expected-SEP-03.1.json')
    args = ap.parse_args()
    cases = [run_order_case(args.binary, i, r, m)
             for i, r, m in itertools.product(('PQ', 'QP'), ('PQ', 'QP'), ('retire', 'empty-then-retire'))]
    payload = dict(binary_sha256=H.SHA256, order_cases=cases, lifetime=run_lifetime_cases(args.binary),
                   thresholds=run_thresholds(args.binary), cadence=run_cadence(args.binary),
                   scope=__doc__.strip())
    if args.expected:
        if args.expected.exists():
            if json.loads(args.expected.read_text()) != json.loads(json.dumps(payload)):
                raise SystemExit('expected-SEP-03.1.json differs')
        else:
            H.dump(args.expected, payload)
    summary = dict(passed=True, order_cases=len(cases), lifetime=payload['lifetime'], thresholds=payload['thresholds'],
                   cadence=payload['cadence'])
    args.report.write_text(json.dumps(dict(summary, payload_steps=sum(len(c['steps']) for c in cases)), indent=1) + '\n')
    print(json.dumps(summary, indent=1))


if __name__ == '__main__':
    main()
