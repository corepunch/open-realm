#!/usr/bin/env python3
"""ORDER-05.1 / 05.2 / 05.3 original-code oracle: agent request clocks (deferred heap).

Executes the unmodified game.dll 1.27.1.7085 routines in Unicorn:
  6f15e310 SimClock_QueueAgentRequest (free list +38 / count +3c, serial, heap push 6f04f990)
  6f15dc30 SimClock_ScheduleRequest   (delay clamp to 6fcd539c, ++serial +50, Math_Add 6f06fbb0)
  6f15d6f0 AgentWrapper_StartTimer    (cancel previous +1c, clock by identity sign 6f15c650, repeat flag)
  6f15d7a0 AgentWrapper_StopTimer     6f15e0e0 AgentWrapper_ScheduleRelease (idempotent through +20)
  6f052380 SimClock_DrainDueRequests  6f04f3c0 heap pop   6f054370 SimClock_ExecuteRequest
  6f053710 SimClock_RearmAgentRequest 6f054190 SimClock_AdvanceRequests 6f0521f0 SimClock_RebaseRequestDeadlines

CONTROLLED (forced-state, labelled): the owner object (global 6fd53a48) with its two request clocks
(+14 primary, +68 presentation), clock fields, preallocated request blocks, wrapper objects and the
receivers' handler bodies (generated x86 that only CALLS the original routines above). Storm allocation
and memset are supplied host implementations of external modules. Deadline words in the independent
model are produced by the ORIGINAL Math_Add 6f06fbb0 (software scalar arithmetic is not re-derived).

The model predicts: every callback (receiver, request creation index, deadline word, clock word seen by
the callback, flags, serial, value), silent pops of cancelled requests, rearms, request-block reuse
(free-list LIFO), heap count, live request count, clock time/epoch after each operation.
"""
import argparse
import hashlib
import importlib.util
import json
import random
import struct
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location('o31', Path(__file__).with_name('verify_ORDER-03.1_subscriber_dispatch.py'))
o31 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(o31)

SHA = o31.SHA
OWNER_GLOBAL = 0x6fd53a48
QUEUE, SCHEDULE, START, STOP_T, RELEASE = 0x6f15e310, 0x6f15dc30, 0x6f15d6f0, 0x6f15d7a0, 0x6f15e0e0
DRAIN, ADVANCE, REBASE, MATH_ADD, MATH_SUB = 0x6f052380, 0x6f054190, 0x6f0521f0, 0x6f06fbb0, 0x6f06fa90
ON_REQUEST = 0x6f15e500
EPS = 0x3556bf95                     # 6fcd5478 wrap remainder snap threshold
MIN_DELAY = 0x38d1b717
CLOCK_OFFSETS = (0x14, 0x68)
BLOCK = 0x28
BLOCKS = 192


def f32(w):
    return struct.unpack('<f', struct.pack('<I', w & 0xffffffff))[0]


def bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def lt_deadline(a, b):
    """Strict deadline order as the heap's UCOMISS/COMISS compare (equal incl. -0 == +0)."""
    return f32(a) < f32(b)


class Scalar:
    """Original Math_Add executed in its own Unicorn instance (used by the model)."""

    def __init__(self, binary):
        self.m = o31.Machine(binary)
        self.cache = {}

    def add(self, a, b):
        return self.op(MATH_ADD, a, b)

    def sub(self, a, b):
        return self.op(MATH_SUB, a, b)

    def op(self, fn, a, b):
        key = (fn, a, b)
        if key not in self.cache:
            m = self.m
            pa, pb, out = o31.OBJ, o31.OBJ + 4, o31.OBJ + 8
            m.w(pa, a)
            m.w(pb, b)
            from unicorn.x86_const import UC_X86_REG_EDX
            m.u.reg_write(UC_X86_REG_EDX, pa)
            m.call(fn, out, pb)
            self.cache[key] = m.r(out)
        return self.cache[key]


# ----------------------------------------------------------------------------- original
def watch(m, address, fn):
    """Observation-only hook on an original instruction (does not alter state)."""
    from unicorn import UC_HOOK_CODE
    m.u.hook_add(UC_HOOK_CODE, lambda uc, a, size, data: fn(m), begin=address, end=address)


class Original:
    """Receivers are supplied wrapper objects whose vt+48 handler is:
         log; ORIGINAL AgentWrapper_OnClockRequest 6f15e500(request); scripted ops for the k-th call; ret 4
    with wrapper flags +4c = 0x10000 (repeat) or 0 (no signal flags), so 15e500 either calls vt+10 for the
    wrapper's own release request (+20) or stops the wrapper's own fired one-shot timer (+1c) via 6f15d7a0.
    Supplied vt+10 (stands in for the teardown 6f15d840, two of whose effects it reproduces): original
    StopTimer 6f15d7a0, then cancel and clear +20; logs 'destroy'."""

    def __init__(self, binary, case):
        m = self.m = o31.Machine(binary)
        self.case = case
        self.log = []
        self.req_index = {}       # request pointer -> creation index (reassigned on block reuse)
        self.created = 0
        self.owner = m.obj(0x100)
        m.w(OWNER_GLOBAL, self.owner)
        self.clocks = [self.owner + off for off in CLOCK_OFFSETS]
        self.blocks = m.obj(BLOCK * BLOCKS * 2)
        self.block_base = [self.blocks + ci * BLOCK * BLOCKS for ci in range(2)]
        for ci, clock in enumerate(self.clocks):
            arr = m.obj(4 * 520)
            base = self.block_base[ci]
            for i in range(BLOCKS):
                m.w(base + i * BLOCK, base + (i + 1) * BLOCK if i + 1 < BLOCKS else 0)
            m.w(clock + 0x10, arr)
            m.w(clock + 0x18, 0x10, 512, 1)                    # growth, capacity, count (slot 0 unused)
            m.w(clock + 0x38, base, 0)                           # free list head, live requests
            st = case['clock'][ci]
            m.w(clock + 0x40, st['time'], st.get('epoch', 0), st.get('span', bits(300.0)), st.get('flags', 0), st.get('serial', 0))
        n = case['wrappers']
        self.wrappers = [m.obj(0x60) for _ in range(n)]
        counters = m.obj(4 * n)
        for i, w in enumerate(self.wrappers):
            spec_w = case['wrapper_spec'][i]
            blocks = [self.block(ops) for ops in case['scripts'].get(i, [])]
            default = self.block([])
            table = m.obj(4 * (len(blocks) + 1))
            m.w(table, *(blocks + [default]))
            entry = o31.CodeBuilder(m)
            entry.marker(self.callback(i))
            entry.b += b'\xff\x74\x24\x04'                                      # push [esp+4] (request)
            entry.ecx(w)
            entry.call(ON_REQUEST)
            counter = counters + 4 * i
            entry.b += b'\xa1' + struct.pack('<I', counter) + b'\xff\x05' + struct.pack('<I', counter)
            entry.b += b'\x3d' + struct.pack('<I', len(blocks)) + b'\x76\x05' + b'\xb8' + struct.pack('<I', len(blocks))
            entry.b += b'\xff\x24\x85' + struct.pack('<I', table)
            handler = entry.done()
            destroy = o31.CodeBuilder(m)
            destroy.marker(lambda _m, i=i: self.log.append(['destroy', i]))
            destroy.b += b'\x56\x8b\xf1'                                        # push esi; mov esi,ecx
            destroy.call(STOP_T)
            destroy.b += b'\x8b\x46\x20\x81\x48\x10\x00\x00\x01\x00'            # mov eax,[esi+20]; or [eax+10],10000
            destroy.b += b'\xc7\x46\x20\x00\x00\x00\x00\x5e\xc2\x04\x00'        # mov [esi+20],0; pop esi; ret 4
            destroy_entry = destroy.done()
            vt = m.obj(0x60)
            m.w(vt + 0x10, destroy_entry)
            m.w(vt + 0x48, handler)
            m.w(w, vt, 1)
            m.w(w + 0x14, spec_w.get('slot', i + 1))
            m.w(w + 0x4c, 0x10000 if spec_w.get('repeat') else 0)
        watch(m, 0x6f15e387, self._queued)          # after heap push: ESI = new request
        watch(m, 0x6f054370, self._execute)         # every popped request: ECX = request
        watch(m, 0x6f053742, self._rearm)           # rearm heap push: ESI = request (new deadline stored)
        watch(m, REBASE, self._rebase)

    def rid(self, req):
        return self.req_index.get(req)

    def ci_of(self, clock):
        return self.clocks.index(clock)

    def _queued(self, m):
        from unicorn.x86_const import UC_X86_REG_ESI
        req = m.u.reg_read(UC_X86_REG_ESI)
        self.req_index[req] = self.created
        ci = self.ci_of(m.r(req + 0xc))
        recv = m.r(req + 0x18)
        self.log.append(['queue', self.created, ci, (req - 4 - self.block_base[ci]) // BLOCK, m.r(req + 4), m.r(req + 8),
                         m.r(req + 0x14), self.wrappers.index(recv) if recv in self.wrappers else None, m.r(req + 0x1c)])
        self.created += 1

    def _execute(self, m):
        req = m.u.reg_read(m.R['ECX'])
        self.log.append(['pop', self.rid(req), m.r(req + 4), m.r(m.r(req + 0xc) + 0x40), m.r(req + 0x10)])

    def _rearm(self, m):
        from unicorn.x86_const import UC_X86_REG_ESI
        req = m.u.reg_read(UC_X86_REG_ESI)
        self.log.append(['rearm', self.rid(req), m.r(req + 4), m.r(req + 0x10)])

    def _rebase(self, m):
        clock = m.u.reg_read(m.R['ECX'])
        self.log.append(['rebase', self.ci_of(clock), m.r(clock + 0x40), m.r(clock + 0x44), m.r(clock + 0x20) - 1])

    def callback(self, i):
        def f(m):
            req = m.r(m.u.reg_read(m.R['ESP']) + 4)
            clock = m.r(req + 0xc)
            self.log.append(['call', i, self.rid(req), m.r(req + 4), m.r(clock + 0x40), m.r(req + 0x10), m.r(req + 0x14), m.r(req + 0x1c)])
        return f

    def ops(self, b, ops):
        m = self.m
        for op in ops:
            k = op[0]
            if k == 'sched':                 # ('sched', clock, wrapper, delay_word, value): original 6f15dc30
                _, ci, wi, delay, value = op
                pd, pv = m.obj(4), m.obj(4)
                m.w(pd, delay)
                m.w(pv, value)
                b.push(pd); b.push(pv); b.push(self.wrappers[wi]); b.ecx(self.clocks[ci]); b.call(SCHEDULE)
            elif k == 'start':               # ('start', wrapper, period_word, value): original 6f15d6f0
                _, wi, period, value = op
                pp = m.obj(4)
                m.w(pp, period)
                b.push(value); b.push(pp); b.ecx(self.wrappers[wi]); b.call(START)
            elif k == 'stop':
                b.ecx(self.wrappers[op[1]]); b.call(STOP_T)
            elif k == 'release':
                b.ecx(self.wrappers[op[1]]); b.call(RELEASE)
            elif k == 'drain':               # original 6f052380 at the clock's current time
                ci = op[1]
                b.marker(lambda _m, ci=ci: self.log.append(['drain', ci, _m.r(self.clocks[ci] + 0x40)]))
                b.ecx(self.clocks[ci]); b.call(DRAIN)
            elif k == 'advance':             # ('advance', clock, increment_word): original 6f054190 (fastcall)
                ci, inc = op[1], op[2]
                pi = m.obj(4)
                m.w(pi, inc)
                b.marker(lambda _m, ci=ci: self.log.append(['advance', ci, _m.r(self.clocks[ci] + 0x40), _m.r(self.clocks[ci] + 0x44)]))
                b.b += b'\xba' + struct.pack('<I', self.clocks[ci])
                b.ecx(pi); b.call(ADVANCE)
            elif k == 'settime':             # FORCED: write the clock time word
                ci, t = op[1], op[2]
                b.marker(lambda _m, ci=ci, t=t: _m.w(self.clocks[ci] + 0x40, t))
            elif k == 'setslot':             # FORCED: write the wrapper identity word (+14) -> clock selection 6f15c650
                wi, slot = op[1], op[2]
                b.marker(lambda _m, wi=wi, slot=slot: _m.w(self.wrappers[wi] + 0x14, slot))
            else:
                raise ValueError(op)

    def block(self, ops):
        b = o31.CodeBuilder(self.m)
        self.ops(b, ops)
        b.ret(1, 4)
        return b.done()

    def state(self):
        m = self.m
        out = []
        for ci, clock in enumerate(self.clocks):
            count = m.r(clock + 0x20)
            arr = m.r(clock + 0x10)
            reqs = [m.r(arr + 4 * i) for i in range(1, count)]
            pending = sorted([[self.rid(r), m.r(r + 4), m.r(r + 0x10)] for r in reqs])
            free, head = [], m.r(clock + 0x38)
            while head and len(free) < 8:
                free.append((head - self.block_base[ci]) // BLOCK)
                head = m.r(head)
            out.append({'time': m.r(clock + 0x40), 'epoch': m.r(clock + 0x44), 'serial': m.r(clock + 0x50), 'count': count,
                        'live': m.r(clock + 0x3c), 'pending': pending, 'free_head': free})
        wr = [[self.rid(m.r(w + 0x1c)) if m.r(w + 0x1c) else None, self.rid(m.r(w + 0x20)) if m.r(w + 0x20) else None,
               m.r(w + 0x4c)] for w in self.wrappers]
        return {'clocks': out, 'wrappers': wr}

    def run(self):
        for op in self.case['ops']:
            b = o31.CodeBuilder(self.m)
            self.ops(b, [op])
            b.b += b'\xc3'
            self.m.call(b.done(), 0)
        return {'log': self.log, 'state': self.state()}


# ----------------------------------------------------------------------------- model
class Model:
    def __init__(self, case, scalar):
        self.case, self.S = case, scalar
        self.log = []
        self.created = 0
        self.clocks = []
        for ci in range(2):
            st = case['clock'][ci]
            self.clocks.append({'ci': ci, 'time': st['time'], 'epoch': st.get('epoch', 0), 'span': st.get('span', bits(300.0)),
                                'flags': st.get('flags', 0), 'serial': st.get('serial', 0), 'heap': [],
                                'free': list(range(BLOCKS)), 'live': 0})
        n = case['wrappers']
        self.wspec = case['wrapper_spec']
        self.slot = [w.get('slot', i + 1) for i, w in enumerate(self.wspec)]
        self.w = [{'timer': None, 'release': None} for _ in range(n)]
        self.calls = [0] * n

    def clock_of(self, wi):
        return 1 if self.slot[wi] & 0x80000000 else 0

    def sched(self, ci, wi, delay, value):
        c = self.clocks[ci]
        if f32(MIN_DELAY) > f32(delay):
            delay = MIN_DELAY
        c['serial'] = (c['serial'] + 1) & 0xffffffff
        block = c['free'].pop(0)
        c['live'] += 1
        r = {'idx': self.created, 'block': block, 'deadline': self.S.add(c['time'], delay), 'period': delay,
             'serial': c['serial'], 'recv': wi, 'value': value, 'flags': 0x20000}
        self.created += 1
        self.log.append(['queue', r['idx'], ci, block, r['deadline'], delay, r['serial'], wi, value])
        c['heap'].append(r)
        return r

    def stop(self, wi):
        t = self.w[wi]['timer']
        if t is not None:
            t['flags'] |= 0x10000
            self.w[wi]['timer'] = None

    def op(self, op):
        k = op[0]
        if k == 'sched':
            self.sched(op[1], op[2], op[3], op[4])
        elif k == 'start':
            wi, period, value = op[1], op[2], op[3]
            if self.w[wi]['timer'] is not None:
                self.w[wi]['timer']['flags'] |= 0x10000
            r = self.sched(self.clock_of(wi), wi, period, value)
            self.w[wi]['timer'] = r
            if self.wspec[wi].get('repeat'):
                r['flags'] |= 1
        elif k == 'stop':
            self.stop(op[1])
        elif k == 'release':
            wi = op[1]
            if self.w[wi]['release'] is None:
                self.w[wi]['release'] = self.sched(self.clock_of(wi), wi, 0, 0)
        elif k == 'drain':
            self.log.append(['drain', op[1], self.clocks[op[1]]['time']])
            self.drain(self.clocks[op[1]])
        elif k == 'advance':
            self.advance(self.clocks[op[1]], op[2])
        elif k == 'settime':
            self.clocks[op[1]]['time'] = op[2]
        elif k == 'setslot':
            self.slot[op[1]] = op[2]
        else:
            raise ValueError(op)

    @staticmethod
    def before(a, b):
        da, db = f32(a['deadline']), f32(b['deadline'])
        if da == db:
            return a['serial'] < b['serial']
        return db > da

    def drain(self, c):
        target = c['time']
        while c['heap']:
            r = c['heap'][0]
            for x in c['heap'][1:]:
                if self.before(x, r):
                    r = x
            if not f32(target) >= f32(r['deadline']):
                break
            c['heap'].remove(r)
            c['time'] = r['deadline']
            r['flags'] &= ~0x20000
            self.log.append(['pop', r['idx'], r['deadline'], c['time'], r['flags']])
            self.execute(c, r)
        c['time'] = target

    def execute(self, c, r):
        if not (r['flags'] & 0x10000):
            wi = r['recv']
            self.log.append(['call', wi, r['idx'], r['deadline'], c['time'], r['flags'], r['serial'], r['value']])
            w = self.w[wi]
            if r is w['release']:
                self.log.append(['destroy', wi])
                self.stop(wi)
                r['flags'] |= 0x10000
                w['release'] = None
            elif r is w['timer'] and not (r['flags'] & 1):
                self.stop(wi)
            script = self.case['scripts'].get(wi, [])
            k = self.calls[wi]
            self.calls[wi] += 1
            for op in (script[k] if k < len(script) else []):
                self.op(op)
            if (r['flags'] & 1) and not (r['flags'] & 0x10000):
                r['deadline'] = self.S.add(c['time'], r['period'])
                r['flags'] = (r['flags'] & ~0x10000) | 0x20000
                self.log.append(['rearm', r['idx'], r['deadline'], r['flags']])
                c['heap'].append(r)
                return
        c['free'].insert(0, r['block'])
        c['live'] -= 1

    def advance(self, c, inc):
        self.log.append(['advance', c['ci'], c['time'], c['epoch']])
        if c['flags'] & 1:
            return
        t = self.S.add(c['time'], inc)
        if f32(c['span']) > f32(t):
            c['time'] = t
            self.drain(c)
            return
        rem = self.S.sub(t, c['span'])
        if f32(EPS) > f32(self.S.sub(rem, 0) & 0x7fffffff):
            rem = 0
        c['time'] = c['span']
        self.drain(c)
        self.log.append(['rebase', c['ci'], c['time'], c['epoch'], len(c['heap'])])
        for r in c['heap']:
            r['deadline'] = self.S.sub(r['deadline'], c['span'])
        c['epoch'] += 1
        c['time'] = rem
        self.drain(c)

    def state(self):
        out = []
        for c in self.clocks:
            pending = sorted([[r['idx'], r['deadline'], r['flags']] for r in c['heap']])
            out.append({'time': c['time'], 'epoch': c['epoch'], 'serial': c['serial'], 'count': len(c['heap']) + 1,
                        'live': c['live'], 'pending': pending, 'free_head': c['free'][:8]})
        wr = [[w['timer']['idx'] if w['timer'] else None, w['release']['idx'] if w['release'] else None,
               0x10000 if self.wspec[i].get('repeat') else 0] for i, w in enumerate(self.w)]
        return {'clocks': out, 'wrappers': wr}

    def run(self):
        for op in self.case['ops']:
            self.op(op)
        return {'log': self.log, 'state': self.state()}


# ----------------------------------------------------------------------------- cases
T0 = bits(10.0)


def clock(time=T0, **kw):
    return dict(time=time, **kw)


def mk(name, group, wrappers, ops, scripts=None, wrapper_spec=None, clocks=None, model=True):
    return dict(name=name, group=group, wrappers=wrappers, ops=ops, scripts=scripts or {},
                wrapper_spec=wrapper_spec or [{} for _ in range(wrappers)], clock=clocks or [clock(), clock()], model=model)


def named_cases():
    cs = []
    d = lambda x: bits(x)
    # ORDER-05.1: ordering, ties, cancellation, reuse, release idempotence, clock selection
    cs.append(mk('equal-deadlines-serial-order', '05.1', 4,
                 [('sched', 0, 2, d(0.5), 2), ('sched', 0, 0, d(0.5), 0), ('sched', 0, 3, d(0.5), 3), ('sched', 0, 1, d(0.5), 1),
                  ('settime', 0, d(11)), ('drain', 0)]))
    cs.append(mk('different-deadlines', '05.1', 4,
                 [('sched', 0, 0, d(0.75), 0), ('sched', 0, 1, d(0.25), 1), ('sched', 0, 2, d(0.5), 2), ('sched', 0, 3, d(0.25), 3),
                  ('settime', 0, d(10.5)), ('drain', 0), ('settime', 0, d(11)), ('drain', 0)]))
    cs.append(mk('delay-clamped-to-minimum', '05.1', 3,
                 [('sched', 0, 0, 0, 0), ('sched', 0, 1, bits(-1.0), 1), ('sched', 0, 2, 0x38d1b716, 2), ('settime', 0, d(10.001)), ('drain', 0)]))
    cs.append(mk('due-boundary-equality', '05.1', 2,
                 [('sched', 0, 0, d(0.125), 0), ('sched', 0, 1, d(0.25), 1), ('settime', 0, bits(10.125)), ('drain', 0)]))
    cs.append(mk('cancel-pending-timer', '05.1', 3,
                 [('start', 0, d(0.5), 7), ('start', 1, d(0.5), 8), ('start', 2, d(0.25), 9), ('stop', 1), ('settime', 0, d(11)), ('drain', 0)]))
    cs.append(mk('restart-cancels-previous', '05.1', 2,
                 [('start', 0, d(0.5), 1), ('start', 0, d(0.75), 2), ('start', 1, d(0.6), 3), ('settime', 0, d(11)), ('drain', 0)]))
    cs.append(mk('release-idempotent-and-minimum-delay', '05.1', 3,
                 [('release', 1), ('release', 0), ('release', 1), ('release', 2), ('settime', 0, d(10.001)), ('drain', 0)]))
    cs.append(mk('negative-identity-uses-presentation-clock', '05.1', 3,
                 [('release', 0), ('release', 1), ('start', 2, d(0.25), 5), ('settime', 0, d(11)), ('drain', 0), ('settime', 1, d(11)), ('drain', 1)],
                 wrapper_spec=[{}, {'slot': 0x80000005}, {'slot': 0x80000006}]))
    cs.append(mk('free-list-lifo-reuse', '05.1', 4,
                 [('sched', 0, 0, d(0.1), 0), ('sched', 0, 1, d(0.2), 1), ('sched', 0, 2, d(0.3), 2), ('settime', 0, d(10.25)), ('drain', 0),
                  ('sched', 0, 3, d(0.1), 3), ('sched', 0, 3, d(0.1), 4), ('sched', 0, 3, d(0.1), 5), ('settime', 0, d(11)), ('drain', 0)]))
    cs.append(mk('serial-wrap-tie', '05.1', 3,
                 [('sched', 0, 0, d(0.5), 0), ('sched', 0, 1, d(0.5), 1), ('sched', 0, 2, d(0.5), 2), ('settime', 0, d(11)), ('drain', 0)],
                 clocks=[clock(serial=0xfffffffe), clock()]))
    cs.append(mk('callback-sees-own-deadline-clock-restored', '05.1', 2,
                 [('sched', 0, 0, d(0.25), 0), ('sched', 0, 1, d(0.5), 1), ('settime', 0, d(12)), ('drain', 0)]))
    cs.append(mk('callback-schedules-due-and-later', '05.1', 4,
                 [('sched', 0, 0, d(0.25), 0), ('sched', 0, 1, d(0.5), 1), ('settime', 0, d(10.6)), ('drain', 0), ('settime', 0, d(11)), ('drain', 0)],
                 scripts={0: [[('sched', 0, 2, d(0.1), 20), ('sched', 0, 3, d(0.5), 30)]]}))
    # ORDER-05.2: repeating requests and callbacks that schedule/cancel others
    cs.append(mk('repeating-catch-up', '05.2', 1, [('start', 0, d(0.125), 0), ('settime', 0, d(10.5)), ('drain', 0)],
                 wrapper_spec=[{'repeat': True}]))
    cs.append(mk('repeating-zero-period-clamped', '05.2', 1, [('start', 0, 0, 0), ('settime', 0, bits(10.002)), ('drain', 0)],
                 wrapper_spec=[{'repeat': True}]))
    cs.append(mk('repeating-callback-cancels-other', '05.2', 3,
                 [('start', 0, d(0.125), 0), ('start', 1, d(0.3), 1), ('start', 2, d(0.2), 2), ('settime', 0, d(10.5)), ('drain', 0)],
                 scripts={0: [[], [('stop', 1)], []]}, wrapper_spec=[{'repeat': True}, {'repeat': True}, {}]))
    cs.append(mk('repeating-callback-schedules-other', '05.2', 3,
                 [('start', 0, d(0.125), 0), ('settime', 0, d(10.5)), ('drain', 0)],
                 scripts={0: [[('start', 1, d(0.0625), 11)], [], [('release', 2)]]}, wrapper_spec=[{'repeat': True}, {'repeat': True}, {}]))
    cs.append(mk('repeating-stops-itself', '05.2', 2,
                 [('start', 0, d(0.125), 0), ('start', 1, d(0.125), 1), ('settime', 0, d(10.5)), ('drain', 0)],
                 scripts={0: [[], [('stop', 0)]]}, wrapper_spec=[{'repeat': True}, {'repeat': True}]))
    cs.append(mk('repeating-restarts-itself', '05.2', 1,
                 [('start', 0, d(0.125), 0), ('settime', 0, d(10.5)), ('drain', 0)],
                 scripts={0: [[('start', 0, d(0.2), 5)]]}, wrapper_spec=[{'repeat': True}]))
    cs.append(mk('repeating-equal-period-tie-rotation', '05.2', 2,
                 [('start', 0, d(0.125), 0), ('start', 1, d(0.125), 1), ('settime', 0, d(10.5)), ('drain', 0)],
                 wrapper_spec=[{'repeat': True}, {'repeat': True}]))
    # Mirrors of the live ORDER-05 probe phases 3/4 (range listeners = repeating wrappers, DestroyTrigger = release).
    cs.append(mk('live-mirror-callback-releases-tied-peer-and-starts-new', '05.2', 3,
                 [('start', 0, d(0.125), 0), ('start', 1, d(0.125), 1)] + [('advance', 0, bits(0.005))] * 60,
                 scripts={0: [[('release', 1), ('start', 2, d(0.125), 2)]]},
                 wrapper_spec=[{'repeat': True}, {'repeat': True}, {'repeat': True}]))
    cs.append(mk('live-mirror-callback-releases-itself', '05.2', 2,
                 [('start', 0, d(0.125), 0), ('start', 1, d(0.125), 1)] + [('advance', 0, bits(0.005))] * 60,
                 scripts={1: [[('release', 1)]]}, wrapper_spec=[{'repeat': True}, {'repeat': True}]))
    cs.append(mk('advance-five-ms-steps', '05.2', 2,
                 [('start', 0, d(0.125), 0), ('release', 1)] + [('advance', 0, bits(0.005))] * 60,
                 wrapper_spec=[{'repeat': True}, {}]))
    # ORDER-05.3: epoch wrap and pause (original-only rows are compared against analytic expectations in the report)
    cs.append(mk('wrap-rebases-pending-deadlines', '05.3', 3,
                 [('start', 0, d(0.125), 0), ('sched', 0, 1, d(0.3), 1), ('sched', 0, 2, d(1.0), 2)] + [('advance', 0, bits(0.005))] * 80,
                 wrapper_spec=[{'repeat': True}, {}, {}], clocks=[clock(time=bits(299.875)), clock()]))
    cs.append(mk('paused-clock-does-not-drain', '05.3', 2,
                 [('start', 0, d(0.125), 0), ('release', 1)] + [('advance', 0, bits(0.005))] * 40,
                 wrapper_spec=[{'repeat': True}, {}], clocks=[clock(flags=1), clock()]))
    cs.append(mk('presentation-clock-independent', '05.3', 2,
                 [('start', 0, d(0.125), 0), ('start', 1, d(0.125), 1)] + [('advance', 1, bits(0.04))] * 10 + [('advance', 0, bits(0.005))] * 10,
                 wrapper_spec=[{'repeat': True, 'slot': 0x80000001}, {'repeat': True}]))
    cs.append(mk('wrap-callback-schedules-during-span-drain', '05.3', 4,
                 [('start', 0, d(0.125), 0)] + [('advance', 0, bits(0.005))] * 30,
                 scripts={0: [[('sched', 0, 1, d(0.05), 10), ('sched', 0, 2, 0, 20), ('start', 3, d(0.125), 30)]]},
                 wrapper_spec=[{'repeat': True}, {}, {}, {'repeat': True}], clocks=[clock(time=bits(299.875)), clock()]))
    cs.append(mk('wrap-large-increment-catch-up', '05.3', 3,
                 [('start', 0, d(0.125), 0), ('sched', 0, 1, d(0.2), 1), ('sched', 0, 2, d(0.6), 2), ('advance', 0, bits(0.5)), ('advance', 0, bits(0.5))],
                 wrapper_spec=[{'repeat': True}, {}, {}], clocks=[clock(time=bits(299.875)), clock()]))
    cs.append(mk('wrap-exact-span-remainder-zero', '05.3', 2,
                 [('sched', 0, 0, d(0.25), 0), ('sched', 0, 1, d(0.0001), 1), ('advance', 0, bits(0.25))],
                 clocks=[clock(time=bits(299.75)), clock()]))
    cs.append(mk('wrap-other-clock-not-rebased', '05.3', 2,
                 [('start', 0, d(0.5), 0), ('start', 1, d(0.5), 1), ('advance', 0, bits(0.25)), ('advance', 1, bits(0.25))],
                 wrapper_spec=[{}, {'slot': 0x80000002}], clocks=[clock(time=bits(299.875)), clock(time=bits(10.0))]))
    cs.append(mk('clock-restored-backwards-pending-absolute', '05.3', 3,
                 [('start', 0, d(0.25), 0), ('sched', 0, 1, d(0.5), 1), ('release', 2), ('settime', 0, bits(5.0))]
                 + [('advance', 0, bits(0.5))] * 12, wrapper_spec=[{'repeat': True}, {}, {}]))
    cs.append(mk('clock-restored-forward-catch-up', '05.3', 2,
                 [('start', 0, d(0.25), 0), ('sched', 0, 1, d(0.5), 1), ('settime', 0, bits(11.0)), ('advance', 0, bits(0.005))],
                 wrapper_spec=[{'repeat': True}, {}]))
    cs.append(mk('identity-sign-switch-moves-new-requests', '05.3', 2,
                 [('start', 0, d(0.25), 0), ('setslot', 0, 0x80000001), ('start', 0, d(0.25), 1), ('release', 0), ('advance', 0, bits(0.3)),
                  ('advance', 1, bits(0.3))], wrapper_spec=[{'repeat': True}, {}]))
    return cs


def random_cases(seed, count):
    rng = random.Random(seed)
    words = [bits(x) for x in (0.0, 0.0001, 0.005, 0.1, 0.125, 0.25, 0.5, 1.0, 2.0)]
    out = []
    for n in range(count):
        nw = 6
        spec_w = [{'repeat': rng.random() < 0.5, 'slot': (0x80000000 | (i + 1)) if rng.random() < 0.15 else i + 1} for i in range(nw)]

        def ops_for():
            ops = []
            for _ in range(rng.randint(0, 3)):
                k = rng.random()
                wi = rng.randrange(nw)
                if k < 0.35:
                    ops.append(('sched', rng.randrange(2), wi, rng.choice(words), rng.randrange(100)))
                elif k < 0.6:
                    ops.append(('start', wi, rng.choice(words[2:]), rng.randrange(100)))   # periods >= 5 ms (instruction budget)
                elif k < 0.85:
                    ops.append(('stop', wi))
                else:
                    ops.append(('release', wi))
            return ops
        scripts = {w: [ops_for() for _ in range(rng.randint(0, 4))] for w in range(nw)}
        ops = ops_for() + ops_for()
        t = T0
        for _ in range(rng.randint(1, 4)):
            ci = rng.randrange(2)
            t2 = bits(f32(t) + rng.choice((0.05, 0.125, 0.3, 0.6)))
            ops += [('settime', ci, t2), ('drain', ci)]
            ops += ops_for()
            t = t2
        out.append(mk('random-%d-%d' % (seed, n), 'random', nw, ops, scripts=scripts, wrapper_spec=spec_w,
                      clocks=[clock(serial=rng.choice((0, 0xfffffff0))), clock()]))
    return out


def jsonable(x):
    return o31.jsonable(x)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--random', type=int, default=400)
    ap.add_argument('--seed', type=int, default=5051)
    ap.add_argument('--expected', type=Path)
    args = ap.parse_args()
    binary = args.binary.read_bytes()
    if hashlib.sha256(binary).hexdigest() != SHA:
        ap.error('requires game.dll 1.27.1.7085')
    scalar = Scalar(binary)
    cases = named_cases() + random_cases(args.seed, args.random)
    results, mismatches = [], []
    stats = {'callbacks': 0, 'silent_pops': 0, 'rearms': 0, 'queued': 0, 'model_compared': 0, 'fault': 0}
    for c in cases:
        try:
            o = jsonable(Original(binary, c).run())
        except Exception as error:
            o = {'fault': str(error)}
            stats['fault'] += 1
        if 'fault' not in o:
            log = o['log']
            stats['callbacks'] += sum(1 for e in log if e[0] == 'call')
            stats['silent_pops'] += sum(1 for e in log if e[0] == 'pop' and e[4] & 0x10000)
            stats['queued'] += sum(1 for e in log if e[0] == 'queue')
            stats['rearms'] += sum(1 for e in log if e[0] == 'rearm')
            stats['rebases'] = stats.get('rebases', 0) + sum(1 for e in log if e[0] == 'rebase')
        if c['model']:
            md = jsonable(Model(c, scalar).run())
            stats['model_compared'] += 1
            if 'fault' in o or o != md:
                mismatches.append({'case': c['name'], 'original': o, 'model': md})
        if c['group'] != 'random':
            results.append({'name': c['name'], 'group': c['group'], 'model_compared': c['model'], 'case': jsonable(c), 'original': o})
    report = {'binary_sha256': SHA, 'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'harness_sha256': hashlib.sha256(Path(spec.origin).read_bytes()).hexdigest(),
              'cases': len(cases), 'named': len(results), 'random': args.random, 'seed': args.seed,
              'mismatches': len(mismatches), 'stats': stats, 'named_results': results, 'mismatch_detail': mismatches[:4]}
    args.report.write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps({k: report[k] for k in ('cases', 'named', 'random', 'mismatches', 'stats')}))
    if args.expected:
        exp = json.loads(args.expected.read_text())     # expected-ORDER-05.x.json: that ID's named group
        frozen = {r['name']: r['original'] for r in exp['oracle_named_results']}
        if frozen != {r['name']: r['original'] for r in results if r['name'] in frozen}:
            print('named results differ from frozen expected', file=sys.stderr)
            sys.exit(2)
    sys.exit(1 if mismatches else 0)


if __name__ == '__main__':
    main()
