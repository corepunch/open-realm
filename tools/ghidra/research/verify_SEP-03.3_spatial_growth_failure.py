#!/usr/bin/env python3
"""SEP-03.3: spatial link growth during one rectangle update, and what a failure would do.

reachable  - growth crosses the 0x20000-link boundary in the middle of one original
             6f14e770 update (host storage behind Storm imports). Membership, chains and
             the subsequent separation query must equal a pre-grown control.
labelled   - INTERVENTIONS, not retail behaviour:
             (A) SMemReAlloc/SMemAlloc returns NULL at the boundary (retail Storm instead
                 reports ERROR_NOT_ENOUGH_MEMORY and calls ExitProcess(1));
             (B) map +80 growth increment forced to 0 so 6f14dde0 refuses the append
                 (no retail writer sets it to 0; ctor 6f14c090 stores 0x20000).
             Both record the exact partial state the original code leaves behind.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sep03_map05_spatial_harness as H  # noqa: E402

SIZE = 64
BIG = (0, 0, 16, 16)        # 256 cells
ALT = (32, 32, 48, 48)


def prepared(binary, pregrow):
    e = H.Emu(binary)
    e.call(0x6f003c40)
    w = H.World(e, SIZE, SIZE)
    filler = w.create_object(w.make_mover())
    if pregrow:
        for i in range(300):
            w.update(filler, BIG if i % 2 == 0 else ALT)
        w.update(filler, (-1, -1, -1, -1)); w.retire(filler); w.compact_dirty(); w.compact_all()
        filler = w.create_object(w.make_mover())
    # Fill the first 0x20000 links to exactly capacity-5 using whole-object moves (512 records each).
    target = 0x20000 - 18          # A+B add 13, leaving 5 free links before C's 36 insertions
    flip = 0
    while True:
        count = w.fields()['link_count'] if not pregrow else w.fields()['records']
        if count + 512 > target:
            break
        w.update(filler, BIG if flip == 0 else ALT); flip ^= 1
    # top up with single-cell moves (2 records each) then one insertion if odd
    small = w.create_object(w.make_mover())
    pos = 0
    w.update(small, (60, 0, 61, 1))
    while (w.fields()['link_count'] if not pregrow else w.fields()['records']) < target:
        remaining = target - (w.fields()['link_count'] if not pregrow else w.fields()['records'])
        if remaining == 1:
            extra = w.create_object(w.make_mover()); w.update(extra, (63, 63, 64, 64))
        else:
            pos ^= 1; w.update(small, (60, pos, 61, pos + 1))
    return e, w


def scenario(e, w):
    names = {}
    a, b, c = (w.create_object(w.make_mover()) for _ in range(3))
    names.update({a: 'A', b: 'B', c: 'C'})
    w.update(a, (20, 20, 22, 22)); w.update(b, (20, 20, 23, 23))
    before = dict(link_count=w.fields()['link_count'], capacity=w.fields()['link_capacity'], records=w.fields()['records'])
    start = len(e.log)
    w.update(c, (19, 19, 25, 25))           # 36 insertion records; crosses the boundary in fresh variant
    growth = [dict(op=r['op'], size=r['size'], previous=r['previous'], caller=f"{r['caller']:08x}") for r in e.log[start:]]
    after = dict(link_count=w.fields()['link_count'], capacity=w.fields()['link_capacity'], records=w.fields()['records'])
    member_cells = sorted(cell for cell in range(SIZE * SIZE) if any(p == c for _, _, p in w.chain(cell)))
    chains = {cell: [(k, names[p]) for _, k, p in w.chain(cell)] for cell in range(19 * SIZE, 25 * SIZE)
              if any(p in names for _, _, p in w.chain(cell))}
    query = [names[o] for o in w.query((18, 18, 26, 26))]
    return dict(before=before, after=after, storm=growth, c_member_cells=len(member_cells),
                c_refs=e.r(c + 0x3c), chains={str(k): v for k, v in chains.items()}, query=query)


def intervention_null(binary):
    e, w = prepared(binary, False)
    hits = []

    def fail(kind, size):
        if size >= 0x200000:
            hits.append(dict(kind=kind, size=size))
            return True
        return False
    e.fail = fail
    a = w.create_object(w.make_mover())
    before = w.fields()
    try:
        w.update(a, (19, 19, 25, 25))
        outcome = dict(returned=True)
    except RuntimeError as error:
        outcome = dict(returned=False, error=str(error).split(' {')[0], fault=e.fault and {k: f'{v:08x}' for k, v in e.fault.items()})
    link_ptr = e.r(w.map + 0x70)
    return dict(intervention='labelled: Storm import returns NULL (retail Storm terminates instead)', storm_calls=hits,
                link_count_before=before['link_count'], wrapper_pointer_after=f'{link_ptr:08x}',
                data_pointer_after=f"{e.r(w.map + 0x78):08x}", outcome=outcome)


def intervention_zero_growth(binary):
    e, w = prepared(binary, False)
    pad = w.create_object(w.make_mover())
    w.update(pad, (40, 0, 43, 3)); w.update(pad, (40, 0, 42, 2))   # 9 insertions + 5 removals = 14 -> 131068
    e.w(w.map + 0x80, 0)                    # LABELLED: no retail writer stores 0
    names = {}
    a = w.create_object(w.make_mover())
    names[a] = 'A'
    before = {k: w.fields()[k] for k in ('link_count', 'link_capacity', 'records')}
    w.update(a, (19, 19, 21, 22))           # 6 insertion records; 4 fit, the last 2 appends are refused
    after = {k: w.fields()[k] for k in ('link_count', 'link_capacity', 'records')}
    heads = []
    for y in range(19, 21):
        for x in range(19, 22):
            cell = y * SIZE + x
            heads.append(dict(cell=cell, head=e.r(e.r(w.map + 0x28) + 4 * cell) & 0xffffff))
    return dict(intervention='labelled: map +80 growth increment = 0', before=before, after=after,
                object_refs=e.r(a + 0x3c), cell_heads=heads,
                dangling=[h for h in heads if h['head'] != 0xffffff and h['head'] >= after['link_count']],
                saved_rect=[v - (1 << 32) if v >> 31 else v for v in e.r(a + 0x1c, 4)])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--expected', type=Path)
    args = ap.parse_args()
    fresh = scenario(*prepared(args.binary, False))
    control = scenario(*prepared(args.binary, True))
    assert fresh['storm'] and not control['storm'], 'boundary must be crossed only by the fresh variant'
    assert fresh['chains'] == control['chains'] and fresh['query'] == control['query']
    assert fresh['c_member_cells'] == 36 == control['c_member_cells']
    payload = json.loads(json.dumps(dict(binary_sha256=H.SHA256, reachable_growth=fresh, pregrown_control=control,
                                         equal_membership_and_query=True,
                                         labelled_null_allocation=intervention_null(args.binary),
                                         labelled_zero_growth=intervention_zero_growth(args.binary),
                                         scope=__doc__.strip())))
    if args.expected:
        if args.expected.exists():
            if json.loads(args.expected.read_text()) != payload:
                raise SystemExit('expected-SEP-03.3.json differs')
        else:
            H.dump(args.expected, payload)
    args.report.write_text(json.dumps(dict(passed=True, **payload), indent=1) + '\n')
    print(json.dumps({k: v for k, v in payload.items() if k not in ('scope',)}, indent=1)[:5000])


if __name__ == '__main__':
    main()
