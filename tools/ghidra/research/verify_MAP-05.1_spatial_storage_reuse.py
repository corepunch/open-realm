#!/usr/bin/env python3
"""MAP-05.1: cross the spatial link-vector and object-pool allocation boundaries, free the
storage and reuse it; assert record identity (link index / object address), links and cells.

All operations are original game.dll code: map factory 6f14efe0 (owner+598 pool), object
factory 6f14cf20 (owner+5d8 pool, 64x72-byte blocks), 6f14e770 updates, 6f14dae0 retire,
6f14df20 compaction, map virtual release 6f14cac0, maintenance drain 6f0523d0, separation
query 6f170c00. Only the Storm memory imports receive host storage. The final section
(stale object across map release) is reachable only if a spatial object outlives
PathMaps_Release; MAP-06.1 measures whether retail does that.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sep03_map05_spatial_harness as H  # noqa: E402

SIZE = 64


def records_of(w, obj):
    return {cell: [i for i, k, p in w.chain(cell) if p == obj] for cell in range(w.width * w.height)
            if any(p == obj for _, _, p in w.chain(cell))}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--expected', type=Path)
    args = ap.parse_args()
    e = H.Emu(args.binary)
    e.call(0x6f003c40)
    w = H.World(e, SIZE, SIZE, factory=True)
    first_map = w.map
    out = dict(binary_sha256=H.SHA256, map_identity=dict(first=f'{first_map:08x}', pool_after_create=w.map_pool()))
    # ---- A. link-vector boundary, reclamation, identity-preserving reuse --------------
    H.fill_links(w, 0x20000 - 18)              # A+B take 13, C crosses after 5 more
    a, b = (w.create_object(w.make_mover()) for _ in range(2))
    names = {a: 'A', b: 'B'}
    w.update(a, (20, 20, 22, 22)); w.update(b, (20, 20, 23, 23))      # 13 records
    c = w.create_object(w.make_mover()); names[c] = 'C'
    log0 = len(e.log)
    w.update(c, (19, 19, 25, 25))
    growth = [dict(op=r['op'], size=r['size'], previous=r['previous'], caller=f"{r['caller']:08x}") for r in e.log[log0:]]
    c_indices = records_of(w, c)
    w.retire(c)
    removal_indices = {cell: [i for i, k, p in w.chain(cell) if p == c and k == 0] for cell in c_indices}
    # Independent prediction of the reclamation sequence of 6f14df20 (ascending dirty cells,
    # newest-first walk, first effective live insertion kept), hence the LIFO free list.
    expected_reclaim, dirty_cells = [], w.dirty()
    for cell in dirty_cells:
        seen = set()
        for i, k, p in w.chain(cell):
            live = k != 2 and e.r(p + 0x38) != 0xffffffff
            if k == 1 and live and p not in seen:
                seen.add(p)
            else:
                expected_reclaim.append(i)
                if live:
                    seen.add(p)
    old_free = w.free_list()
    w.compact_dirty()
    free_after = w.free_list()
    assert free_after == expected_reclaim[::-1] + old_free, 'free-list prediction differs'
    reclaimed = set(expected_reclaim)
    pool_after_retire = w.pool_state()
    d = w.create_object(w.make_mover()); names[d] = 'D'
    log1 = len(e.log)
    w.update(d, (19, 19, 25, 25))
    d_indices = records_of(w, d)
    emission = [d_indices[cell][0] for cell in sorted(d_indices)]
    a_out = dict(growth=growth, capacity=w.fields()['link_capacity'], high_water=w.fields()['link_count'],
                 c_records={str(k): v for k, v in c_indices.items()},
                 c_removal_records={str(k): v for k, v in removal_indices.items()},
                 free_list_after_cleanup_head=free_after[:80], free_list_length=len(free_after),
                 dirty_cells_compacted=len(dirty_cells), reclaimed_count=len(expected_reclaim),
                 reclaimed_equals_c_records=set(sum(c_indices.values(), []) + sum(removal_indices.values(), [])) <= reclaimed,
                 c_object_recycled=pool_after_retire['recycled'][:1] == [c],
                 d_object_is_c_memory=d == c,
                 d_records={str(k): v for k, v in d_indices.items()},
                 d_takes_free_list_in_emission_order=emission == free_after[:36],
                 storm_calls_during_reuse=len(e.log) - log1,
                 cells=[dict(cell=cell, chain=[[i, k, names.get(p, 'filler')] for i, k, p in w.chain(cell)])
                        for cell in (20 * SIZE + 20, 20 * SIZE + 22, 24 * SIZE + 24)],
                 query=[names.get(o, 'filler') for o in w.query((18, 18, 26, 26))])
    assert a_out['reclaimed_equals_c_records'] and a_out['d_takes_free_list_in_emission_order']
    assert a_out['storm_calls_during_reuse'] == 0 and a_out['d_object_is_c_memory']
    out['link_boundary_and_reuse'] = a_out
    # ---- B. object-pool block boundary and reuse ------------------------------------
    pool0 = w.pool_state()
    created, blocks = [], []
    while len(blocks) < 2 and len(created) < 200:
        mark = len(e.log)
        created.append(w.create_object(w.make_mover()))
        for r in e.log[mark:]:
            if r['op'] == 'alloc':
                blocks.append(dict(size=r['size'], caller=f"{r['caller']:08x}", at_object=len(created),
                                   raw_allocated_after=w.pool_state()['raw_allocated'],
                                   object_offset_in_block=created[-1] - r['result']))
    # Free three of the new objects (no records: destroyed inside retire) and reuse them.
    for o in created[-3:]:
        w.retire(o)
    recycled = w.pool_state()['recycled']
    again = [w.create_object(w.make_mover()) for _ in range(4)]
    out['object_block_boundary'] = dict(pool_before=dict((k, pool0[k]) for k in ('raw_allocated', 'live', 'created')),
                                        objects_created=len(created), block_allocations=blocks,
                                        recycled_after_retire=[created.index(x) for x in recycled],
                                        reuse_order=[created.index(x) if x in created else 'raw' for x in again],
                                        pool_after={k: v for k, v in w.pool_state().items() if k not in ('recycled', 'block_head', 'raw_free_head')})
    # ---- C. whole-map release and pool reuse ----------------------------------------
    req_old = w.fields()['timer_request']
    for o in (a, b, d):
        out.setdefault('live_objects_before_release', {})[names[o]] = dict(refs=e.r(o + 0x3c), map=e.r(o + 0x2c) == first_map)
    log3 = len(e.log)
    w.release_map()
    released = dict(storm=[dict(op=r['op'], size=r.get('size'), caller=f"{r['caller']:08x}") for r in e.log[log3:]],
                    old_request_flags=f'{e.r(req_old + 0x10):08x}', map_timer_field=e.r(first_map + 0xb8),
                    map_pool=w.map_pool(), link_fields={k: w.fields()[k] for k in ('link_capacity', 'link_count', 'records', 'free_head')},
                    stale_object_refs={names[o]: e.r(o + 0x3c) for o in (a, b, d)})
    w.map = w.factory_map(32, 32)
    reused = dict(second=f'{w.map:08x}', same_identity=w.map == first_map, fields=w.fields(), map_pool=w.map_pool(),
                  new_request=f"{w.fields()['timer_request']:08x}", new_request_is_old=w.fields()['timer_request'] == req_old)
    # Drain the clock through the old and new deadlines: a cancelled request must not compact.
    import unicorn
    fired = []
    e.uc.hook_add(unicorn.UC_HOOK_CODE, lambda uc, ad, s, dd: fired.append(uc.reg_read(e.X.UC_X86_REG_ECX)),
                  begin=0x6f14df20, end=0x6f14df20)
    clock = w.owner + 0x164
    deadline = max(e.r(req_old + 4), e.r(w.fields()['timer_request'] + 4))
    e.w(clock + 0x40, deadline)
    e.call(0x6f0523d0, clock)
    reused['callbacks_after_drain'] = [f'{m:08x}' for m in fired]
    reused['old_request_flags_after_drain'] = f'{e.r(req_old + 0x10):08x}'
    out['map_release_and_reuse'] = dict(released=released, reused=reused)
    # ---- D. stale object outliving the release (hazard probe) -----------------------
    before = w.fields()['records']
    w.retire(a)
    stale = dict(records_in_new_map_after_stale_retire=w.fields()['records'] - before,
                 dirty=w.dirty(), a_refs_after_retire=e.r(a + 0x3c))
    w.compact_dirty()
    stale['a_refs_after_compaction'] = e.r(a + 0x3c)
    stale['a_recycled'] = a in w.pool_state()['recycled']
    out['stale_object_across_release'] = stale
    out['scope'] = __doc__.strip()
    payload = json.loads(json.dumps(out))
    if args.expected:
        if args.expected.exists():
            if json.loads(args.expected.read_text()) != payload:
                raise SystemExit('expected-MAP-05.1.json differs')
        else:
            H.dump(args.expected, payload)
    args.report.write_text(json.dumps(dict(passed=True, **payload), indent=1) + '\n')
    print(json.dumps({k: v for k, v in payload.items() if k not in ('scope', 'link_boundary_and_reuse')}, indent=1)[:6000])
    print(json.dumps({k: v for k, v in a_out.items() if k in ('growth', 'capacity', 'high_water', 'free_list_length', 'query', 'storm_calls_during_reuse')}))


if __name__ == '__main__':
    main()
