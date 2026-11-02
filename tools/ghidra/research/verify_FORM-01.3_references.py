#!/usr/bin/env python3
"""FORM-01.3 / GROUP-03.2 whole-image reference scan (research tool, new file; reads the hash-checked DLL only).

For every canonical request+100 flag setter and its CMoveReq wrappers, lists all rel32 CALL/JMP sites in .text and
every aligned or unaligned absolute 32-bit occurrence of the entry VA in any section; also verifies each setter's
instruction bytes (`cmp [ebp+8],0` / `or|and dword [ecx+100], imm32` / `ret 4`) and reports the bit it writes.
--expected compares with a frozen report (exit 1 on difference).
"""
import argparse, hashlib, json, struct, sys
from pathlib import Path

SETTERS = {0x6f16dc30: 0x1, 0x6f16dc50: 0x2, 0x6f16dcb0: 0x4, 0x6f16dc70: 0x8, 0x6f16dc90: 0x10, 0x6f16d7e0: 0x20,
           0x6f16dde0: 0x100, 0x6f16dd00: 0x200, 0x6f16dc00: 0x400, 0x6f16ddb0: 0x800}
OTHERS = [0x6f89caf0, 0x6f89cb30, 0x6f89cbf0, 0x6f89cca0, 0x6f89ccc0, 0x6f89cd40, 0x6f89cd60, 0x6f89cd80,
          0x6f05a5c0, 0x6f05b970, 0x6f05c0e0, 0x6f05c320, 0x6f05c350]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--expected', type=Path)
    args = ap.parse_args()
    data = args.binary.read_bytes()
    if hashlib.sha256(data).hexdigest() != 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':
        ap.error('requires retail game.dll 1.27.1.7085')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    opt = pe + 24
    base = struct.unpack_from('<I', data, opt + 28)[0]
    secs = []
    for i in range(struct.unpack_from('<H', data, pe + 6)[0]):
        s = opt + struct.unpack_from('<H', data, pe + 20)[0] + i * 40
        name = data[s:s + 8].rstrip(b'\0').decode()
        vsize, va, rsize, raw = struct.unpack_from('<IIII', data, s + 8)
        secs.append((name, base + va, data[raw:raw + min(rsize, vsize) if vsize else rsize]))
    text = next(s for s in secs if s[0] == '.text')
    report = dict(binary_sha256=hashlib.sha256(data).hexdigest(), scanner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  setters={}, others={})
    targets = list(SETTERS) + OTHERS
    rel = {t: [] for t in targets}
    tva, tb = text[1], text[2]
    tset = set(targets)
    for j in range(len(tb) - 5):
        op = tb[j]
        if op in (0xe8, 0xe9):
            dst = (tva + j + 5 + struct.unpack_from('<i', tb, j + 1)[0]) & 0xffffffff
            if dst in tset:
                rel[dst].append(['call' if op == 0xe8 else 'jmp', '%08x' % (tva + j)])
    absref = {t: [] for t in targets}
    for name, va, b in secs:
        for t in targets:
            needle = struct.pack('<I', t)
            i = b.find(needle)
            while i >= 0:
                absref[t].append([name, '%08x' % (va + i)])
                i = b.find(needle, i + 1)

    def bytes_at(v, n):
        return tb[v - tva:v - tva + n]
    for t, bit in SETTERS.items():
        code = bytes_at(t, 0x30)
        ok = code[:3] == b'\x55\x8b\xec' and code[3:7] == b'\x83\x7d\x08\x00'
        imm = struct.pack('<I', bit)
        orr = (b'\x81\x89\x00\x01\x00\x00' + imm) in code or (b'\x83\x89\x00\x01\x00\x00' + bytes([bit])) in code
        andm = (b'\x81\xa1\x00\x01\x00\x00' + struct.pack('<I', ~bit & 0xffffffff)) in code or \
               (b'\x83\xa1\x00\x01\x00\x00' + bytes([~bit & 0xff])) in code
        report['setters']['%08x' % t] = dict(bit='%x' % bit, prologue_test=ok, or_imm=orr, and_imm=andm, ret4=b'\xc2\x04\x00' in code,
                                             rel32=rel[t], absolute=absref[t])
    for t in OTHERS:
        report['others']['%08x' % t] = dict(rel32=rel[t], absolute=absref[t])
    args.report.write_text(json.dumps(report, indent=1) + '\n')
    if args.expected:
        exp = json.loads(args.expected.read_text())
        same = exp['setters'] == report['setters'] and exp['others'] == report['others']
        print('compare', 'equal' if same else 'DIFFERENT')
        sys.exit(0 if same else 1)
    for t, r in report['setters'].items():
        print(t, r['bit'], r['prologue_test'], r['or_imm'], r['and_imm'], r['ret4'], [x[1] for x in r['rel32']], r['absolute'])
    for t, r in report['others'].items():
        print(t, [x[1] for x in r['rel32']], r['absolute'])


if __name__ == '__main__':
    main()
