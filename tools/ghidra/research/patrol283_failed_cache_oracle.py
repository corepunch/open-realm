#!/usr/bin/env python3
"""Original 495c00 slot logic; synthetic identity/validation/timer service boundary.

No retail bytes distributed. This is not a live acquisition or timer-scheduler oracle.
"""
import argparse
import copy
import hashlib
import json
import pathlib
import struct
from foot03_spatial_harness_copy import Emu, SHA256

ROOT = pathlib.Path(__file__).resolve().parents[3]
FIXTURE = ROOT / 'games/warcraft-3/game/tests/fixtures/retail_patrol283_failed_cache.json'
EMPTY = [0xffffffff, 0xffffffff]
IDENTITY = [5, 9]
CASES = [
    ('all-empty', [], {}, 7, 0),
    ('two-empty', [1, 4], {}, 4, 0),
    ('same-before-empty', [7], {2: IDENTITY}, 2, 0),
    ('same-after-empty', [0, 3], {6: IDENTITY}, 6, 0),
    ('full-earliest', [], {}, 3, 0),
    ('full-equal', [], {}, 0, 0),
    ('full-equal-minimum', [], {}, 2, 0),
    ('validation-reject', [], {}, None, 1),
]

def word(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]

def run(binary):
    from unicorn import UC_HOOK_CODE
    e = Emu(binary)
    X = e.X
    ability, target, vtable = e.fixture(0x500), e.fixture(0x100), e.fixture(0x100)
    registry, slots, canonical = e.fixture(0x100), e.fixture(0x100), e.fixture(0x100)
    timer_vtable, class_stub, remaining_stub = e.fixture(0x100), e.fixture(0x100), e.fixture(0x100)
    e.w(target, vtable, 4)
    e.w(target + 0xc, *IDENTITY)
    e.w(vtable + 0x1c, class_stub)
    e.w(timer_vtable + 0x18, remaining_stub)
    e.w(0x6fd68610, registry)
    e.w(registry + 0xc, slots)
    e.w(registry + 0x1c, 16)
    e.w(slots + IDENTITY[0] * 8, 0xfffffffe, canonical)
    e.w(canonical + 0xc, 0x2b61676c)
    e.w(canonical + 0x14, *IDENTITY)
    e.w(0x6fd6bf5c, 0xdeadbeef)
    e.call(0x6f00b500)
    assert e.r(0x6fd6bf5c) == word(10)
    calls, remaining = [], []
    rejected = 0

    def boundary(uc, address, size, data):
        sp = uc.reg_read(X.UC_X86_REG_ESP)
        ecx = uc.reg_read(X.UC_X86_REG_ECX)
        result, argc = 0, 0
        if address == 0x6f061320:
            assert ecx == ability + 0x6c
            result = target
        elif address == 0x6f652500 or address == class_stub:
            result = 0x1234
        elif address == 0x6f04cdd0:
            result = 1
        elif address == 0x6f4968e0:
            assert ecx == ability and e.r(sp + 4, 3) == [target, 0, 0]
            result, argc = rejected, 3
        elif address == remaining_stub:
            index = (ecx - ability - 0x338) // 20
            assert 0 <= index < 8 and ecx == ability + 0x338 + 20 * index
            e.w(e.r(sp + 4), remaining[index])
            argc = 1
        elif address in (0x6f0606c0, 0x6f0608d0):
            index = (ecx - ability - 0x338) // 20
            assert 0 <= index < 8 and ecx == ability + 0x338 + 20 * index
            row = {'service': 'cancel' if address == 0x6f0606c0 else 'schedule', 'slot': index}
            if address == 0x6f0608d0:
                delay, code, receiver, periodic, alternate = e.r(sp + 4, 5)
                assert receiver == ability
                row.update(duration_word=e.r(delay), event=code, periodic=periodic, alternate=alternate)
                argc = 5
            calls.append(row)
        else:
            raise AssertionError(hex(address))
        uc.reg_write(X.UC_X86_REG_EAX, result)
        uc.reg_write(X.UC_X86_REG_ESP, sp + 4 + 4 * argc)
        uc.reg_write(X.UC_X86_REG_EIP, e.r(sp))

    for address in (0x6f061320, 0x6f652500, class_stub, 0x6f04cdd0,
                    0x6f4968e0, remaining_stub, 0x6f0606c0, 0x6f0608d0):
        e.uc.hook_add(UC_HOOK_CODE, boundary, begin=address, end=address)
    rows = []
    for label, empties, identities, selected, rejected in CASES:
        before = [[20+i, 100+i] for i in range(8)]
        if label == 'all-empty':
            before = [EMPTY[:] for _ in range(8)]
        for i in empties:
            before[i] = EMPTY[:]
        for i, identity in identities.items():
            before[i] = identity[:]
        durations = [8., 7., 6., 1., 4., 5., 9., 10.]
        if label == 'full-equal':
            durations = [3.] * 8
        elif label == 'full-equal-minimum':
            durations = [4., 4., 1., 1., 4., 4., 4., 4.]
        remaining = list(map(word, durations))
        e.w(ability + 0x20, 0x20084010)
        e.w(ability + 0x6c, *IDENTITY)
        for i, identity in enumerate(before):
            e.w(ability + 0x2d4 + i * 12, *identity, 0xfeed0000+i)
            e.w(ability + 0x338 + i * 20, timer_vtable)
        calls.clear()
        e.call(0x6f495c00, ability)
        assert e.esp_after == 4 and e.r(target+4) == 4
        rows.append({'case': label, 'before': before, 'remaining_words': remaining[:],
                     'after': [e.r(ability+0x2d4+i*12, 2) for i in range(8)],
                     'third_words': [e.r(ability+0x2dc+i*12) for i in range(8)],
                     'flags_after': e.r(ability+0x20), 'services': copy.deepcopy(calls),
                     'stack_delta': e.esp_after, 'references_after': e.r(target+4)})
    result = {'schema': 1, 'original_sha256': SHA256, 'initializer_word': e.r(0x6fd6bf5c),
              'entry': '6f495c00', 'rows': rows,
              'scope': ['Unmodified495c00 and00b500 instruction bytes execute in Unicorn.',
                        'External identity resolve, class validation, task validation and timer services are labelled stubs; registry memory is synthetic.',
                        'Original slot traversal/comparison/identity stores and timer-call selection execute unchanged.',
                        'No claim for native scheduler expiry, live target eligibility, callback order, or public multi-target behavior.']}
    validate(result)
    return result

def validate(data):
    assert data['original_sha256'] == SHA256 and data['initializer_word'] == word(10)
    assert len(data['rows']) == len(CASES)
    for row, (label, _, _, selected, rejected) in zip(data['rows'], CASES):
        assert row['case'] == label
        expected = copy.deepcopy(row['before'])
        if selected is not None:
            expected[selected] = IDENTITY[:]
        assert row['after'] == expected
        assert row['third_words'] == [0xfeed0000+i for i in range(8)]
        assert row['stack_delta'] == 4 and row['references_after'] == 4
        assert row['flags_after'] == (0x20084010 if rejected else 0x84010)
        assert row['services'] == ([] if rejected else [
            {'service': 'cancel', 'slot': selected},
            {'service': 'schedule', 'slot': selected, 'duration_word': word(10),
             'event': 0xd01c4, 'periodic': 0, 'alternate': 0}])

def negative_controls(data):
    trials = []
    for kind in ('missing', 'extra', 'reorder', 'slot', 'duration', 'event', 'identity', 'flags'):
        d = copy.deepcopy(data)
        if kind == 'missing': d['rows'].pop()
        if kind == 'extra': d['rows'].append(copy.deepcopy(d['rows'][0]))
        if kind == 'reorder': d['rows'][0], d['rows'][1] = d['rows'][1], d['rows'][0]
        if kind == 'slot': d['rows'][0]['services'][0]['slot'] = 0
        if kind == 'duration': d['rows'][0]['services'][1]['duration_word'] ^= 1
        if kind == 'event': d['rows'][0]['services'][1]['event'] ^= 1
        if kind == 'identity': d['rows'][0]['after'][7][1] ^= 1
        if kind == 'flags': d['rows'][0]['flags_after'] ^= 0x20000000
        try: validate(d)
        except AssertionError: trials.append(kind); continue
        raise AssertionError('accepted mutation: '+kind)
    return trials

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', required=True)
    p.add_argument('--check', action='store_true')
    p.add_argument('--report', type=pathlib.Path)
    a = p.parse_args()
    data = run(a.binary)
    mutations = negative_controls(data)
    if a.check:
        assert data == json.loads(FIXTURE.read_text())
        spec = json.loads(FIXTURE.with_suffix('.provenance.json').read_text())
        for name, digest in spec['repository_files'].items():
            assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest() == digest, name
    if a.report: a.report.write_text(json.dumps(data, indent=2)+'\n')
    print(json.dumps({'status': 'PASS', 'cases': len(data['rows']), 'negative_controls': mutations}))

if __name__ == '__main__': main()
