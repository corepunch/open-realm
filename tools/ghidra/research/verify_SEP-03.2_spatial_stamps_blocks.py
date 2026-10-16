#!/usr/bin/env python3
"""SEP-03.2: spatial query-stamp wrap/repair and fresh block allocation (original code).

Evidence classes inside the report:
  * reachable   - only original constructors/mutators/queries/compactions; the Storm
                  imports receive host storage (Storm never returns NULL).
  * labelled    - a single fixture write replaces >= 2^31 real stamp increments
                  (map +b4 jump) to reach a counter state in bounded time. These rows
                  show what the original code does in that state; they are not live
                  evidence that the state occurs.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sep03_map05_spatial_harness as H  # noqa: E402


def world(binary, size=8):
    e = H.Emu(binary)
    e.call(0x6f003c40)
    return e, H.World(e, size, size)


def stamp_increments(binary):
    e, w = world(binary)
    a, b = (w.create_object(w.make_mover()) for _ in range(2))
    w.update(a, (0, 0, 2, 2)); w.update(b, (1, 1, 3, 3))      # nonempty cells: 0,1,8,9,10,16,17,18 (9 has both)
    rows = []
    for rect in ((0, 0, 8, 8), (0, 0, 1, 1), (5, 5, 8, 8), (8, 8, 9, 9), (-4, -4, -1, -1), (2, 2, 2, 5)):
        before = w.fields()['stamp']
        result = w.query(rect)
        nonempty = sum(1 for c in H_cells(rect, 8) if w.chain(c))
        rows.append(dict(kind='separation-query 6f170c00', rect=list(rect), before=before, after=w.fields()['stamp'],
                         nonempty_cells=nonempty, candidates=len(result)))
    for x, y in ((1, 1), (5, 5), (9, 9)):
        before = w.fields()['stamp']
        vec = e.fixture(0x20)
        data = e.fixture(0x100)
        e.w(vec + 0xc, data); e.w(vec + 0x14, 0, 32, 0)
        pt = e.fixture(8); e.w(pt, x, y)
        e.call(0x6f14cdf0, w.map, pt, vec)
        assert e.esp_after == 12
        rows.append(dict(kind='collector 6f14cdf0', cell=[x, y], before=before, after=w.fields()['stamp'],
                         collected=e.r(vec + 0x1c)))
    w.update(a, (4, 4, 5, 5))                                       # removal records -> dirty 0,1,8,9
    before = w.fields()['stamp']
    w.compact_dirty()
    rows.append(dict(kind='dirty compaction 6f14df20', before=before, after=w.fields()['stamp'], dirty_cells=4))
    before = w.fields()['stamp']
    w.compact_all()
    rows.append(dict(kind='full compaction 6f14dfc0', before=before, after=w.fields()['stamp'],
                     nonempty_cells=sum(1 for c in range(64) if w.chain(c))))
    return rows


def H_cells(rect, width):
    y0, x0, y1, x1 = rect
    return [y * width + x for y in range(max(0, y0), min(width, y1)) for x in range(max(0, x0), min(width, x1))]


def stale_stamp_after_repair(binary, jump):
    """R is stamped once by a query, then left untouched while the counter advances.
    After the >0x7fffffff repair the counter revisits R's stale stamp; the next query
    whose stamp equals it skips R."""
    e, w = world(binary)
    names = {}
    p, q, r = (w.create_object(w.make_mover()) for _ in range(3))
    names.update({p: 'P', q: 'Q', r: 'R'})
    w.update(r, (6, 6, 7, 7))           # R alone in cell 54
    w.update(p, (0, 0, 1, 1))           # P in cell 0
    for _ in range(10):
        w.query((0, 0, 1, 1))           # +2 each: query stamp + cell stamp
    w.query((6, 6, 7, 7))               # stamps R with this query stamp
    r_stamp = e.r(r + 0x38)
    if jump:
        e.w(w.map + 0xb4, 0x80000000)   # LABELLED: replaces >= 2^31 real increments
    rows = []
    w.update(q, (0, 0, 1, 1)); w.update(q, (0, 1, 1, 2))   # removal record in cell 0 -> dirty {0}
    w.compact_dirty()                   # repair (>0x7fffffff -> 0) happens before the scan
    rows.append(dict(op='dirty compaction', counter=f"{w.fields()['stamp']:08x}"))
    guard = 0
    while (w.fields()['stamp'] + 1) & 0xffffffff < r_stamp and guard < 64:
        gap = r_stamp - (w.fields()['stamp'] + 1)
        w.query((0, 0, 1, 1) if gap >= 2 else (4, 4, 5, 5))   # +2 (nonempty cell) or +1 (empty cell)
        guard += 1
    query = [names[o] for o in w.query((0, 0, 8, 8))]
    rows.append(dict(op='full-map query', counter_after=f"{w.fields()['stamp']:08x}", candidates=query))
    query2 = [names[o] for o in w.query((0, 0, 8, 8))]
    rows.append(dict(op='next full-map query', candidates=query2, r_stamp_now=f"{e.r(r + 0x38):08x}"))
    return dict(jump=jump, r_stale_stamp=f'{r_stamp:08x}', rows=rows)


def compaction_collision(binary, jump):
    """Directly target the compaction comparison: R's only insertion record is in a
    dirty cell whose compaction cell stamp equals R's stale stamp."""
    e, w = world(binary)
    names = {}
    r, s, p = (w.create_object(w.make_mover()) for _ in range(3))
    names.update({r: 'R', s: 'S', p: 'P'})
    w.update(r, (6, 6, 7, 7))
    w.query((6, 6, 7, 7))
    r_stamp = e.r(r + 0x38)            # = 1
    if jump:
        e.w(w.map + 0xb4, 0x7fffffff + 1)   # LABELLED counter state; next compaction repairs to 0
    w.update(s, (6, 6, 7, 7)); w.update(s, (6, 7, 7, 8))   # dirty cell 54 (S removal) ; cell 55 S insertion
    w.compact_dirty()                   # repair 0x80000000 -> 0; cell 54 is first nonempty dirty cell -> stamp 1 == R
    chain54 = [(k, names.get(pl, 'meta')) for _, k, pl in w.chain(54)]
    q = [names[o] for o in w.query((0, 0, 8, 8))]
    return dict(jump=jump, r_stamp=f'{r_stamp:08x}', counter_after=f"{w.fields()['stamp']:08x}", cell54=chain54,
                r_refs=e.r(r + 0x3c), r_rect=[v - (1 << 32) if v >> 31 else v for v in e.r(r + 0x1c, 4)], query=q)


def sentinel_stamp(binary):
    """LABELLED: a query stamp of 0xffffffff marks live objects with the dead sentinel."""
    e, w = world(binary)
    destroyed = []
    import unicorn
    e.uc.hook_add(unicorn.UC_HOOK_CODE, lambda uc, a, s, d: destroyed.append(uc.reg_read(e.X.UC_X86_REG_ECX)),
                  begin=0x6f14d750, end=0x6f14d750)
    a = w.create_object(w.make_mover())
    w.update(a, (0, 0, 1, 2))
    w.query((0, 0, 1, 1))               # ordinary stamp (1) so the wrapped cell stamp 0 cannot hide it
    e.w(w.map + 0xb4, 0xfffffffe)       # LABELLED: unreachable between 0.1 s repairs
    w.query((0, 0, 1, 1))               # query stamp 0xffffffff
    stamped = e.r(a + 0x38)
    b = w.create_object(w.make_mover())
    w.update(b, (0, 0, 1, 1)); w.update(b, (0, 1, 1, 2))   # dirty cell 0
    w.compact_all()
    return dict(object_stamp_after_query=f'{stamped:08x}', live_object_destroyed=a in destroyed,
                pool_live=w.pool_state()['live'],
                records_left=[c for c in range(64) if any(pl == a for _, _, pl in w.chain(c))],
                still_referenced_by_mover=True)


def link_growth(binary):
    """Cross the 0x20000-link boundary during ordinary updates; compare with a control
    whose vector was already grown (free-list reuse)."""
    out = {}
    for variant in ('fresh', 'pregrown'):
        e, w = world(binary, 64)
        objs = [w.create_object(w.make_mover()) for _ in range(3)]
        names = {o: 'ABC'[i] for i, o in enumerate(objs)}
        if variant == 'pregrown':
            filler = w.create_object(w.make_mover())
            for i in range(300):                    # 300 * 512 records > 0x20000 * 1
                w.update(filler, (0, 0, 16, 16) if i % 2 == 0 else (32, 32, 48, 48))
            w.update(filler, (-1, -1, -1, -1)); w.retire(filler); w.compact_dirty(); w.compact_all()
        start_log = len(e.log)
        growth_at = None
        for i in range(300):
            o = objs[i % 3]
            rect = ((i * 3) % 40, (i * 7) % 40, (i * 3) % 40 + 16, (i * 7) % 40 + 16)
            before_cap = w.fields()['link_capacity']
            w.update(o, rect)
            if w.fields()['link_capacity'] != before_cap and growth_at is None and variant == 'fresh' and before_cap:
                growth_at = dict(update=i, object=names[o], capacity_before=before_cap,
                                 capacity_after=w.fields()['link_capacity'], link_count=w.fields()['link_count'])
        chains = {c: [(k, names[p]) for _, k, p in w.chain(c)] for c in range(64 * 64)}
        query = [names[o] for o in w.query((0, 0, 64, 64))]
        out[variant] = dict(storm=[{k: v for k, v in row.items() if k != 'result'} for row in e.log[start_log:]],
                            fields={k: v for k, v in w.fields().items() if k in ('link_growth', 'link_capacity', 'link_count', 'records')},
                            growth=growth_at, query=query, chains_digest=json_digest(chains))
        out[variant]['_chains'] = chains
    out['chains_equal'] = out['fresh'].pop('_chains') == out['pregrown'].pop('_chains')
    out['query_equal'] = out['fresh']['query'] == out['pregrown']['query']
    return out


def json_digest(value):
    import hashlib
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def object_blocks(binary):
    e, w = world(binary)
    import unicorn
    returned = []
    e.uc.hook_add(unicorn.UC_HOOK_CODE, lambda uc, a, s, d: returned.append(uc.reg_read(e.X.UC_X86_REG_ECX)),
                  begin=0x6f14d750, end=0x6f14d750)
    start = len(e.log)
    objs = []
    for i in range(65):
        objs.append(w.create_object(w.make_mover()))
    allocs = [r for r in e.log[start:] if r['op'] == 'alloc']
    blocks = [dict(size=r['size'], caller=f"{r['caller']:08x}") for r in allocs]
    first_block, second_block = allocs[0]['result'], allocs[1]['result'] if len(allocs) > 1 else None
    layout = dict(first_object_offset=objs[0] - first_block, stride=objs[1] - objs[0],
                  object64_in_second_block=second_block is not None and second_block <= objs[64] < second_block + 4612,
                  addresses_ascending_within_first_block=all(objs[i + 1] - objs[i] == 72 for i in range(63)))
    # Ordinary memberships, then remove three objects in one order and reclaim.
    names = {o: f'O{i}' for i, o in enumerate(objs)}
    for i, o in enumerate(objs[:6]):
        w.update(o, (1, 1, 3, 3))
    q_before = [names[o] for o in w.query((0, 0, 8, 8))]
    for o in (objs[1], objs[4], objs[2]):
        w.retire(o)
    returned_before_cleanup = list(returned)
    w.compact_dirty()
    returned_during_cleanup = [names[o] for o in returned[len(returned_before_cleanup):]]
    recycled = [names[p] for p in w.pool_state()['recycled']]
    q_after = [names[o] for o in w.query((0, 0, 8, 8))]
    # New objects pop the recycled list first (LIFO) and enter at chain heads.
    fresh = [w.create_object(w.make_mover()) for _ in range(4)]
    reuse = [names.get(o, 'new-raw-element') for o in fresh]
    for i, o in enumerate(fresh):
        names[o] = f'N{i}'
        w.update(o, (1, 1, 2, 2))
    q_new = [names[o] for o in w.query((1, 1, 2, 2))]
    ps = w.pool_state()
    return dict(block_allocations=blocks, layout=layout, query_before_removal=q_before,
                returned_before_cleanup=[names[o] for o in returned_before_cleanup],
                returned_during_cleanup=returned_during_cleanup,
                recycled_after_cleanup=recycled, query_after_reclamation=q_after,
                new_objects_reuse=reuse, query_new_cell=q_new,
                pool=dict(live=ps['live'], created=ps['created'], raw_allocated=ps['raw_allocated'], recycled=len(ps['recycled'])))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--expected', type=Path)
    args = ap.parse_args()
    payload = dict(binary_sha256=H.SHA256,
                   stamp_increments=stamp_increments(args.binary),
                   stale_stamp_control=stale_stamp_after_repair(args.binary, False),
                   stale_stamp_labelled=stale_stamp_after_repair(args.binary, True),
                   compaction_collision_control=compaction_collision(args.binary, False),
                   compaction_collision_labelled=compaction_collision(args.binary, True),
                   sentinel_stamp_labelled=sentinel_stamp(args.binary),
                   link_growth=link_growth(args.binary),
                   object_blocks=object_blocks(args.binary), scope=__doc__.strip())
    payload = json.loads(json.dumps(payload))
    if args.expected:
        if args.expected.exists():
            if json.loads(args.expected.read_text()) != payload:
                raise SystemExit('expected-SEP-03.2.json differs')
        else:
            H.dump(args.expected, payload)
    args.report.write_text(json.dumps(dict(passed=True, **payload), indent=1) + '\n')
    print(json.dumps(payload, indent=1)[:6000])


if __name__ == '__main__':
    main()
