#!/usr/bin/env python3
"""ROUTE-02.2: static instruction closure of the waypoint selector / next-step
collector roots. Recursive descent over objdump (Intel) disassembly of the
sha256-guarded game.dll: follows fallthrough, conditional/direct jumps, switch
tables (jmp [reg*4+table], entries read until they leave the current 4 KiB
window) and direct calls. Reports every reached function, every indirect
call/jump that is not a switch table, and every import call.

Usage: route02_2_callgraph.py --binary game.dll [--root 6f167bf0 ...] --report out.json
"""
import argparse, hashlib, json, re, struct, subprocess
from pathlib import Path

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
INS = re.compile(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--root', action='append', default=[])
    ap.add_argument('--report', type=Path, required=True)
    a = ap.parse_args()
    data = a.binary.read_bytes()
    assert hashlib.sha256(data).hexdigest() == SHA
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    opt = pe + 24
    secs = []
    for i in range(struct.unpack_from('<H', data, pe + 6)[0]):
        s = opt + struct.unpack_from('<H', data, pe + 20)[0] + 40 * i
        vsz, va, rsz, raw = struct.unpack_from('<IIII', data, s + 8)
        secs.append((0x6f000000 + va, vsz, raw, rsz))

    def u32(va):
        for s, vsz, raw, rsz in secs:
            if s <= va < s + rsz:
                return struct.unpack_from('<I', data, raw + va - s)[0]
        return None
    cache = {}

    def window(va):
        start = va & ~0xfff
        if start not in cache:
            out = subprocess.run(['objdump', '-d', '-M', 'intel', '--start-address=%#x' % start,
                                  '--stop-address=%#x' % (start + 0x1400), str(a.binary)], capture_output=True, text=True).stdout
            ins = {}
            last = None
            for line in out.splitlines():
                mm = INS.match(line)
                if not mm:
                    continue
                if mm.group(3):
                    last = int(mm.group(1), 16)
                    ins[last] = [len(mm.group(2).split()), mm.group(3).strip()]
                elif last is not None:  # objdump continuation line of a long instruction
                    ins[last][0] += len(mm.group(2).split())
            cache[start] = ins
        return cache[start]

    def ins_at(va):
        w = window(va)
        if va in w:
            return w[va]
        w2 = window(va - 0x400) if (va & 0xfff) < 0x400 else {}
        return w2.get(va)
    roots = [int(r, 16) for r in (a.root or ['6f167bf0', '6f165e60', '6f166140', '6f168d30'])]
    funcs, todo = {}, list(roots)
    while todo:
        f = todo.pop()
        if f in funcs:
            continue
        info = dict(calls=set(), indirect=[], imports=[], tables=[], writes_nonstack=[])
        funcs[f] = info
        seen, work = set(), [f]
        while work:
            va = work.pop()
            while va not in seen:
                seen.add(va)
                it = ins_at(va)
                if it is None:
                    info['indirect'].append('undecoded %#x' % va); break
                n, text = it
                op = text.split()[0]
                nxt = va + n
                m = re.search(r'0x([0-9a-f]+)$', text)
                if op == 'call':
                    if re.match(r'call\s+0x[0-9a-f]+$', text):
                        t = int(m.group(1), 16); info['calls'].add(t); todo.append(t)
                    elif 'ds:0x6fa7c' in text:
                        info['imports'].append('%#x %s' % (va, text))
                    else:
                        info['indirect'].append('%#x %s' % (va, text))
                    va = nxt; continue
                if op.startswith('ret'):
                    break
                if op == 'jmp':
                    if re.match(r'jmp\s+0x[0-9a-f]+$', text):
                        t = int(m.group(1), 16)
                        if not (f - 0x10 <= t < f + 0x2000):  # tail jump to another function
                            info['calls'].add(t); todo.append(t); break
                        va = t; continue
                    tm = re.match(r'jmp\s+DWORD PTR \[(e..)\*4\+0x([0-9a-f]+)\]$', text)
                    if tm:
                        table = int(tm.group(2), 16); k = 0; targets = []
                        while True:
                            t = u32(table + 4 * k)
                            if t is None or not (f <= t < f + 0x2000) or k > 64:
                                break
                            targets.append(t); k += 1
                        info['tables'].append(dict(at='%#x' % va, table='%#x' % table, entries=len(targets)))
                        work.extend(targets); break
                    info['indirect'].append('%#x %s' % (va, text)); break
                if op.startswith('j'):
                    if m: work.append(int(m.group(1), 16))
                    va = nxt; continue
                if op == 'mov' and text.startswith('mov    DWORD PTR') or op in ('inc', 'dec', 'add', 'sub', 'or', 'and') and 'PTR [' in text.split(',')[0]:
                    dest = text.split(',')[0]
                    if 'PTR [' in dest and not re.search(r'\[(ebp|esp)[\]\-+]', dest):
                        info['writes_nonstack'].append('%#x %s' % (va, text))
                va = nxt
    rep = dict(binary_sha256=SHA, roots=['%#x' % r for r in roots], functions={})
    for f, i in sorted(funcs.items()):
        rep['functions']['%#x' % f] = dict(calls=sorted('%#x' % c for c in i['calls']), indirect=i['indirect'],
                                           imports=i['imports'], tables=i['tables'], writes_nonstack=i['writes_nonstack'])
    rep['function_count'] = len(funcs)
    rep['indirect_total'] = sum(len(i['indirect']) for i in funcs.values())
    rep['import_total'] = sum(len(i['imports']) for i in funcs.values())
    a.report.write_text(json.dumps(rep, indent=1) + '\n')
    print(json.dumps(dict(function_count=rep['function_count'], indirect_total=rep['indirect_total'],
                          import_total=rep['import_total'], functions=sorted(rep['functions'])), indent=1))


if __name__ == '__main__':
    main()
