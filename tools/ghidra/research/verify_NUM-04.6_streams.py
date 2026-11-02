#!/usr/bin/env python3
"""NUM-04.6 original-code oracle: CRandData (TLS registry slot 0xd entry 3) 45-stream reseed and stream consumers.

Executes original game.dll code under Unicorn:
  * 6f693710 Streams_Reseed45(ECX seed) for a seed set (locked 'war3', lobby-tick example, boundary words);
  * 6f214140 Jass_SetRandomSeed full body including its 693710 tail (owner seed + one draw + 45-stream reseed);
  * 6f693660 RandData_StreamRange(ECX index, EDX span), 6f6936a0 RandData_StreamUnitReal(ECX out, EDX index) and
    6f695c70 RandData_RollDice(ECX out, EDX index, dice, sides, bonus, scale*) for dice 1..16 (exact model; the dice<1 error path calls a logging import and is not executed).
Explicit stand-in (recorded in the report): 6f06c180 (TlsGetValue slot lookup) is replaced by a hook returning a
synthetic registry whose [+0x10] data array holds the stream table at entry 3; no arithmetic is replaced.
Runtime scalar initializers 6f001dd0/6f001a80/6f001b80 run first (as verify_wc3_pathing_random.py).
Every original result is compared with an independent Python port of wc3_pathing_random.h (seed/next).
"""
import argparse, hashlib, json, struct
from pathlib import Path

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
TLS_LOOKUP = 0x6f06c180


def model(table):
    def seed(s):
        s &= 0xffffffff
        return [s, ((s % 59) * 0x400 | (s % 61) * 4 | (s % 53) * 0x40000 | (((s // 47) * 17 + s) * 0x4000000)) & 0xffffffff]

    def nxt(st):
        shifts, steps, periods = (24, 16, 8, 0), (4, 12, 24, 28), (188, 212, 236, 244)
        index = mix = 0
        for i in range(4):
            off = ((st[1] >> shifts[i]) & 255) - steps[i]
            if off < 0:
                off += periods[i]
            w = table[off // 4]
            rot = i + 1 if i < 3 else 0
            mix ^= ((w << rot) | (w >> (32 - rot))) & 0xffffffff if rot else w
            index |= off << shifts[i]
        st[1] = index
        st[0] = (st[0] + mix) & 0xffffffff
        return st[0]
    return seed, nxt


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--fixture', type=Path, help='full words (research report root)')
    a = ap.parse_args()
    b = a.binary.read_bytes(); digest = hashlib.sha256(b).hexdigest()
    if digest != SHA:
        ap.error('unsupported game.dll')
    pe = struct.unpack_from('<I', b, 60)[0]; opt = pe + 24
    base, size = (struct.unpack_from('<I', b, opt + i)[0] for i in (28, 56))
    u = Uc(UC_ARCH_X86, UC_MODE_32); u.mem_map(base, (size + 4095) & ~4095)
    for i in range(struct.unpack_from('<H', b, pe + 6)[0]):
        s = opt + struct.unpack_from('<H', b, pe + 20)[0] + 40 * i; va, n, off = struct.unpack_from('<III', b, s + 12)
        if n:
            u.mem_write(base + va, b[off:off + n])
    u.mem_map(0, 4096); u.mem_map(0x10000000, 0x100000); u.mem_map(0x20000000, 0x10000)
    owner, registry, data, tbl, out = 0x10000000, 0x10002000, 0x10003000, 0x10004000, 0x10005000
    stack, stop = 0x20008000, 0x30000000

    def write(at, *v):
        u.mem_write(at, struct.pack('<' + 'I' * len(v), *(q & 0xffffffff for q in v)))

    def read(at, n=2):
        return list(struct.unpack('<' + 'I' * n, u.mem_read(at, n * 4)))
    tls_calls = []

    def on_code(uc, addr, sz, _):
        if addr == TLS_LOOKUP:  # stand-in for TlsGetValue([6fd3cb98])[ECX]
            idx = uc.reg_read(UC_X86_REG_ECX); tls_calls.append(idx)
            if idx != 0xd:
                raise RuntimeError('unexpected TLS slot %#x' % idx)
            esp = uc.reg_read(UC_X86_REG_ESP); ret = struct.unpack('<I', uc.mem_read(esp, 4))[0]
            uc.reg_write(UC_X86_REG_EAX, registry); uc.reg_write(UC_X86_REG_ESP, esp + 4); uc.reg_write(UC_X86_REG_EIP, ret)
    u.hook_add(UC_HOOK_CODE, on_code, begin=TLS_LOOKUP, end=TLS_LOOKUP)

    def run(entry, *v, ecx=0, edx=0, purge=0):
        write(stack, stop, *v); u.reg_write(UC_X86_REG_ESP, stack); u.reg_write(UC_X86_REG_ECX, ecx); u.reg_write(UC_X86_REG_EDX, edx)
        u.emu_start(entry, stop, count=5000000)
        if u.reg_read(UC_X86_REG_EIP) != stop or u.reg_read(UC_X86_REG_ESP) != stack + 4 + purge:
            raise RuntimeError('original ABI differs at %#x' % entry)
        return u.reg_read(UC_X86_REG_EAX)
    for entry in (0x6f001dd0, 0x6f001a80, 0x6f001b80):
        run(entry)
    write(0x6fd53a48, owner)
    write(registry + 0x10, data); write(data + 0xc, tbl); write(tbl, 0x6fb7a69c)
    words = read(0x6fa92f10, 61)
    mseed, mnext = model(words)
    streams = lambda: [read(tbl + 4 + 8 * i) for i in range(45)]

    def model_reseed(seed):
        loc = mseed(seed); return [mseed(mnext(loc)) for _ in range(45)]
    report = dict(binary_sha256=digest, stand_ins=['6f06c180 TLS slot lookup returns synthetic registry (slot 0xd -> [+0x10] data -> entry 3 table)'],
                  reseed=[], set_random_seed=[], consumers=[])
    calls = 0
    for seed in (0x77617233, 77115558, 0, 1, 12345, 0x7fffffff, 0x80000000, 0xffffffff):
        for i in range(45):
            write(tbl + 4 + 8 * i, 0xdeadbeef, 0xdeadbeef)
        run(0x6f693710, ecx=seed); calls += 1
        got = streams()
        if got != model_reseed(seed):
            raise RuntimeError('693710 reseed differs from model for seed %#x' % seed)
        report['reseed'].append(dict(seed=seed, streams=got))
    for seed in (12345, 0, 0x77617233, 0xffffffff):
        write(owner, 0, 0)
        run(0x6f214140, seed); calls += 1
        own = mseed(seed); first = mnext(own)
        if read(owner) != own or streams() != model_reseed(first):
            raise RuntimeError('SetRandomSeed full body differs for %#x' % seed)
        report['set_random_seed'].append(dict(seed=seed, owner_after=read(owner), reseed_word=first, streams=streams()))
    # Consumers on the war3-seeded table: 693660 mul-high span, 6936a0 unit real (software scalar 0 + f*(1-0)).
    run(0x6f693710, ecx=0x77617233)
    ref = model_reseed(0x77617233)
    for k, (idx, span) in enumerate([(2, 100), (32, 7), (35, 0xffffffff), (44, 1), (0, 0x10000), (13, 3)] * 3):
        before = read(tbl + 4 + 8 * idx)
        r = run(0x6f693660, ecx=idx, edx=span); calls += 1
        draw = mnext(ref[idx]); exp = (draw * span) >> 32
        if r != exp or read(tbl + 4 + 8 * idx) != ref[idx]:
            raise RuntimeError('693660 differs')
        report['consumers'].append(dict(fn='6f693660', index=idx, span=span, before=before, draw=draw, result=r, after=ref[idx][:]))
    for k, idx in enumerate([2, 3, 4, 8, 11, 23, 41, 43] * 2):
        before = read(tbl + 4 + 8 * idx)
        write(out, 0xdeadbeef)
        r = run(0x6f6936a0, ecx=out, edx=idx); calls += 1
        draw = mnext(ref[idx])
        f = struct.unpack('<f', struct.pack('<I', (draw & 0x7fffff) | 0x3f800000))[0] - 1.0
        exp = struct.unpack('<I', struct.pack('<f', f))[0]
        got = read(out, 1)[0]
        if r != out or got != exp or read(tbl + 4 + 8 * idx) != ref[idx]:
            raise RuntimeError('6936a0 differs idx %d got %#x exp %#x' % (idx, got, exp))
        report['consumers'].append(dict(fn='6f6936a0', index=idx, before=before, draw=draw, result_word=got, after=ref[idx][:]))
    scale = 0x10005100
    for dice, sides, bonus, sc in [(1, 6, 0, 1.0), (2, 4, 3, 1.0), (16, 2, 0, 1.0), (3, 12, 5, 0.5), (1, 1, 0, 1.0)] * 2:
        idx = 2
        before = read(tbl + 4 + 8 * idx)
        write(out, 0xdeadbeef); write(scale, struct.unpack('<I', struct.pack('<f', sc))[0])
        r = run(0x6f695c70, dice, sides, bonus, scale, ecx=out, edx=idx, purge=0x10); calls += 1
        if dice < 1:
            exp_val = 0.0
        else:
            tot = sum((mnext(ref[idx]) * sides) >> 32 for _ in range(dice))
            exp_val = float(struct.unpack('<f', struct.pack('<f', float(tot + dice + bonus)))[0]) * sc
        exp = struct.unpack('<I', struct.pack('<f', exp_val))[0]
        got = read(out, 1)[0]
        if r != out or got != exp or read(tbl + 4 + 8 * idx) != ref[idx]:
            raise RuntimeError('695c70 differs dice %d sides %d got %#x exp %#x' % (dice, sides, got, exp))
        report['consumers'].append(dict(fn='6f695c70', index=idx, dice=dice, sides=sides, bonus=bonus, scale=sc, before=before, result_word=got, after=ref[idx][:]))
    summary = dict(passed=True, binary_sha256=digest, original_calls=calls, tls_lookups=len(tls_calls), stand_ins=report['stand_ins'],
                   reseed_seeds=len(report['reseed']), set_random_seed_seeds=len(report['set_random_seed']), consumer_calls=len(report['consumers']),
                   war3_streams_sha256=hashlib.sha256(json.dumps(report['reseed'][0]['streams']).encode()).hexdigest())
    if a.fixture:
        a.fixture.write_text(json.dumps(summary | report, separators=(',', ':')) + '\n')
    a.report.write_text(json.dumps(summary, indent=1) + '\n'); print(json.dumps(summary, indent=1))


if __name__ == '__main__':
    main()
