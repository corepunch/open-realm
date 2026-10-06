#!/usr/bin/env python3
"""MAP-04.2 static exit inventory for retail temporary pathing exclusion scopes.

Instruction-level (recursive-descent) analysis of game.dll 1.27.1.7085 using GNU
objdump decoding. For every acquire/release pair it enumerates the instructions
reachable from the acquire without passing the matching release, and reports:

* exits (ret / tail jump) reachable while the scope is held (early exits),
* the direct-call closure executed while held, every indirect call/jump in that
  closure, imported calls (throw/raise/abort) and whether any known map writer
  or another exclusion scope is reachable (nesting / edit-during-request),
* the compiled C++ unwind map (FuncInfo) for holder-based scopes.

Read-only: the DLL is only decoded. No Ghidra access.
"""
import argparse, bisect, hashlib, json, re, struct, subprocess, sys
from pathlib import Path

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
IMPORTS = {0x6fa7c5b4: '_CxxThrowException', 0x6fa7c5a8: 'raise', 0x6fa7c530: 'abort', 0x6fa7c4bc: 'exit',
           0x6fa7c5a0: '_exit', 0x6fa7c330: 'RaiseException', 0x6fa7c350: 'TerminateProcess',
           0x6fa7c39c: 'std::_Xbad_alloc', 0x6fa7c3a0: 'std::_Xout_of_range', 0x6fa7c3a4: 'std::_Xlength_error',
           0x6fa7c3e8: 'terminate', 0x6fa7c508: 'memmove', 0x6fa7c50c: 'memset',
           0x6fa7c7e8: 'Storm#405 SMemReAlloc', 0x6fa7c7ec: 'Storm#401 SMemAlloc', 0x6fa7c884: 'Storm#403 SMemFree',
           0x6fa7c300: 'IsDebuggerPresent', 0x6fa7c304: 'IsProcessorFeaturePresent',
           0x6fa7c3dc: '__crtTerminateProcess', 0x6fa7c3e0: '__crtUnhandledException', 0x6fa7c3e4: '_crt_debugger_hook'}
# Functions that write fine cells/links, hierarchy classes or exclusion counters.
WRITERS = {
    0x6f15d360: 'PathMaps_UpdateRectangle (hierarchy clear/rebuild)',
    0x6f054000: 'PathCell_EditTerrainFlags', 0x6f04d870: 'terrain pathing setter',
    0x6f14e770: 'SpatialObject_UpdateRectangle', 0x6f14d960: 'SpatialMap_EmitRectangleRecords',
    0x6f160590: 'Mover_UpdateFineOccupancyBounds', 0x6f04e0b0: 'full hierarchy refresh',
    0x6f15ab60: 'map initialization', 0x6f05bd30: 'unit spatial +40 toggle', 0x6f063d10: 'widget list +40 toggle',
    0x6f16da60: 'group target +40 toggle', 0x6f169c50: 'group member acquire', 0x6f169d60: 'group member release',
    0x6f059590: 'rectangle-list exclusion query', 0x6f166c30: 'Path_RequestAcceleratedRoute (coarse scope)',
    0x6f166e90: 'Path_RequestFineRoute (fine scope)', 0x6f167bf0: 'Path_SelectVisibleFineWaypoint (self scope)',
    0x6f166140: 'Path_CollectAndResolveNextStepBlockers (self scope)', 0x6f16ec00: 'PathContext_AdmitPortalFinePoint',
    0x6f16ee80: 'Separate_ValidateEndpoint', 0x6f170080: 'Mover_RecoverEmbeddedFinePoint',
    0x6f0599c0: 'unreferenced mixed exclusion query', 0x6f05ca50: 'MoverBridge_StopWithRecovery',
    0x6f04df50: 'PathWorld_TestPointQuery', 0x6f16bcf0: 'MoveRequest_PublishReadyCohorts',
}

# (name, function entry, acquire VAs, release VAs, notes)
SCOPES = [
    ('fine_request_self_target', 0x6f166e90, [0x6f166f66, 0x6f166f7d], [0x6f167041, 0x6f167048],
     'inc self+40, inc target+40, PathFine_BuildRoute, dec target, dec self'),
    ('coarse_request_rectangles', 0x6f166c30, [0x6f166d35, 0x6f166d4c], [0x6f166dd7, 0x6f166dee],
     '15d360(self+1c,1),15d360(target+1c,1), PathAcc_BuildRoute, 15d360(self,0),15d360(target,0)'),
    ('visible_waypoint_self', 0x6f167bf0, [0x6f167c2a], [0x6f167cbd], 'self +40 around segment samples'),
    ('next_step_blockers_self', 0x6f166140, [0x6f166265], [0x6f1662dd], 'self +40 around step blocker collection'),
    ('portal_fine_point', 0x6f16ec00, [0x6f16ec34], [0x6f16eca0], 'self +40'),
    ('separation_endpoint', 0x6f16ee80, [0x6f16ef24], [0x6f16ef61], 'self +40'),
    ('embedded_point_recovery', 0x6f170080, [0x6f170133], [0x6f1701d2], 'self +40'),
    ('group_publish_members', 0x6f16bcf0, [0x6f16bd1d], [0x6f16bd2a, 0x6f16bd90], '169c50 acquire, 169d60 release (two exits)'),
    ('stop_with_recovery_unit', 0x6f05ca50, [0x6f05ca68], [0x6f05ca8f], '05bd30(1) / 05bd30(0) around 171340'),
    ('point_query_unit', 0x6f04df50, [0x6f04dffb], [0x6f04e03f], '05bd30(1) / 05bd30(0) around 149320'),
    ('rectangle_list_query', 0x6f059590, [0x6f059733, 0x6f05976e], [0x6f059808, 0x6f05983f], 'units/widgets 15d360(1) around 1627e0, then 15d360(0)'),
    ('unreferenced_mixed_query', 0x6f0599c0, [0x6f059a4c], [0x6f059bbe], 'no static references'),
]


def load(binary, cache):
    data = binary.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA:
        sys.exit('unsupported binary')
    if not cache.exists():
        out = subprocess.run(['objdump', '-d', '-M', 'intel', '--start-address=0x6f001000', '--stop-address=0x6fa7b74a',
                              str(binary)], capture_output=True, text=True, check=True).stdout
        cache.write_text(out)
    return data, parse(cache.read_text())


def parse(text, lo=0):
    """objdump prints >7-byte instructions on continuation lines without a mnemonic."""
    ins, last = {}, None
    for line in text.split('\n'):
        m = re.match(r'^\s*([0-9a-f]{8}):\t([0-9a-f ]+?)\s*(?:\t(.*))?$', line)
        if not m:
            continue
        a, n = int(m.group(1), 16), len(m.group(2).split())
        if m.group(3) is None:
            if last is not None:
                ins[last] = (ins[last][0] + n, ins[last][1])
            continue
        if a >= lo:
            ins[a] = (n, m.group(3).strip())
            last = a
    return ins


class Dis:
    def __init__(self, binary, ins, u32=None):
        self.binary, self.ins, self.u32 = binary, ins, u32
        self.extra = 0
        self.order = None

    def resolve_tables(self):
        """Bounded switch tables: `cmp r,N; ja D; jmp [r*4+T]` -> T[0..N]."""
        order = sorted(self.ins)
        pos = {a: i for i, a in enumerate(order)}
        for va, (size, text) in list(self.ins.items()):
            m = re.match(r'jmp\s+DWORD PTR \[(e\w\w)\*4\+0x([0-9a-f]+)\]$', text)
            if not m:
                continue
            reg, table = m.group(1), int(m.group(2), 16)
            bound, byte_table = None, None
            for back in range(1, 7):
                i = pos[va] - back
                if i < 0:
                    break
                t = self.ins[order[i]][1]
                mb = re.match(r'movzx\s+' + reg + r',BYTE PTR \[(e\w\w)\+0x([0-9a-f]+)\]$', t)
                if mb and byte_table is None:
                    byte_table, reg = int(mb.group(2), 16), mb.group(1)
                    continue
                mm = re.match(r'cmp\s+' + reg + r',0x([0-9a-f]+)$', t)
                if mm:
                    bound = int(mm.group(1), 16)
                    break
            if bound is None or bound >= 1024:
                continue
            n = bound + 1
            if byte_table is not None:
                n = max(self.u32(byte_table + k) & 0xff for k in range(bound + 1)) + 1
            targets = [self.u32(table + 4 * k) for k in range(n)]
            if all(abs(t - va) < 0x4000 for t in targets):
                TABLES[va] = targets

    def at(self, va):
        if not 0x6f001000 <= va < 0x6fa7b74a:
            raise KeyError('outside .text: %x' % va)
        if va not in self.ins:  # linear sweep misaligned here: decode a window from va.
            out = subprocess.run(['objdump', '-d', '-M', 'intel', f'--start-address={va:#x}', f'--stop-address={va + 64:#x}',
                                  str(self.binary)], capture_output=True, text=True, check=True).stdout
            for a, v in parse(out, va).items():
                self.ins.setdefault(a, v)
            self.extra += 1
        return self.ins[va]


TABLES = {}  # jmp VA -> resolved targets (filled by Dis.table)


def successors(dis, va):
    size, text = dis.at(va)
    if va in TABLES:
        return list(TABLES[va]), None
    op = text.split()[0] if text else ''
    nxt = va + size
    target = None
    m = re.match(r'^(j\w+|call)\s+0x([0-9a-f]+)$', text)
    if m:
        target = int(m.group(2), 16)
    if op.startswith('ret'):
        return [], ('ret', None)
    if op == 'jmp':
        if target is not None:
            return [target], ('jmp', target)
        return [], ('indirect_jmp', text)
    if op.startswith('j') or op.startswith('loop'):
        return [nxt, target] if target is not None else [nxt], None
    if op == 'call':
        return [nxt], (('call', target) if target is not None else ('indirect_call', text))
    if op in ('int3', 'ud2', 'hlt', '(bad)'):
        return [], ('trap', text)
    return [nxt], None


def function_cfg(dis, entry, starts):
    """Recursive descent of one function; jmp to another function start is a tail call."""
    seen, work, events = set(), [entry], {}
    while work:
        va = work.pop()
        if va in seen:
            continue
        seen.add(va)
        succ, ev = successors(dis, va)
        if ev and ev[0] == 'jmp' and ev[1] in starts and ev[1] != entry:
            events[va] = ('tail', ev[1])
            continue
        if ev:
            events[va] = ev
        work.extend(succ)
    return seen, events


WRITES = re.compile(r'^(mov|lea|pop|xor|add|sub|and|or|inc|dec|imul|movzx|movsx|xchg|cmov\w*|shl|shr|sar|not|neg|sete|setne)\s+(e\w\w)\b')


def guards(dis, acquire, releases):
    """`test R,R; je skip` immediately before an acquire/release on [R+0x40] (or call guarded by R)."""
    order = sorted(dis.ins)
    pos = {a: i for i, a in enumerate(order)}
    out = {}
    for r in [acquire, *releases]:
        i = pos[r]
        t2, t1 = dis.ins[order[i - 1]][1], dis.ins[order[i - 2]][1]
        m1 = re.match(r'test\s+(e\w\w),(e\w\w)$', t1)
        m2 = re.match(r'je\s+0x([0-9a-f]+)$', t2)
        if m1 and m2 and m1.group(1) == m1.group(2):
            out[r] = (order[i - 1], m1.group(1), int(m2.group(1), 16))
    return out


def scope_region(dis, acquire, releases, starts, entry):
    """Instructions reachable while held. A release's null-guard skip edge is infeasible when the
    same callee-saved register guarded the acquire and is not written inside the held region."""
    g = guards(dis, acquire, releases)
    seen, work, events = set(), [acquire], {}
    acq_reg = g.get(acquire, (None, None))[1]
    paired = [r for r in releases if acq_reg and g.get(r, (0, None))[1] == acq_reg]
    rel = set(paired or releases)
    infeasible = {}
    for r in releases:
        if r in g and g[r][1] == acq_reg and acq_reg in ('esi', 'edi', 'ebx'):
            infeasible[g[r][0]] = g[r][2]
    while work:
        va = work.pop()
        if va in seen:
            continue
        seen.add(va)
        succ, ev = successors(dis, va)
        if va in infeasible:
            succ = [s for s in succ if s != infeasible[va]]
        if ev and ev[0] == 'jmp' and ev[1] in starts and ev[1] != entry:
            ev = ('tail', ev[1]); succ = []
        if ev:
            events[va] = ev
        if va in rel:
            continue
        work.extend(succ)
    written = sorted({WRITES.match(dis.ins[v][1]).group(2) for v in seen
                      if v not in rel and WRITES.match(dis.ins[v][1])})
    guard_ok = acq_reg is None or acq_reg not in written
    return seen, events, dict(acquire_guard=acq_reg, infeasible_skip_edges={hex(k): hex(v) for k, v in infeasible.items()},
                              registers_written_while_held=written, guard_register_preserved=guard_ok)


# Indirect calls resolved from instructions: call site -> targets (evidence in HANDOFF).
RESOLVED = {
    # 14ef80: object from pool 151480, whose constructor 14fc40 stores vtable 6fa90d64 (14fca9);
    # slot +0c of 6fa90d64 is 169840 (PathGroup_Activate).
    0x6f14efa6: [0x6f169840],
    # 14ec50: object from pool 150d50, whose constructor 1657c0 stores vtable 6fa91c40 (165813);
    # slot +0c of 6fa91c40 is 166060 (Path_Activate).
    0x6f14ec76: [0x6f166060],
}
# PathFine_FindPlacement 14a1e0 calls its 7th argument (ebx=[ebp+20]) and forwards it to 14b070/14b220/
# 14b3d0/14b580 (edi=[ebp+18]). Producers of that argument found by instruction tracing:
#   Unit_StopWithRecoveryAndSupport 69a871 / 9d7625 push 6f654060 -> 05ca50 p1 -> 171340 p6 -> 170080 p3 -> 14a1e0 p7;
#   05ca50 callers 495540/69a89f push 0 (no callback);
#   PathOwner_FindPlacement 16ecc0 stores constant 6f16ee00 (16edad) or its own [ebp+20].
# 6f654060 is a leaf predicate; 6f16ee00 itself calls a context callback [ebx+24] (left unresolved).
# Context-specific: the stop/recovery scopes receive 6f654060 or null, the portal scope 6f16ee00.
PLACEMENT_SITES = (0x6f14a2bb, 0x6f14b0cd, 0x6f14b134, 0x6f14b196, 0x6f14b1f6, 0x6f14b27d, 0x6f14b2e3, 0x6f14b346, 0x6f14b3a6,
                   0x6f14b42d, 0x6f14b498, 0x6f14b4f6, 0x6f14b556, 0x6f14b5d5, 0x6f14b640, 0x6f14b6a6, 0x6f14b706)
CONTEXT_CALLBACKS = {'stop_with_recovery_unit': [0x6f654060], 'embedded_point_recovery': [0x6f654060],
                     'portal_fine_point': [0x6f16ee00]}


def closure(dis, roots, starts, resolved=RESOLVED):
    funcs, work, indirect, imports, traps = set(), list(roots), [], [], []
    while work:
        f = work.pop()
        if f in funcs or f is None:
            continue
        funcs.add(f)
        _, events = function_cfg(dis, f, starts)
        for va, ev in events.items():
            if ev[0] in ('call', 'tail'):
                work.append(ev[1])
            elif ev[0] == 'indirect_call' and va in resolved:
                work.extend(resolved[va])
            elif ev[0] == 'indirect_call':
                m = re.search(r'ds:0x([0-9a-f]+)', ev[1])
                if m and int(m.group(1), 16) in IMPORTS:
                    imports.append((f, va, IMPORTS[int(m.group(1), 16)]))
                elif m and int(m.group(1), 16) < 0x6fa7d000:
                    imports.append((f, va, 'import@' + m.group(1)))
                else:
                    indirect.append((f, va, ev[1]))
            elif ev[0] == 'indirect_jmp':
                m = re.search(r'ds:0x([0-9a-f]+)', ev[1])
                if m and int(m.group(1), 16) < 0x6fa7d000:
                    imports.append((f, va, IMPORTS.get(int(m.group(1), 16), 'import@' + m.group(1))))
                else:
                    indirect.append((f, va, ev[1]))
            elif ev[0] == 'trap':
                traps.append((f, va, ev[1]))
    return funcs, indirect, imports, traps


def closure_stores(dis, funcs, starts):
    """Stores in the held closure to +0x40 counters, path self/target (+a0/+a4) or object rectangle (+1c..+28)."""
    hits = []
    for f in sorted(funcs):
        seen, _ = function_cfg(dis, f, starts)
        for v in sorted(seen):
            t = dis.ins[v][1]
            m = re.match(r'^(mov|inc|dec|add|sub|or|and)\s+DWORD PTR \[(e\w\w)\+0x(40|a0|a4|1c|20|24|28|38|f4)\]', t)
            if m:
                hits.append([hex(f), hex(v), t])
    return hits


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--report', type=Path, required=True)
    p.add_argument('--functions', type=Path, required=True, help='research/_ghidra/functions.json (function starts)')
    p.add_argument('--cache', type=Path, default=Path('/tmp/map04-objdump.asm'))
    a = p.parse_args()
    data, ins = load(a.binary, a.cache)
    dis = None
    starts = {int(f['address'], 16) for f in json.loads(a.functions.read_text())['functions']}
    starts |= set(WRITERS)
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    secs = []
    for i in range(struct.unpack_from('<H', data, pe + 6)[0]):
        o = pe + 24 + struct.unpack_from('<H', data, pe + 20)[0] + 40 * i
        vsz, va, rsz, raw = struct.unpack_from('<IIII', data, o + 8)
        secs.append((0x6f000000 + va, vsz, raw))

    def u32(va):
        for s, n, raw in secs:
            if s <= va < s + n:
                return struct.unpack_from('<I', data, raw + va - s)[0]
        raise KeyError(hex(va))

    dis = Dis(a.binary, ins, u32)
    dis.resolve_tables()
    report = dict(binary_sha256=SHA, switch_tables_resolved=len(TABLES), tool=subprocess.run(['objdump', '--version'], capture_output=True, text=True).stdout.split('\n')[0],
                  scope=__doc__, scopes=[])
    for name, entry, acquires, releases, note in SCOPES:
        fseen, fevents = function_cfg(dis, entry, starts)
        missing = [hex(v) for v in acquires + releases if v not in fseen]
        held = set()
        held_events = {}
        guard_info = {}
        for acq in acquires:
            s, ev, gi = scope_region(dis, acq, releases, starts, entry)
            held |= s
            held_events.update(ev)
            guard_info[hex(acq)] = gi
        exits = sorted(hex(va) for va, ev in held_events.items() if ev[0] in ('ret', 'tail', 'indirect_jmp', 'trap'))
        calls = sorted({ev[1] for va, ev in held_events.items() if ev[0] == 'call' and va not in acquires + releases})
        local_indirect = sorted(hex(va) + ' ' + ev[1] for va, ev in held_events.items() if ev[0] == 'indirect_call')
        resolved = dict(RESOLVED)
        for site in PLACEMENT_SITES:
            if name in CONTEXT_CALLBACKS:
                resolved[site] = CONTEXT_CALLBACKS[name]
        funcs, indirect, imports, traps = closure(dis, calls, starts, resolved)
        stores = closure_stores(dis, funcs, starts)
        reach = sorted(hex(f) + ' ' + WRITERS[f] for f in funcs if f in WRITERS)
        # SEH/C++ EH frame: push handler at entry+5 (push -1; push handler).
        eh = None
        m = re.search(r'push\s+0x(6f9[0-9a-f]+)', ins.get(entry + 5, (0, ''))[1])
        if m:
            h = int(m.group(1), 16)
            # handler: mov edx,[esp+8]; ...; mov eax,FuncInfo; jmp handler
            fi = None
            for k in range(0, 40):
                tt = ins.get(h + k, (0, ''))[1]
                mm = re.match(r'mov\s+eax,0x(6f[0-9a-f]+)$', tt)
                if mm:
                    fi = int(mm.group(1), 16); break
            if fi:
                magic, states, umap = u32(fi), u32(fi + 4), u32(fi + 8)
                ntry, flags = u32(fi + 12), u32(fi + 32)
                unwind = []
                for st in range(states):
                    prev, action = u32(umap + 8 * st), u32(umap + 8 * st + 4)
                    body = [ins.get(action, (0, ''))[1], ins.get(action + 3, (0, ''))[1]]
                    unwind.append(dict(state=st, to_state=prev if prev < 0x80000000 else prev - (1 << 32),
                                       action=hex(action) if action else None, body=body if action else None))
                eh = dict(handler=hex(h), funcinfo=hex(fi), magic=hex(magic), states=states, try_blocks=ntry,
                          eh_flags=flags, unwind=unwind)
        report['scopes'].append(dict(
            name=name, entry=hex(entry), acquires=[hex(v) for v in acquires], releases=[hex(v) for v in releases],
            guards=guard_info, closure_stores=stores,
            note=note, missing_from_function_cfg=missing, held_instructions=len(held),
            exits_while_held=exits, direct_calls_while_held=[hex(c) for c in calls],
            local_indirect_calls_while_held=local_indirect,
            closure_functions=len(funcs), closure_indirect=[(hex(f), hex(v), t) for f, v, t in indirect],
            closure_imports=[(hex(f), hex(v), t) for f, v, t in imports], closure_traps=len(traps),
            writers_or_scopes_reachable=reach, eh=eh))
    report['windows_decoded_on_demand'] = dis.extra
    a.report.write_text(json.dumps(report, indent=1))
    for s in report['scopes']:
        print(s['name'], 'exits:', s['exits_while_held'], 'calls:', len(s['direct_calls_while_held']),
              'closure:', s['closure_functions'], 'indirect:', len(s['closure_indirect']),
              'imports:', [t for _, _, t in s['closure_imports']], 'reach:', s['writers_or_scopes_reachable'],
              'eh:', s['eh'] and [u['body'] for u in s['eh']['unwind']], 'missing:', s['missing_from_function_cfg'])


if __name__ == '__main__':
    main()
