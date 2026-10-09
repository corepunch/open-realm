#!/usr/bin/env python3
"""Cold original point factories and wrapper pool, deferred final release and reuse.

Only Storm memory imports supply host storage. Class lookup and registry identity
bindings are supplied, as in the earlier lifetime oracle; constructors, raw growth,
reference ownership, deferred clock dispatch and all reclaim code are original.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / 'research'))
import sep03_map05_spatial_harness as H

COUNT = 513
CLASSES = (
    ('COrderPoint', 0x6fd70e14, 0x6fb7885c, 0x6f6807a0, 0x6fb7886c, 0x58, 1, 0x54, 0x6f72642e, 0x6f015630, 0x6f01564d),
    ('CTaskPoint', 0x6fd70f1c, 0x6fb78eb0, 0x6f680db0, 0x6fb78ec0, 0x50, 64, 0x4c, 0x74736b2e, 0x6f0158a0, 0x6f0158bd),
)


def validate_report(report):
    if report.get('passed') is not True or report.get('binary_sha256') != H.SHA256:
        raise ValueError('unsupported or unsuccessful cold pool run')
    rows = report.get('classes', [])
    if len(rows) != 2:
        raise ValueError('missing point class')
    for row, cls in zip(rows, CLASSES):
        name, _, _, _, _, size, block, *_ = cls
        expected = dict(name=name, objects=COUNT, payload_block_size=block,
                        payload_block_bytes=size * block + 4,
                        payload_growth_allocations=(COUNT + block - 1) // block,
                        wrapper_block_size=512, wrapper_block_bytes=0xbc * 512 + 4,
                        wrapper_growth_allocations=2, reuse_allocations=0,
                        final_payload_live=0, final_wrapper_live=0, final_registry_live=0,
                        deferred_releases=COUNT * 2)
        if any(row.get(k) != v for k, v in expected.items()):
            raise ValueError('cold growth/release contract differs')
        phases = row.get('phases', [])
        if len(phases) != 2 or phases[0] != phases[1]:
            raise ValueError('reuse changed final release ordering')
        for phase in phases:
            if sorted(phase.get('release_order', [])) != list(range(COUNT)):
                raise ValueError('missing or duplicated final release')
            if phase.get('pending_before') != COUNT or phase.get('pending_after') != 0:
                raise ValueError('deferred release queue incomplete')
    return report


def verify(binary):
    from unicorn import UC_HOOK_CODE
    reports = []
    for name, factory, factory_vt, construct, payload_vt, size, block, owned, rawcode, init, end in CLASSES:
        e = H.Emu(binary)
        registry = e.fixture(0x100)
        e.call(0x6f04a6b0, registry)
        e.w(H.REGISTRY_ALIAS, registry)
        owner = e.fixture(0x1000)
        e.call(0x6f157610, owner, 7085)
        e.w(H.OWNER_GLOBAL, owner)
        e.w(owner + 0x254, 0x6f04d9c0)
        host = e.fixture(0x100)
        e.w(0x6fd3c82c, host)
        e.w(host + 0x30, -1)
        # Execute the shipped static initializer through ObjectPool_Init and
        # the vtable write. Stop before CRT atexit registration, not a game hook.
        e.w(e.stack, e.stop)
        e.uc.reg_write(e.X.UC_X86_REG_ESP, e.stack)
        e.uc.emu_start(init, end, count=10000)
        assert e.uc.reg_read(e.X.UC_X86_REG_EIP) == end
        assert e.uc.reg_read(e.X.UC_X86_REG_ESP) == e.stack - 4  # atexit function argument
        assert e.r(factory) == factory_vt
        assert e.r(factory + 4, 5) == [size, block, 0, 0, 0]
        bucket, entry, hash_input = e.fixture(16), e.fixture(0x80), e.fixture(4)
        e.w(hash_input, rawcode)
        class_hash = e.call(0x6f198420, hash_input)
        e.w(host + 0x28, bucket, 0, 0)
        e.w(bucket, 0, 0, entry)
        e.w(entry + 4, class_hash)
        e.w(entry + 0x18, rawcode)
        e.w(entry + 0x70, factory)
        assert e.r(host + 0x34) == 0
        pool = e.call(0x6f057470)
        assert e.r(pool, 8) == [0xbc, 512, 0, 0, 0, 0, 0, 0]
        slots = e.fixture(COUNT * 8)
        e.w(registry + 0xc, slots)
        e.w(registry + 0x1c, COUNT)
        clock = owner + 0x14
        phases, wrappers, payloads, released = [], [], [], []
        def watch(uc, address, _size, _data):
            released.append(e.uc.reg_read(e.X.UC_X86_REG_ECX))
        hook = e.uc.hook_add(UC_HOOK_CODE, watch, begin=0x6f0576b0, end=0x6f0576b0)
        start = len(e.log)
        for cycle in range(2):
            current_wrappers, current_payloads = [], []
            previous_released = list(released)
            reuse_start = len(e.log)
            for i in range(COUNT):
                wrapper = e.call(0x6f057350, pool, 0, 0)
                payload = e.call(construct, factory)
                assert e.r(payload) == payload_vt and e.r(payload + owned) == 0
                assert e.r(payload + 4, 4) == [0, 0, 0xffffffff, 0xffffffff]
                assert e.r(wrapper + 0x14, 4) == [0xffffffff, 0xffffffff, 0, 0]
                e.call(0x6f057c30, wrapper, payload)
                assert e.r(payload + 4) == 1
                # Supplied canonical bindings. No payload/wrapper preallocation.
                e.w(wrapper + 0x14, i, 100 + i)
                e.w(payload + 0xc, i, 100 + i)
                e.w(slots + 8 * i, -2, wrapper)
                current_wrappers.append(wrapper)
                current_payloads.append(payload)
                assert e.r(pool + 0x18) == i + 1 and e.r(factory + 0xc) == i + 1
            if cycle == 0:
                wrappers, payloads = current_wrappers, current_payloads
                allocs = e.log[start:]
                payload_allocs = [r for r in allocs if r['size'] == size * block + 4]
                wrapper_allocs = [r for r in allocs if r['size'] == 0xbc * 512 + 4]
                assert len(payload_allocs) == (COUNT + block - 1) // block
                assert len(wrapper_allocs) == 2
                assert all(r['op'] == 'alloc' for r in payload_allocs + wrapper_allocs)
            else:
                expected = list(reversed(previous_released))
                assert current_wrappers == expected
                payload_by_wrapper = dict(zip(wrappers, payloads))
                assert current_payloads == [payload_by_wrapper[p] for p in expected]
                assert len(e.log) == reuse_start
            e.w(registry + 0x40, -1)
            e.w(registry + 0x48, COUNT)
            release_start = len(released)
            for payload in current_payloads:
                e.call(0x6f0557b0, payload)
                e.call(0x6f0557b0, payload)  # idempotent deferred request
            assert e.r(clock + 0x3c) == COUNT and len(released) == release_start
            e.w(clock + 0x40, 0x3f800000 + cycle * 0x800000)
            e.call(0x6f052380, clock)
            order = released[release_start:]
            assert len(order) == COUNT and len(set(order)) == COUNT
            assert e.r(clock + 0x3c) == 0 and e.r(pool + 0x18) == 0
            assert e.r(factory + 0xc) == 0 and e.r(registry + 0x48) == 0
            assert e.r(pool + 0x14) == order[-1] - 4
            for wrapper, payload in zip(current_wrappers, current_payloads):
                assert e.r(wrapper + 0x14, 4) == [0xffffffff, 0xffffffff, 0, 0]
                assert e.r(wrapper + 0x50, 2) == [0, 0]
                # The raw free chain overwrites the destroyed vtable word.
                assert e.r(payload) != payload_vt and e.r(payload + 4) == 0
                assert e.r(payload + 0xc, 2) == [0xffffffff, 0xffffffff]
            ordinal = {p: i for i, p in enumerate(current_wrappers)}
            phases.append(dict(pending_before=COUNT, pending_after=0,
                               release_order=[ordinal[p] for p in order]))
            count = len(e.log)
            e.call(0x6f052380, clock)
            assert len(released) == release_start + COUNT and len(e.log) == count
            assert e.r(0) == 0
        e.uc.hook_del(hook)
        reports.append(dict(name=name, objects=COUNT, payload_block_size=block,
            payload_block_bytes=size * block + 4, payload_growth_allocations=len(payload_allocs),
            wrapper_block_size=512, wrapper_block_bytes=0xbc * 512 + 4,
            wrapper_growth_allocations=len(wrapper_allocs), reuse_allocations=0,
            final_payload_live=0, final_wrapper_live=0, final_registry_live=0,
            deferred_releases=COUNT * 2, phases=phases))
    return validate_report(dict(passed=True, binary_sha256=H.SHA256,
        status='bounded-original-order-pool-growth', classes=reports,
        scope=__doc__, exclusions=['Supplied class lookup and canonical registry identity bindings.',
            'Static initializer stops before CRT atexit registration.',
            'Empty subscriptions, relationships and child lists; public producer evidence is separate.']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ('binary', 'output'): parser.add_argument('--' + arg, type=Path, required=True)
    parser.add_argument('--fixture', type=Path)
    args = parser.parse_args()
    report = verify(args.binary)
    if args.fixture and report != json.loads(args.fixture.read_text()):
        raise ValueError('frozen cold pool result differs')
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'classes'}))


if __name__ == '__main__': main()
