#!/usr/bin/env python3
"""ROUTE-01.2 research oracle: empty/partial route buffers, invalid starts and
the route-table growth boundary, observed through the public advance 165ae0.

Every call is unchanged retail code (165ae0 -> 165b60/166c30/162cb0 and
165c60/167ce0/166e90/148100/147dc0 ...). Route storage is the CLrPath
constructor's growth-0x80 tables backed by host Storm storage. Each scenario
records the public return code and the complete next state (both buffers,
capacities, indices, flags, timestamps, retry/delay, output), then the state
after the following public advance. Supplied preconditions are listed in
route01_1_2_harness.py; index values that no retail writer produces are
labelled 'unreachable-by-writers' and kept only as consumer evidence.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from route01_1_2_harness import Retail, FINE_BUCKET, ACC_BUCKET, fw, wf, f32, model_fine  # noqa: E402

RADII = [.25, .75, 1.25, 1.75]


def snapshot(r, output):
    fine, acc = r.table(0x34), r.table(0x54)
    return dict(fine_count=fine['count'], fine_capacity=fine['capacity'], fine_index=r.read(r.path + 0x74)[0],
                fine_words=r.words(0x34), coarse_count=acc['count'], coarse_capacity=acc['capacity'],
                coarse_index=r.read(r.path + 0x78)[0], coarse_words=r.words(0x54),
                flags=r.read(r.path + 0x88)[0], fine_time=r.read(r.path + 0x7c)[0], coarse_time=r.read(r.path + 0x80)[0],
                delay=r.read(r.path + 0x94)[0], retry=r.read(r.path + 0x98)[0],
                destination=r.read(r.path + 0x1c, 2), adjusted=r.read(r.path + 0x24, 2), output=r.read(output, 2),
                fine_bucket=r.read(FINE_BUCKET + 8, 3), coarse_bucket=r.read(ACC_BUCKET + 8, 3))


def prepare(r, cls, goal, enabled, lane=0):
    r.construct_path()
    r.write(r.path + 0x88, (lane << 30) | (0x200000 if enabled else 0))
    r.uc.mem_write(r.path + 0x1c, struct.pack('<4f', *goal, *goal))
    r.write(r.path + 0x9c, 0x02000000)
    r.write(r.path + 0xa8, -1, -1)
    r.uc.mem_write(r.path + 0xb4, struct.pack('<f', RADII[cls]))
    r.set_buckets()


def advance(r, source, goal, tick):
    """One public 165ae0(ECX path, source*, destination/output*, mover) at owner counter tick."""
    r.write(r.owner + 0x538, tick)
    out = r.points + 0x80
    r.uc.mem_write(r.points + 0x70, struct.pack('<2f', *source))
    r.uc.mem_write(out, struct.pack('<2f', *goal))
    try:
        result, popped = r.run(0x6f165ae0, r.path, r.points + 0x70, out, r.mover, budget=50000000)
        assert popped == 12, popped
        return dict(result=result, state=snapshot(r, out))
    except Exception as error:  # preserved as an observation, never retried
        return dict(fault=str(error), eip=r.uc.reg_read(r.R.UC_X86_REG_EIP), state=snapshot(r, out))


def build(r, name):
    r.clear_map()
    if name == 'wall_gap':
        r.block_fine([(40, y) for y in range(8, 58) if not 30 <= y <= 33])
    r.rebuild()


class FineComparison:
    """Read-only original build observers compared with the production C kernel.

    Inputs contain the original current terrain and the original effective
    query class/mask (setup0 and setup1 retain the preceding query profile).
    Scheduler and table owners still execute only in the unchanged oracle.
    """
    class Input(ctypes.Structure):
        _fields_ = [('cells', ctypes.POINTER(ctypes.c_uint8)),
                    ('objects', ctypes.POINTER(ctypes.c_uint32))]

    def __init__(self, retail, engine):
        self.r, self.engine = retail, engine
        self.calls, self.differences, self.frame = 0, [], None
        engine.pathing_fine_result_reset()
        engine.pathing_fine_result_words.argtypes = [ctypes.POINTER(ctypes.c_uint32),
            ctypes.POINTER(self.Input), ctypes.POINTER(ctypes.c_uint32)]
        retail.hook(0x6f148100, self.enter)
        for ret in (0x6f148155, 0x6f14817d, 0x6f14819f, 0x6f1481dd):
            retail.hook(ret, self.leave)

    def enter(self, uc, address, size, user):
        r = self.r
        sp = uc.reg_read(r.R.UC_X86_REG_ESP)
        _, route, source, goal, mask, budget, radius, target = r.read(sp, 8)
        assert target == 0, 'this comparison requires the supplied empty object registry'
        source_words, goal_words = r.read(source, 2), r.read(goal, 2)
        a, b = [math.floor(wf(v)) for v in source_words], [math.floor(wf(v)) for v in goal_words]
        initialized = 0 <= a[0] < r.fine_side and 0 <= a[1] < r.fine_side and a != b
        cls = min(3, max(0, int(wf(r.read(radius)[0]) * 2))) if initialized else r.read(r.fine_system + 0xa0)[0] & 65535
        effective_mask = r.read(mask)[0] if initialized else r.read(r.fine_system + 0xa4)[0]
        cells = (ctypes.c_uint8 * (r.fine_side ** 2))(*bytes(uc.mem_read(r.cells, r.fine_side ** 2 * 4))[3::4])
        q = (ctypes.c_uint32 * 16)(r.fine_side, r.fine_side, *(v & 0xffffffff for v in (*a, *b)),
            budget, cls, effective_mask, 0, 0, 0xffffffff, *source_words, *goal_words)
        output = (ctypes.c_uint32 * (7 + 2 * 32768))()
        self.engine.pathing_fine_result_words(q, ctypes.byref(self.Input(cells, None)), output)
        count = output[3]
        self.frame = (route, list(output[:7]), list(output[7:7 + 2 * count]), list(q))

    def leave(self, uc, address, size, user):
        r = self.r
        route, actual, words, inputs = self.frame
        count = r.read(route + 0x1c)[0]
        table = r.read(route + 0xc)[0]
        expected = [uc.reg_read(r.R.UC_X86_REG_EAX), r.read(r.fine_system + 0x6c)[0],
            r.read(r.fine_system + 0x40)[0], count]
        obstruction = r.read(r.fine_system + 0xd0)[0]
        self.calls += 1
        if actual[:4] != expected or actual[5] != obstruction or words != r.read(table, count * 2):
            self.differences.append(dict(call=self.calls, input=inputs, expected=expected,
                actual=actual, obstruction=obstruction, words_equal=words == r.read(table, count * 2)))
        self.frame = None


class CoarseComparison:
    """Compare every reached native adaptive build with retained production work."""
    def __init__(self, retail, engine):
        self.r, self.engine = retail, engine
        self.calls, self.differences, self.frame = 0, [], None
        engine.pathing_adaptive_retained_reset()
        engine.pathing_adaptive_retained_route.argtypes = [ctypes.POINTER(ctypes.c_uint32),
            ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_uint32)]
        retail.hook(0x6f162cb0, self.enter)
        for ret in (0x6f162d05, 0x6f162d2d, 0x6f162d4f, 0x6f162d90):
            retail.hook(ret, self.leave)

    def enter(self, uc, address, size, user):
        r = self.r
        sp = uc.reg_read(r.R.UC_X86_REG_ESP)
        _, lane, route, source, goal, budget, size_input, warp = r.read(sp, 8)
        assert lane % 2 == 0 and lane <= 6
        classes = [(r.read(storage + 8 * i + 4)[0] >> (30 - lane)) & 3
                   for side, storage in zip(r.sides, r.data) for i in range(side * side)]
        q = (ctypes.c_uint32 * 8)(r.sides[0], r.sides[0], size_input, budget,
            *r.read(source, 2), *r.read(goal, 2))
        output = (ctypes.c_uint32 * (4 + 2 * 131072))()
        self.engine.pathing_adaptive_retained_route(q, (ctypes.c_uint8 * len(classes))(*classes), output)
        count = output[3]
        self.frame = (route, list(output[:4]), list(output[4:4 + count * 2]), list(q))

    def leave(self, uc, address, size, user):
        r = self.r
        route, actual, words, inputs = self.frame
        count = r.read(route + 0x1c)[0]
        table = r.read(route + 0xc)[0]
        expected = [uc.reg_read(r.R.UC_X86_REG_EAX), r.read(r.system + 0x9c)[0],
            r.read(r.system + 0x6c)[0], count]
        self.calls += 1
        if actual != expected or words != r.read(table, count * 2):
            self.differences.append(dict(call=self.calls, input=inputs, expected=expected,
                actual=actual, words_equal=words == r.read(table, count * 2)))
        self.frame = None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--expected', type=Path)
    parser.add_argument('--engine', type=Path, help='production C probe; compare every reached fine build')
    parser.add_argument('--initialized-consumer', action='store_true',
                        help='run original0040d0 coarse-distance initializer before public advances')
    args = parser.parse_args()
    r = Retail(args.binary, 64)
    if args.initialized_consumer:
        r.run(0x6f0040d0, 0)
        assert r.read(0x6fd54194)[0] == fw(10), 'original consumer threshold initialization failed'
    engine = ctypes.CDLL(str(args.engine.resolve())) if args.engine else None
    comparisons = [FineComparison(r, engine)] if engine else []
    coarse_comparisons = [CoarseComparison(r, engine)] if engine else []
    rows = []
    source, goal = (f32(30.125), f32(33.875)), (f32(51.25), f32(42.75))

    def record(**kw):
        rows.append(kw)
        return kw

    for name, cls in itertools.product(['open', 'wall_gap'], range(4)):
        build(r, name)
        # S1/S2 empty buffers, both admissions available.
        for enabled in (False, True):
            prepare(r, cls, goal, enabled)
            before = snapshot(r, r.points + 0x80)
            first = advance(r, source, goal, 100)
            second = advance(r, source, goal, 110)
            record(scenario='empty_admitted', map=name, cls=cls, adaptive_enabled=enabled, before=before, first=first, second=second)
        # S3 empty, coarse denied (bucket work above limit) -> retained empty buffers.
        prepare(r, cls, goal, True)
        r.write(ACC_BUCKET + 8, 901)
        first = advance(r, source, goal, 100)
        r.write(ACC_BUCKET + 8, 0)  # next owner work window; queued request stays FIFO head
        second = advance(r, source, goal, 110)
        record(scenario='empty_coarse_denied', map=name, cls=cls, adaptive_enabled=True, first=first, second=second)
        # S4 empty, fine denied.
        for enabled in (False, True):
            prepare(r, cls, goal, enabled)
            r.write(FINE_BUCKET + 8, 1101)
            first = advance(r, source, goal, 100)
            r.write(FINE_BUCKET + 8, 0)  # next owner work window; queued request stays FIFO head
            second = advance(r, source, goal, 110)
            record(scenario='empty_fine_denied', map=name, cls=cls, adaptive_enabled=enabled, first=first, second=second)
        # S5 interval denial: fine request time equal to the owner counter.
        prepare(r, cls, goal, False)
        r.write(r.path + 0x7c, 100)
        first = advance(r, source, goal, 100)
        second = advance(r, source, goal, 140)
        record(scenario='empty_fine_interval', map=name, cls=cls, adaptive_enabled=False, first=first, second=second)
        # S6 partial buffer with supplied fine indices (consumer evidence), denied then admitted.
        prepare(r, cls, goal, False)
        base = advance(r, source, goal, 100)
        count = base['state']['fine_count']
        for label, index in [('last_valid', count - 1), ('zero', 0), ('equal_count', count), ('beyond', count + 7),
                             ('int_max', 0x7fffffff), ('negative_min', 0x80000000), ('minus_one', 0xffffffff)]:
            for admitted in (False, True):
                prepare(r, cls, goal, False)
                advance(r, source, goal, 100)
                r.write(r.path + 0x74, index)
                if not admitted:
                    r.write(FINE_BUCKET + 8, 1101)
                else:
                    r.set_buckets()
                out = advance(r, source, goal, 120)
                reach = 'reachable' if label in ('last_valid', 'zero', 'minus_one') else 'unreachable-by-writers'
                record(scenario='fine_index_state', map=name, cls=cls, label=label, index=index, admitted=admitted,
                       reachability=reach, base_count=count, after=out)
        # S6b coarse index states with adaptive enabled.
        prepare(r, cls, goal, True)
        base = advance(r, source, goal, 100)
        ccount = base['state']['coarse_count']
        for label, index in [('last_valid', ccount - 1), ('equal_count', ccount), ('minus_one', 0xffffffff)]:
            for admitted in (False, True):
                prepare(r, cls, goal, True)
                advance(r, source, goal, 100)
                r.write(r.path + 0x78, index)
                if not admitted:
                    r.write(ACC_BUCKET + 8, 901)
                else:
                    r.set_buckets()
                out = advance(r, source, goal, 120)
                record(scenario='coarse_index_state', map=name, cls=cls, label=label, index=index, admitted=admitted,
                       reachability='reachable' if label != 'equal_count' else 'unreachable-by-writers',
                       base_count=ccount, after=out)
        # S7 invalid starts.
        starts = [('blocked_fine_cell', (f32(40.5), f32(20.5))), ('outside_negative_x', (f32(-3.5), f32(10.25))),
                  ('outside_negative_y', (f32(10.25), f32(-.5))), ('outside_beyond_x', (f32(70.25), f32(10.25)))]
        for (label, start), enabled in itertools.product(starts, (False, True)):
            if label == 'blocked_fine_cell' and name == 'open':
                continue
            prepare(r, cls, goal, enabled)
            prior = advance(r, source, goal, 100)  # retained search state = previous ordinary request
            r.construct_path()
            prepare(r, cls, goal, enabled)
            first = advance(r, start, goal, 200)
            second = advance(r, start, goal, 210) if 'fault' not in first else None
            record(scenario='invalid_start', map=name, cls=cls, label=label, start=[fw(v) for v in start],
                   adaptive_enabled=enabled, prior_fine_count=prior['state']['fine_count'],
                   fine_source_node=r.read(r.fine_system + 0x90)[0], coarse_source_node=r.read(r.system + 0xc4)[0],
                   first=first, second=second)
    # S7b out-of-map fine setup returns 0 but 148100 still searches from the stale
    # +90 source node: the same request after two different priors (direct 166e90).
    build(r, 'open')
    stale = []
    for prior_goal in [(f32(51.25), f32(42.75)), (f32(12.25), f32(50.75))]:
        prepare(r, 0, goal, False)
        r.uc.mem_write(r.points + 0x40, struct.pack('<4f', *source, *prior_goal))
        r.write(r.owner + 0x538, 100)
        r.run(0x6f166e90, r.path, r.points + 0x40, r.points + 0x48)
        prior_nodes = r.read(r.fine_system + 0x40)[0]
        prepare(r, 0, goal, False)
        r.uc.mem_write(r.points + 0x40, struct.pack('<4f', f32(-3.5), f32(10.25), *goal))
        r.write(r.owner + 0x538, 200)
        result, _ = r.run(0x6f166e90, r.path, r.points + 0x40, r.points + 0x48)
        stale.append(dict(prior_goal=[fw(v) for v in prior_goal], prior_nodes=prior_nodes, result=result,
                          nodes_after=r.read(r.fine_system + 0x40)[0], pops=r.read(r.fine_system + 0x6c)[0],
                          source_node=r.read(r.fine_system + 0x90)[0], nearest_node=r.read(r.fine_system + 0x9c)[0],
                          flags=r.read(r.path + 0x88)[0], index=r.read(r.path + 0x74)[0], words=r.words(0x34)))
    # S8 growth boundary on a 256-cell map: straight corridor longer than 128 points.
    g = Retail(args.binary, 256)
    if args.initialized_consumer:
        g.run(0x6f0040d0, 0)
        assert g.read(0x6fd54194)[0] == fw(10)
    if engine:
        comparisons.append(FineComparison(g, engine))
        coarse_comparisons.append(CoarseComparison(g, engine))
    g.clear_map()
    g.block_fine([(x, y) for x in range(256) for y in (100, 104)], masks=(2,))
    g.rebuild()
    growth = []
    captured = []

    def on_fine(uc, address, size, user):
        sp = g.read(g.uc.reg_read(g.R.UC_X86_REG_ESP), 5)
        captured.append(dict(source=g.read(sp[3], 2), goal=g.read(sp[4], 2), chain=g.fine_chain(sp[1])))
    g.hook(0x6f147dc0, on_fine)
    for cls, length in itertools.product((0, 1), (126, 127, 128, 129, 180, 250)):
        g.construct_path()
        g.write(g.path + 0x88, 0)
        g.write(g.path + 0x9c, 0x02000000)
        g.uc.mem_write(g.path + 0xb4, struct.pack('<f', RADII[cls]))
        g.set_buckets(fine_limit=0xffffffff)
        s = (f32(2.25), f32(102.75))
        t = (f32(2 + length + .25), f32(102.75))
        g.uc.mem_write(g.path + 0x1c, struct.pack('<4f', *t, *t))
        log0 = len(g.storm_log)
        captured.clear()
        g.uc.mem_write(g.points + 0x40, struct.pack('<4f', *s, *t))
        g.write(g.owner + 0x538, 100)
        result, _ = g.run(0x6f166e90, g.path, g.points + 0x40, g.points + 0x48)
        table = g.table(0x34)
        words = g.words(0x34)
        assert captured and model_fine(captured[0]['chain'], [wf(v) for v in captured[0]['source']],
                                       [wf(v) for v in captured[0]['goal']]) == words
        first = dict(result=result, count=table['count'], capacity=table['capacity'], index=g.read(g.path + 0x74)[0],
                     storm=g.storm_log[log0:], words_sha256=hashlib.sha256(struct.pack('<%dI' % len(words), *words)).hexdigest(),
                     first_words=words[:4], last_words=words[-4:])
        # Shorter follow-up on the same path object: count shrinks, capacity is retained.
        log1 = len(g.storm_log)
        t2 = (f32(12.25), f32(102.75))
        g.uc.mem_write(g.points + 0x40, struct.pack('<4f', *s, *t2))
        g.write(g.owner + 0x538, 200)
        g.set_buckets(fine_limit=0xffffffff)
        result2, _ = g.run(0x6f166e90, g.path, g.points + 0x40, g.points + 0x48)
        table2 = g.table(0x34)
        second = dict(result=result2, count=table2['count'], capacity=table2['capacity'], index=g.read(g.path + 0x74)[0],
                      storm=g.storm_log[log1:], words=g.words(0x34))
        growth.append(dict(cls=cls, length=length, first=first, second=second))
    payload = dict(version=1, binary_sha256=r.digest, task='ROUTE-01.2', rows=rows, growth=growth, stale_outside=stale)
    if args.initialized_consumer:
        payload['consumer_initialization'] = {'entry': '6f0040d0', 'address': '6fd54194', 'word': fw(10)}
    blob = json.dumps(payload, separators=(',', ':'), sort_keys=True).encode()
    if args.expected:
        if args.expected.exists():
            assert json.loads(args.expected.read_text()) == json.loads(blob), 'frozen ROUTE-01.2 state differs'
        else:
            args.expected.write_bytes(blob + b'\n')
    summary = {}
    for row in rows:
        key = row['scenario'] + ':' + str(row.get('label', row.get('adaptive_enabled')))
        summary.setdefault(key, 0)
        summary[key] += 1
    faults = [dict(scenario=r_['scenario'], map=r_['map'], cls=r_['cls'], label=r_.get('label'),
                   enabled=r_.get('adaptive_enabled'), fault=r_['first'].get('fault'), eip=r_['first'].get('eip'))
              for r_ in rows if isinstance(r_.get('first'), dict) and 'fault' in r_['first']]
    report = dict(passed=True, binary_sha256=r.digest, payload_sha256=hashlib.sha256(blob).hexdigest(),
                  rows=len(rows), growth_cases=len(growth), summary=summary, faults=faults,
                  stale_outside_routes_differ=stale[0]['words'] != stale[1]['words'])
    if engine:
        report['engine_fine_builds'] = sum(c.calls for c in comparisons)
        report['engine_coarse_builds'] = sum(c.calls for c in coarse_comparisons)
        report['engine_differences'] = [dict(kind='fine', **d) for c in comparisons for d in c.differences] + [dict(kind='coarse', **d) for c in coarse_comparisons for d in c.differences]
        report['passed'] = not report['engine_differences'] and not faults
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
