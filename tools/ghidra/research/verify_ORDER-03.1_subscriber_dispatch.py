#!/usr/bin/env python3
"""ORDER-03.1 / ORDER-03.2 original-code oracle: agent event-subscriber tables under mutation.

Executes the unmodified game.dll 1.27.1.7085 code in Unicorn:
  6f0725b0 Agent_RegisterEventSubscriber (-> 6f0720a0 lazy table, 6f0725d0 table register)
  6f0728c0 Agent_UnregisterEventSubscriber (-> 6f0728e0)
  6f071dc0 Agent_DispatchEvent (agent ref pin) -> 6f071e00 core dispatcher (stack sentinel,
           tombstone reclamation at depth 1, deferred growth 6f071a90 at depth 0)
  6f071d00 Agent_ClearEventSubscribers (-> 6f071bd0 clear-or-tombstone)
plus the original fixed-size pools 6f06a320/6f06a3c0 (node pool 6fd3cce4, table pool 6fd3ccd0,
bucket-array pools 6fd3ccf8 + 0x14*i, initialized by the original 6f06a270 / 6f071680).

CONTROLLED (forced-state) inputs, labelled: agents, callback objects, their vtables and the
subscriber handler bodies are supplied. A handler body is generated x86 that only CALLS the
original functions above (register / unregister / nested dispatch / clear / release a ref);
no game.dll instruction is replaced. Storm SMemAlloc (IAT 6fa7c7ec) and CRT memset (IAT
6fa7c50c) are supplied host implementations (external modules, not game.dll).

An independent Python model (written from the assembly reading in the handoff) predicts every
delivery (callback, delivered remapped event, table depth/count, agent and callback refs),
destruction point, final bucket chain (incl. tombstones), header and pool live counts.
"""
import argparse
import hashlib
import json
import random
import struct
import sys
from pathlib import Path

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
REGISTER, UNREGISTER, DISPATCH, CLEAR = 0x6f0725b0, 0x6f0728c0, 0x6f071dc0, 0x6f071d00
NODE_POOL, TABLE_POOL, BUCKET_POOL = 0x6fd3cce4, 0x6fd3ccd0, 0x6fd3ccf8
IAT_ALLOC, IAT_MEMSET = 0x6fa7c7ec, 0x6fa7c50c
OBJ, CODE, HEAP, STACK, STOP = 0x10000000, 0x11000000, 0x12000000, 0x20000000, 0x30000000


# ----------------------------------------------------------------------------- original code
class Machine:
    def __init__(self, binary):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_INVALID
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EIP
        self.R = dict(ESP=UC_X86_REG_ESP, ECX=UC_X86_REG_ECX, EAX=UC_X86_REG_EAX, EIP=UC_X86_REG_EIP)
        pe = struct.unpack_from('<I', binary, 0x3c)[0]
        opt = pe + 24
        base, size = (struct.unpack_from('<I', binary, opt + o)[0] for o in (28, 56))
        u = self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        u.mem_map(base, (size + 4095) & ~4095)
        u.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
        for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
            s = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
            va, cnt, off = struct.unpack_from('<III', binary, s + 12)
            if cnt:
                u.mem_write(base + va, binary[off:off + cnt])
        u.mem_map(0, 0x1000)                       # native SEH list head FS:[0]
        for a, n in ((OBJ, 0x100000), (CODE, 0x100000), (HEAP, 0x1000000), (STACK, 0x10000), (STOP, 0x1000)):
            u.mem_map(a, n)
        self.heap = HEAP
        self.code_cursor = CODE + 0x100
        self.obj_cursor = OBJ
        self.labels = {}                           # code address -> callable(machine)
        # Supplied external allocator / memset (Storm and CRT are not game.dll).
        self.alloc_stub, self.memset_stub = CODE, CODE + 0x10
        u.mem_write(self.alloc_stub, b'\xc2\x10\x00')
        u.mem_write(self.memset_stub, b'\xc3')
        self.w(IAT_ALLOC, self.alloc_stub)
        self.w(IAT_MEMSET, self.memset_stub)
        self.labels[self.alloc_stub] = self._storm_alloc
        self.labels[self.memset_stub] = self._memset
        u.hook_add(UC_HOOK_CODE, self._code, begin=CODE, end=CODE + 0x100000 - 1)
        u.hook_add(UC_HOOK_MEM_INVALID, self._invalid)
        self.external_allocs = []
        # Original pool initializers (constructors 6f002440/6f002460/6f002480 minus atexit).
        self.call(0x6f06a270, NODE_POOL, 0x10, 0x400)
        self.call(0x6f071680, BUCKET_POOL, 0x100)
        self.call(0x6f06a270, TABLE_POOL, 8, 0x400)

    def _invalid(self, uc, access, address, size, value, data):
        raise RuntimeError('invalid memory access %d at %#x size %d EIP %#x' % (access, address, size, uc.reg_read(self.R['EIP'])))

    def _code(self, uc, address, size, data):
        f = self.labels.get(address)
        if f:
            f(self)

    def _storm_alloc(self, _):
        esp = self.u.reg_read(self.R['ESP'])
        amount = self.r(esp + 4)
        p = (self.heap + 15) & ~15
        self.heap = p + amount
        self.external_allocs.append(amount)
        self.u.reg_write(self.R['EAX'], p)

    def _memset(self, _):
        esp = self.u.reg_read(self.R['ESP'])
        dst, val, n = self.r(esp + 4), self.r(esp + 8) & 0xff, self.r(esp + 12)
        self.u.mem_write(dst, bytes([val]) * n)
        self.u.reg_write(self.R['EAX'], dst)

    def w(self, a, *v):
        self.u.mem_write(a, struct.pack('<' + 'I' * len(v), *(x & 0xffffffff for x in v)))

    def r(self, a):
        return struct.unpack('<I', self.u.mem_read(a, 4))[0]

    def rb(self, a):
        return self.u.mem_read(a, 1)[0]

    def rh(self, a):
        return struct.unpack('<H', self.u.mem_read(a, 2))[0]

    def obj(self, size):
        p = self.obj_cursor
        self.obj_cursor += (size + 15) & ~15
        return p

    def emit(self, code):
        p = self.code_cursor
        self.u.mem_write(p, bytes(code))
        self.code_cursor += (len(code) + 15) & ~15
        return p

    def call(self, entry, ecx, *args):
        sp = STACK + 0xf000
        self.w(sp, STOP, *args)
        self.u.reg_write(self.R['ESP'], sp)
        self.u.reg_write(self.R['ECX'], ecx)
        self.u.emu_start(entry, STOP, count=5_000_000)
        if self.u.reg_read(self.R['EIP']) != STOP:
            raise RuntimeError('instruction budget exceeded at %#x' % self.u.reg_read(self.R['EIP']))
        if self.u.reg_read(self.R['ESP']) != sp + 4 + 4 * len(args):
            raise RuntimeError('unexpected stack cleanup')
        return self.u.reg_read(self.R['EAX'])

    def pool_live(self, pool):
        return self.r(pool + 8)


def rel(at, target):
    return struct.pack('<i', target - (at + 5))


class CodeBuilder:
    """Little x86 assembler for handler bodies that CALL original code (absolute layout)."""

    def __init__(self, m):
        self.m, self.b = m, bytearray()
        self.base = m.code_cursor

    @property
    def here(self):
        return self.base + len(self.b)

    def push(self, v):
        self.b += b'\x68' + struct.pack('<I', v & 0xffffffff)

    def ecx(self, v):
        self.b += b'\xb9' + struct.pack('<I', v & 0xffffffff)

    def call(self, target):
        self.b += b'\xe8' + rel(self.here, target)

    def marker(self, fn):
        self.m.labels[self.here] = fn
        self.b += b'\x90'

    def decref(self, obj):
        # mov ecx,obj; dec dword [ecx+4]; jnz +4; mov eax,[ecx]; call [eax]
        self.ecx(obj)
        self.b += b'\xff\x49\x04\x75\x04\x8b\x01\xff\x10'

    def ret(self, value, pop):
        self.b += b'\xb8' + struct.pack('<I', value & 0xffffffff) + b'\xc2' + struct.pack('<H', pop)

    def done(self):
        p = self.m.emit(self.b)
        assert p == self.base
        return p


# ----------------------------------------------------------------------------- scenario language
# A case: agents A0.., callback objects C0.. (initial refs), initial subscriptions, top-level
# operations, and per-callback scripts: script[cb][k] = (return value, [ops]) for its k-th call.
# ops: ('reg', agent, ev, remap, cb) ('unreg', agent, ev, cb|None) ('dispatch', agent, ev)
#      ('clear', agent) ('decref', 'agent'|'cb', index)


class Original:
    def __init__(self, binary, case):
        m = self.m = Machine(binary)
        self.case = case
        self.log = []
        n_agents, n_cbs = case['agents'], len(case['cb_refs'])
        self.agent_vt = m.obj(0x20)
        self.cb_vt = [m.obj(0x20) for _ in range(n_cbs)]
        self.agents = [m.obj(0x20) for _ in range(n_agents)]
        self.cbs = [m.obj(0x20) for _ in range(n_cbs)]
        self.packets = []
        self.calls = [0] * n_cbs
        self.depth_of = {}
        agent_destroy = CodeBuilder(m)
        agent_destroy.marker(lambda _m: self.log.append(('destroy-agent', self.agents.index(_m.u.reg_read(_m.R['ECX'])))))
        agent_destroy.b += b'\xc3'
        ad = agent_destroy.done()
        cb_destroy = CodeBuilder(m)
        cb_destroy.marker(lambda _m: self.log.append(('destroy-cb', self.cbs.index(_m.u.reg_read(_m.R['ECX'])))))
        cb_destroy.b += b'\xc3'
        cd = cb_destroy.done()
        m.w(self.agent_vt, ad)
        for a in self.agents:
            m.w(a, self.agent_vt, case.get('agent_refs', 1), 0, 0)
        counters = m.obj(4 * n_cbs)
        for i, (vt, cb) in enumerate(zip(self.cb_vt, self.cbs)):
            script = case['scripts'].get(i, [])
            blocks = [self.block(ret, ops) for ret, ops in script]
            default = self.block(case.get('default_ret', 1), [])
            table = m.obj(4 * (len(blocks) + 1))
            m.w(table, *(blocks + [default]))
            entry = CodeBuilder(m)
            entry.marker(self.delivered(i))
            counter = counters + 4 * i
            # mov eax,[counter]; cmp eax,len; jbe +3? -> clamp: use cmovg via simple code
            entry.b += b'\xa1' + struct.pack('<I', counter)                    # mov eax,[counter]
            entry.b += b'\xff\x05' + struct.pack('<I', counter)                # inc dword [counter]
            entry.b += b'\x3d' + struct.pack('<I', len(blocks))                # cmp eax,len
            entry.b += b'\x76\x05'                                             # jbe +5
            entry.b += b'\xb8' + struct.pack('<I', len(blocks))                # mov eax,len
            entry.b += b'\xff\x24\x85' + struct.pack('<I', table)              # jmp [table+eax*4]
            m.w(vt, cd, 0, 0, entry.done())
            m.w(cb, vt, case['cb_refs'][i])

    def packet(self, ev):
        p = self.m.obj(0x20)
        self.m.w(p, 0, 0, ev, 0, 0, 0)
        self.packets.append(p)
        return p

    def ops(self, cb_builder, ops):
        for op in ops:
            k = op[0]
            if k == 'reg':
                _, a, ev, remap, c = op
                cb_builder.push(self.cbs[c]); cb_builder.push(remap); cb_builder.push(ev)
                cb_builder.ecx(self.agents[a]); cb_builder.call(REGISTER)
            elif k == 'unreg':
                _, a, ev, c = op
                cb_builder.push(0 if c is None else self.cbs[c]); cb_builder.push(ev)
                cb_builder.ecx(self.agents[a]); cb_builder.call(UNREGISTER)
            elif k == 'dispatch':
                _, a, ev = op
                pk = self.packet(ev)
                cb_builder.marker(lambda _m, a=a, ev=ev: self.log.append(('dispatch', a, ev)))
                cb_builder.push(pk); cb_builder.push(ev)
                cb_builder.ecx(self.agents[a]); cb_builder.call(DISPATCH)
                cb_builder.marker(lambda _m, a=a, ev=ev: self.log.append(('dispatch-ret', a, ev, _m.u.reg_read(_m.R['EAX']))))
            elif k == 'clear':
                cb_builder.ecx(self.agents[op[1]]); cb_builder.call(CLEAR)
            elif k == 'decref':
                cb_builder.decref(self.agents[op[2]] if op[1] == 'agent' else self.cbs[op[2]])
            else:
                raise ValueError(op)

    def block(self, ret, ops):
        b = CodeBuilder(self.m)
        self.ops(b, ops)
        b.ret(ret, 4)
        return b.done()

    def table_state(self, a):
        m = self.m
        t = m.r(self.agents[a] + 8)
        if t == 0:
            return None
        n, arr = m.rb(t + 1), m.r(t + 4)
        chains = []
        for i in range(n):
            tail = m.r(arr + 4 * i)
            chain = []
            if tail:
                node = m.r(tail)
                for _ in range(10000):
                    cb = m.r(node + 8)
                    chain.append([m.r(node + 4), self.cbs.index(cb) if cb else None, m.r(node + 12)])
                    if node == tail:
                        break
                    node = m.r(node)
            chains.append(chain)
        return {'depth': m.rb(t), 'buckets': n, 'count': m.rh(t + 2), 'chains': chains}

    def delivered(self, i):
        def f(m):
            esp = m.u.reg_read(m.R['ESP'])
            packet = m.r(esp + 4)
            a = self.case.get('observe_agent', 0)
            t = m.r(self.agents[a] + 8)
            self.log.append(('deliver', i, m.r(packet + 8), m.rb(t) if t else None, m.rh(t + 2) if t else None,
                             m.r(self.agents[a] + 4), m.r(self.cbs[i] + 4)))
        return f

    def run(self):
        m = self.m
        live0 = {k: m.pool_live(p) for k, p in (('node', NODE_POOL), ('table', TABLE_POOL))}
        for op in self.case['setup']:
            self.top(op)
        for op in self.case['ops']:
            self.top(op)
        result = {'log': self.log, 'tables': [self.table_state(a) for a in range(self.case['agents'])],
                  'agent_refs': [m.r(a + 4) for a in self.agents], 'cb_refs': [m.r(c + 4) for c in self.cbs],
                  'pool_live_delta': {k: m.pool_live(p) - live0[k] for k, p in (('node', NODE_POOL), ('table', TABLE_POOL))},
                  'bucket_pool_live': [m.pool_live(BUCKET_POOL + 0x14 * i) for i in range(5)]}
        return result

    def top(self, op):
        b = CodeBuilder(self.m)
        self.ops(b, [op])
        b.b += b'\xc3'
        entry = b.done()
        self.m.call(entry, 0)


# ----------------------------------------------------------------------------- independent model
class Node:
    __slots__ = ('next', 'ev', 'cb', 'remap', 'sentinel')

    def __init__(self, ev=0, cb=None, remap=0, sentinel=False):
        self.next, self.ev, self.cb, self.remap, self.sentinel = None, ev, cb, remap, sentinel


class Table:
    def __init__(self):
        self.depth, self.n, self.count = 0, 4, 0
        self.b = [None] * 4                         # bucket -> tail node


class Model:
    """Re-implementation of 0725d0/0728e0/071dc0/071e00/071bd0/071a90 from the assembly reading."""

    def __init__(self, case):
        self.case = case
        self.tables = [None] * case['agents']
        self.agent_refs = [case.get('agent_refs', 1)] * case['agents']
        self.cb_refs = list(case['cb_refs'])
        self.calls = [0] * len(case['cb_refs'])
        self.log = []
        self.nodes_live = 0
        self.tables_live = 0
        self.bucket_live = [0] * 5

    @staticmethod
    def idx(n):
        i = 0
        while n > 4:
            n >>= 1
            i += 1
        return i

    def release_cb(self, c):
        if c is None:
            return
        self.cb_refs[c] -= 1
        if self.cb_refs[c] == 0:
            self.log.append(('destroy-cb', c))

    def free_node(self, node):
        self.nodes_live -= 1

    def unlink(self, t, bi, prev, node):
        """prev->next = node->next with the tail fix-up used by every original unlink site."""
        nxt = node.next
        prev.next = nxt
        if node is t.b[bi]:
            t.b[bi] = None if prev is node else prev
            # original: if cur==tail: if prev==tail -> bucket=0 (single element) else bucket=prev
        node.next = None
        return nxt

    def walk(self, t, bi):
        tail = t.b[bi]
        if tail is None:
            return None, None
        return tail, tail.next

    def table(self, a, create):
        if self.tables[a] is None and create:
            self.tables[a] = Table()
            self.tables_live += 1
            self.bucket_live[0] += 1
        return self.tables[a]

    def grow_check(self, t):
        if t.count > 4 * t.n and t.n < 0x40:
            # collect buckets n-1 .. 0 by splicing circular lists -> order bucket0, bucket1, ...
            order = []
            for i in range(t.n):
                tail = t.b[i]
                if tail is None:
                    continue
                node = tail.next
                while True:
                    order.append(node)
                    if node is tail:
                        break
                    node = node.next
            self.bucket_live[self.idx(t.n)] -= 1
            t.n *= 2
            self.bucket_live[self.idx(t.n)] += 1
            t.b = [None] * t.n
            for node in order:
                node.next = None
                if node.cb is None:
                    self.free_node(node)
                    continue
                bi = node.ev & (t.n - 1)
                tail = t.b[bi]
                if tail is None:
                    node.next = node
                else:
                    node.next = tail.next
                    tail.next = node
                t.b[bi] = node

    def register(self, a, ev, remap, cb):
        t = self.table(a, True)
        bi = ev & (t.n - 1)
        tail = t.b[bi]
        found = False
        if tail is not None:
            prev, node = tail, tail.next
            while node is not None:
                if node.cb is None:
                    if t.depth == 0:
                        nxt = node.next
                        last = node is t.b[bi]
                        self.unlink(t, bi, prev, node)
                        self.free_node(node)
                        node = None if last else nxt
                        continue
                elif node.ev == ev:
                    found = True
                    if node.cb == cb:
                        node.remap = remap                 # existing: update remap, ref +1 -1
                        return
                if node is t.b[bi]:
                    break
                prev, node = node, node.next
        node = Node(ev, cb, remap)
        self.nodes_live += 1
        tail = t.b[bi]
        if tail is None:
            node.next = node
        else:
            node.next = tail.next
            tail.next = node
        t.b[bi] = node
        self.cb_refs[cb] += 1
        if not found:
            t.count += 1
            if t.depth == 0:
                self.grow_check(t)

    def unregister(self, a, ev, cb):
        t = self.tables[a]
        if t is None:
            return
        bi = ev & (t.n - 1)
        tail = t.b[bi]
        if tail is None:
            return
        found = other = False
        prev, node = tail, tail.next
        while node is not None:
            if node.cb is None:
                if t.depth == 0:
                    nxt = node.next
                    last = node is t.b[bi]
                    self.unlink(t, bi, prev, node)
                    self.free_node(node)
                    node = None if last else nxt
                    continue
            elif node.ev == ev:
                if cb is None or node.cb == cb:
                    found = True
                    c = node.cb
                    if t.depth == 0:
                        nxt = node.next
                        last = node is t.b[bi]
                        self.unlink(t, bi, prev, node)
                        self.release_cb(c)
                        self.free_node(node)
                        if other:
                            return
                        node = None if last else nxt
                        continue
                    self.release_cb(c)
                    node.cb = None
                    if other:
                        return
                else:
                    other = True
                    if found:
                        return
            if node is t.b[bi]:
                break
            prev, node = node, node.next
        if found and not other:
            t.count -= 1

    def clear(self, a):
        t = self.tables[a]
        if t is None:
            return
        for bi in range(t.n - 1, -1, -1):
            tail = t.b[bi]
            if tail is None:
                continue
            prev, node = tail, tail.next
            while node is not None:
                last = node is t.b[bi]
                if t.depth == 0:
                    nxt = node.next
                    self.unlink(t, bi, prev, node)
                    c = node.cb
                    self.release_cb(c)
                    self.free_node(node)
                    node = None if last else nxt
                    continue
                c = node.cb
                node.cb = None
                self.release_cb(c)
                if last:
                    break
                prev, node = node, node.next
        t.count = 0
        if t.depth == 0:
            self.bucket_live[self.idx(t.n)] -= 1
            self.tables_live -= 1
            self.tables[a] = None

    def dispatch(self, a, ev):
        self.log.append(('dispatch', a, ev))
        t = self.tables[a]
        if t is None:
            self.log.append(('dispatch-ret', a, ev, 0))
            return 0
        self.agent_refs[a] += 1
        result = self.core(a, t, ev)
        self.agent_refs[a] -= 1
        if self.agent_refs[a] == 0:
            self.log.append(('destroy-agent', a))
        self.log.append(('dispatch-ret', a, ev, result))
        return result

    def core(self, a, t, ev):
        t.depth += 1
        saved = t.count
        bi = ev & (t.n - 1)
        s = Node(sentinel=True)
        tail = t.b[bi]
        if tail is None:
            s.next = s
        else:
            s.next = tail.next
            tail.next = s
        t.b[bi] = s
        result = 0
        prev, node = s, s.next
        while node is not s:
            if node.cb is None:
                if t.depth == 1:
                    nxt = node.next
                    self.unlink(t, bi, prev, node)
                    self.free_node(node)
                    node = nxt
                    continue
            elif node.ev == ev:
                c = node.cb
                self.deliver(c, node.remap)
                r = self.invoke(c)
                if r:
                    result = 1
            prev, node = node, node.next
        # 072820: unlink the sentinel with the last visited predecessor
        self.unlink(t, bi, prev, s)
        t.depth -= 1
        if t.depth == 0 and t.count > saved:
            self.grow_check(t)
        return result

    def deliver(self, c, remap):
        a = self.case.get('observe_agent', 0)
        t = self.tables[a]
        self.log.append(('deliver', c, remap, t.depth if t else None, t.count if t else None, self.agent_refs[a], self.cb_refs[c]))

    def invoke(self, c):
        script = self.case['scripts'].get(c, [])
        k = self.calls[c]
        self.calls[c] += 1
        if k < len(script):
            ret, ops = script[k]
        else:
            ret, ops = self.case.get('default_ret', 1), []
        for op in ops:
            self.op(op)
        return ret

    def op(self, op):
        k = op[0]
        if k == 'reg':
            self.register(*op[1:])
        elif k == 'unreg':
            self.unregister(*op[1:])
        elif k == 'dispatch':
            self.dispatch(*op[1:])
        elif k == 'clear':
            self.clear(op[1])
        elif k == 'decref':
            if op[1] == 'agent':
                self.agent_refs[op[2]] -= 1
                if self.agent_refs[op[2]] == 0:
                    self.log.append(('destroy-agent', op[2]))
            else:
                self.cb_refs[op[2]] -= 1
                if self.cb_refs[op[2]] == 0:
                    self.log.append(('destroy-cb', op[2]))

    def table_state(self, a):
        t = self.tables[a]
        if t is None:
            return None
        chains = []
        for i in range(t.n):
            tail = t.b[i]
            chain = []
            if tail is not None:
                node = tail.next
                while True:
                    chain.append([node.ev, node.cb, node.remap])
                    if node is tail:
                        break
                    node = node.next
            chains.append(chain)
        return {'depth': t.depth, 'buckets': t.n, 'count': t.count, 'chains': chains}

    def run(self, nodes0=0):
        for op in self.case['setup'] + self.case['ops']:
            self.op(op)
        bl = [0] * 5
        return {'log': self.log, 'tables': [self.table_state(a) for a in range(self.case['agents'])],
                'agent_refs': self.agent_refs, 'cb_refs': self.cb_refs,
                'pool_live_delta': {'node': self.nodes_live, 'table': self.tables_live}, 'bucket_live': self.bucket_live}


# ----------------------------------------------------------------------------- cases
E, F, G = 0x8024c, 0x80250, 0x8024b          # E and F share bucket 0 of 4; G is bucket 3


def base_case(name, n_cbs, scripts, ops=None, setup=None, cb_refs=None, group='03.1', **kw):
    setup = setup if setup is not None else [('reg', 0, E, 0xd0000 + i, i) for i in range(4)]
    return dict(name=name, group=group, agents=kw.pop('agents', 1), cb_refs=cb_refs or [1] * n_cbs, setup=setup,
                ops=ops or [('dispatch', 0, E), ('dispatch', 0, E)], scripts=scripts, **kw)


def named_cases():
    cs = []
    S = lambda *ops: [(1, list(ops))]                 # first invocation runs ops, returns 1
    # --- ORDER-03.1: insertion/removal while dispatching to four subscribers C0..C3 (+C4 spare)
    cs.append(base_case('baseline-four', 5, {}))
    cs.append(base_case('insert-same-event-from-first', 5, {0: S(('reg', 0, E, 0xd0004, 4))}))
    cs.append(base_case('insert-same-event-from-last', 5, {3: S(('reg', 0, E, 0xd0004, 4))}))
    cs.append(base_case('insert-other-event-same-bucket', 5, {0: S(('reg', 0, F, 0xd0004, 4))},
                        ops=[('dispatch', 0, E), ('dispatch', 0, F), ('dispatch', 0, E)]))
    cs.append(base_case('remove-later', 5, {0: S(('unreg', 0, E, 2))}))
    cs.append(base_case('remove-earlier', 5, {2: S(('unreg', 0, E, 0))}))
    cs.append(base_case('remove-self', 5, {1: S(('unreg', 0, E, 1))}))
    cs.append(base_case('remove-all-for-event', 5, {1: S(('unreg', 0, E, None))}))
    cs.append(base_case('remove-then-reinsert-later', 5, {0: S(('unreg', 0, E, 2), ('reg', 0, E, 0xd00f2, 2))}))
    cs.append(base_case('reregister-existing-updates-remap', 5, {0: S(('reg', 0, E, 0xd00ee, 3))}))
    cs.append(base_case('duplicate-callback-other-event', 5, {0: S(('reg', 0, F, 0xd00f0, 0))},
                        ops=[('dispatch', 0, E), ('dispatch', 0, F)]))
    cs.append(base_case('last-ref-callback-removed-during-dispatch', 5, {0: S(('unreg', 0, E, 2))}, cb_refs=[0, 0, 0, 0, 0]))
    cs.append(base_case('self-removal-last-ref-destroys-running-callback', 5, {1: S(('unreg', 0, E, 1))}, cb_refs=[0, 0, 0, 0, 0]))
    cs.append(base_case('return-values-or', 5, {0: [(0, [])], 1: [(0, [])], 2: [(0, [])], 3: [(0, [])]},
                        ops=[('dispatch', 0, E), ('dispatch', 0, G)]))
    cs.append(base_case('growth-deferred-to-depth-zero', 30,
                        {0: S(*[('reg', 0, 0x80300 + 4 * i, 0xd1000 + i, 5 + (i % 20)) for i in range(20)])}))
    cs.append(base_case('growth-at-depth-zero-immediate', 30, {},
                        setup=[('reg', 0, E, 0xd0000 + i, i) for i in range(4)] +
                              [('reg', 0, 0x80300 + 4 * i, 0xd1000 + i, 5 + (i % 20)) for i in range(20)]))
    cs.append(base_case('tombstones-survive-growth-are-freed', 30,
                        {0: S(('unreg', 0, E, 3), *[('reg', 0, 0x80300 + 4 * i, 0xd1000 + i, 5 + (i % 20)) for i in range(20)])}))
    # --- ORDER-03.2: destroy subscriber owners / the agent and nested dispatch
    cs.append(base_case('nested-same-event-sees-inserted', 6,
                        {0: [(1, [('reg', 0, E, 0xd0005, 5), ('dispatch', 0, E)]), (1, [('unreg', 0, E, 2)])]}, group='03.2'))
    cs.append(base_case('nested-removal-tombstone-reclaimed-by-outer', 6,
                        {1: [(1, [('dispatch', 0, E)]), (1, [('unreg', 0, E, 3)])]}, group='03.2'))
    cs.append(base_case('nested-other-event-same-bucket', 6,
                        {0: S(('reg', 0, F, 0xd0005, 5), ('dispatch', 0, F))}, group='03.2'))
    cs.append(base_case('three-level-nesting', 6,
                        {0: [(1, [('dispatch', 0, E)]), (1, [('dispatch', 0, E)]), (1, [('unreg', 0, E, 1), ('reg', 0, E, 0xd0005, 5)])]},
                        ops=[('dispatch', 0, E), ('dispatch', 0, E)], group='03.2'))
    cs.append(base_case('clear-all-during-dispatch', 5, {1: S(('clear', 0))}, group='03.2'))
    cs.append(base_case('clear-all-in-nested-dispatch', 5, {1: [(1, [('dispatch', 0, E)]), (1, [('clear', 0)])]}, group='03.2'))
    cs.append(base_case('agent-last-ref-released-during-dispatch', 5, {1: S(('clear', 0), ('decref', 'agent', 0))},
                        ops=[('dispatch', 0, E)], group='03.2'))
    cs.append(base_case('register-after-clear-during-dispatch', 5, {1: S(('clear', 0), ('reg', 0, E, 0xd0004, 4))}, group='03.2'))
    cs.append(base_case('cross-agent-nested', 6, {0: S(('dispatch', 1, E))}, agents=2,
                        setup=[('reg', 0, E, 0xd0000 + i, i) for i in range(4)] +
                              [('reg', 1, E, 0xd0004, 4), ('reg', 1, E, 0xd0005, 5)],
                        scripts_note='agent 1 subscriber 4 removes agent-0 subscriber 2', group='03.2')
              | {'scripts': {0: S(('dispatch', 1, E)), 4: S(('unreg', 0, E, 2))}})
    cs.append(base_case('dispatch-without-table', 2, {}, setup=[], ops=[('dispatch', 0, E)], group='03.2'))
    return cs


def random_cases(seed, count):
    rng = random.Random(seed)
    events = [E, F, G, 0x8024d, 0x80300, 0x80304]
    out = []
    for n in range(count):
        n_cbs = 8
        setup = []
        for i in range(rng.randint(2, 7)):
            setup.append(('reg', 0, rng.choice(events[:3]), 0xd0000 + rng.randrange(0x100), rng.randrange(n_cbs)))
        if n % 2:      # near the 4*buckets growth threshold so in-dispatch registrations cross it
            for k in range(rng.randint(12, 15)):
                setup.append(('reg', 0, 0x80600 + 4 * k + rng.randrange(4), 0xd3000 + k, rng.randrange(n_cbs)))
        scripts = {}
        budget = [6]

        def ops_for(depth):
            ops = []
            for _ in range(rng.randint(0, 3)):
                k = rng.random()
                if k < 0.35:
                    ops.append(('reg', 0, rng.choice(events), 0xd0000 + rng.randrange(0x100), rng.randrange(n_cbs)))
                elif k < 0.65:
                    ops.append(('unreg', 0, rng.choice(events[:3]), rng.choice([None] + list(range(n_cbs)))))
                elif k < 0.85 and budget[0] > 0 and depth < 3:
                    budget[0] -= 1
                    ops.append(('dispatch', 0, rng.choice(events[:3])))
                elif k < 0.9:
                    ops.append(('clear', 0))
                else:
                    ops.append(('reg', 0, 0x80400 + 4 * rng.randrange(40), 0xd2000, rng.randrange(n_cbs)))
            return ops
        for c in range(n_cbs):
            scripts[c] = [(rng.randint(0, 1), ops_for(1)) for _ in range(rng.randint(0, 3))]
        ops = [('dispatch', 0, rng.choice(events[:3])) for _ in range(rng.randint(1, 3))]
        out.append(dict(name='random-%d-%d' % (seed, n), group='random', agents=1, cb_refs=[rng.randint(0, 2) for _ in range(n_cbs)],
                        setup=setup, ops=ops, scripts=scripts))
    return out


def jsonable(x):
    if isinstance(x, tuple):
        return [jsonable(v) for v in x]
    if isinstance(x, list):
        return [jsonable(v) for v in x]
    if isinstance(x, dict):
        return {str(k): jsonable(v) for k, v in x.items()}
    return x


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--random', type=int, default=400)
    ap.add_argument('--seed', type=int, default=3031)
    ap.add_argument('--expected', type=Path, help='compare named-case results with a frozen expected file')
    args = ap.parse_args()
    binary = args.binary.read_bytes()
    if hashlib.sha256(binary).hexdigest() != SHA:
        ap.error('requires game.dll 1.27.1.7085')
    cases = named_cases() + random_cases(args.seed, args.random)
    results, mismatches, stats = [], [], {'deliveries': 0, 'tombstones_final': 0, 'nested_dispatches': 0,
                                          'destroy_cb': 0, 'destroy_agent': 0, 'growths': 0, 'crash': 0}
    for case in cases:
        model = Model(case).run()
        try:
            orig = Original(binary, case).run()
        except Exception as error:      # an original-code fault is a result, not a harness error
            orig = {'fault': str(error)}
            stats['crash'] += 1
        o = jsonable(orig)
        md = jsonable(model)
        ok = 'fault' not in o and all(o[k] == md[k] for k in ('log', 'tables', 'agent_refs', 'cb_refs')) \
            and o['pool_live_delta'] == md['pool_live_delta']
        bucket_ok = 'fault' not in o and o['bucket_pool_live'] == md['bucket_live']
        if not (ok and bucket_ok):
            mismatches.append({'case': case['name'], 'original': o, 'model': md})
        stats['deliveries'] += sum(1 for e in md['log'] if e[0] == 'deliver')
        stats['nested_dispatches'] += max(0, sum(1 for e in md['log'] if e[0] == 'dispatch') - len(case['ops']))
        stats['destroy_cb'] += sum(1 for e in md['log'] if e[0] == 'destroy-cb')
        stats['destroy_agent'] += sum(1 for e in md['log'] if e[0] == 'destroy-agent')
        stats['tombstones_final'] += sum(1 for t in md['tables'] if t for ch in t['chains'] for n in ch if n[1] is None)
        stats['growths'] += sum(1 for t in md['tables'] if t and t['buckets'] > 4)
        if case['group'] != 'random':
            results.append({'name': case['name'], 'group': case['group'],
                            'case': jsonable({k: v for k, v in case.items() if k != 'scripts_note'}), 'original': o})
    report = {'binary_sha256': SHA, 'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'cases': len(cases), 'named': len(results), 'random': args.random, 'seed': args.seed,
              'mismatches': len(mismatches), 'stats': stats, 'named_results': results, 'mismatch_detail': mismatches[:5]}
    args.report.write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps({k: report[k] for k in ('cases', 'named', 'random', 'mismatches', 'stats')}))
    if args.expected:
        exp = json.loads(args.expected.read_text())
        frozen = {r['name']: r['original'] for r in exp['oracle']['named_results']}
        now = {r['name']: r['original'] for r in results if r['name'] in frozen}
        if frozen != now:
            print('named results differ from frozen expected', file=sys.stderr)
            sys.exit(2)
    sys.exit(1 if mismatches else 0)


if __name__ == '__main__':
    main()
