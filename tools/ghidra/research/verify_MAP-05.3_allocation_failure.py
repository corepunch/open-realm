#!/usr/bin/env python3
"""MAP-05.3: map/spatial allocation failure and metadata/dead-record cleanup thresholds.

reachable  - (R1) every Storm allocation reached while constructing the registry, owner,
             spatial map, objects and links, with its caller VA; (R2) many fine-search
             metadata links (original 6f14d890) plus dead/removal records are reclaimed by
             the original repeating maintenance request (6f0523d0 -> 6f0543c0 -> 6f14df20)
             without any count threshold; no partial link survives.
labelled   - INTERVENTIONS, never retail behaviour: (L1) each Storm allocation in turn
             returns NULL (retail Storm calls SErrDisplayError(8,...) + ExitProcess(1)
             instead) to record whether any original caller checks the result; (L2) the
             24-bit link index reaches the 0xffffff empty-chain sentinel (requires 16,777,215
             links high-water; reached here by placing the link data pointer so that only the
             top indices are backed by memory).
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sep03_map05_spatial_harness as H  # noqa: E402


def scenario(e):
    w = H.World(e, 16, 16)
    a, b = (w.create_object(w.make_mover()) for _ in range(2))
    w.update(a, (1, 1, 3, 3)); w.update(b, (2, 2, 4, 4))
    w.retire(a); w.compact_dirty()
    return w


def census(binary):
    e = H.Emu(binary)
    e.call(0x6f003c40)
    scenario(e)
    return [dict(n=i, op=r['op'], size=r['size'], flags=r.get('flags'), caller=f"{r['caller']:08x}")
            for i, r in enumerate(e.log) if r['op'] != 'free']


def null_sweep(binary, sites):
    rows = []
    for site in sites:
        e = H.Emu(binary)
        e.call(0x6f003c40)
        counter = [0]

        def fail(kind, size, target=site['n']):
            hit = counter[0] == target
            counter[0] += 1
            return hit
        e.fail = fail
        try:
            scenario(e)
            result = dict(outcome='completed without fault')
        except (RuntimeError, AssertionError) as error:
            fault = e.fault or {}
            result = dict(outcome='fault' if fault else 'assertion', eip=f"{fault.get('eip', 0):08x}",
                          address=f"{fault.get('address', 0):08x}", null_page=bool(fault.get('null_page')),
                          message=str(error).split(' {')[0][:160])
        rows.append(dict(site=site, **result))
    return rows


def maintenance_reclaims_metadata(binary):
    import unicorn
    e = H.Emu(binary)
    e.call(0x6f003c40)
    w = H.World(e, 32, 32)
    movers = [w.create_object(w.make_mover()) for _ in range(4)]
    for i, o in enumerate(movers):
        w.update(o, (i, i, i + 4, i + 4))
    for i in range(2000):                    # ~2000 search metadata links, like one fine search
        w.metadata(i % 32, (i // 32) % 32, i & 0xffff, i >> 16)
    w.update(movers[0], (20, 20, 22, 22))    # removal records
    w.retire(movers[1])                      # removal + dead insertion records
    before = dict(records=w.fields()['records'], high_water=w.fields()['link_count'], dirty=len(w.dirty()))
    clock = w.owner + 0x164
    req = w.fields()['timer_request']
    callbacks = []
    e.uc.hook_add(unicorn.UC_HOOK_CODE, lambda uc, a, s, d: callbacks.append(uc.reg_read(e.X.UC_X86_REG_ECX)),
                  begin=0x6f14df20, end=0x6f14df20)
    e.w(clock + 0x40, e.r(req + 4))
    e.call(0x6f0523d0, clock)
    meta_left = sum(1 for c in range(32 * 32) for _, k, _ in w.chain(c) if k == 2)
    dead_left = sum(1 for c in range(32 * 32) for _, k, p in w.chain(c) if k != 2 and (k == 0 or e.r(p + 0x38) == 0xffffffff))
    free = w.free_list()
    return dict(before=before, callbacks=len(callbacks), after=dict(records=w.fields()['records'],
                high_water=w.fields()['link_count'], dirty=len(w.dirty()), free_list=len(free)),
                metadata_records_left=meta_left, dead_or_removal_records_left=dead_left,
                live_records_equal_memberships=w.fields()['records'] == 4 + 16 + 16,
                retired_object_recycled=movers[1] in w.pool_state()['recycled'],
                free_plus_records_equals_high_water=len(free) + w.fields()['records'] == w.fields()['link_count'])


def sentinel_overflow(binary):
    e = H.Emu(binary)
    e.call(0x6f003c40)
    w = H.World(e, 8, 8)
    o = w.create_object(w.make_mover())
    w.update(o, (0, 0, 1, 1))                 # ordinary first record allocates the real vector
    region = 0x50000000
    e.uc.mem_map(region, 0x10000)             # backs indices 0xfffff0 .. 0x1001ff
    data = (region - 0xfffff0 * 8) & 0xffffffff
    e.uc.mem_map(data & ~0xfff, 0x2000)       # backs indices 0 .. ~0x200 (aliasing target)
    e.w(w.map + 0x78, data)                   # LABELLED fixture: data pointer
    e.w(w.map + 0x84, 0x1000400)              # LABELLED fixture: capacity (no growth path)
    e.w(w.map + 0x88, 0xfffffe)               # LABELLED fixture: high-water 16,777,214
    e.w(w.map + 0xac, 0xffffff)
    e.w(e.r(w.map + 0x28), 0xffffff)          # cell 0 emptied (its old record index 0 now aliased)
    p = w.create_object(w.make_mover())
    w.update(p, (2, 0, 3, 3))                 # three insertions: indices 0xfffffe, 0xffffff, 0x1000000
    heads = [e.r(e.r(w.map + 0x28) + 4 * (16 + x)) & 0xffffff for x in range(3)]
    try:
        query = [('P' if x == p else 'other') for x in w.query((2, 0, 3, 3))]
    except RuntimeError:
        query = dict(fault_eip=f"{e.fault['eip']:08x}", fault_address=f"{e.fault['address']:08x}",
                     reason='aliased index-0 slot holds no valid payload (zero-filled backing)')
    return dict(intervention='labelled: link high-water placed at 0xfffffe', cell_heads=[f'{h:06x}' for h in heads],
                second_cell_reads_as_empty=heads[1] == 0xffffff, third_cell_aliases_index_0=heads[2] == 0,
                object_refs=e.r(p + 0x3c), records=w.fields()['records'], link_count=f"{w.fields()['link_count']:08x}",
                query=query)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--expected', type=Path)
    args = ap.parse_args()
    sites = census(args.binary)
    payload = json.loads(json.dumps(dict(binary_sha256=H.SHA256, allocation_census=sites,
                                         labelled_null_sweep=null_sweep(args.binary, sites),
                                         maintenance_reclaims_metadata=maintenance_reclaims_metadata(args.binary),
                                         labelled_sentinel_overflow=sentinel_overflow(args.binary),
                                         scope=__doc__.strip())))
    if args.expected:
        if args.expected.exists():
            if json.loads(args.expected.read_text()) != payload:
                raise SystemExit('expected-MAP-05.3.json differs')
        else:
            H.dump(args.expected, payload)
    args.report.write_text(json.dumps(dict(passed=True, **payload), indent=1) + '\n')
    print(json.dumps({k: v for k, v in payload.items() if k != 'scope'}, indent=1)[:9000])


if __name__ == '__main__':
    main()
