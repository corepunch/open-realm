#!/usr/bin/env python3
"""ACC-04.1 research: ordinary adaptive costs, ties and budget exhaustion.

All maps are producer-built (04d870/054000 terrain setters with the ground flag,
full 15d360) and every request is a complete original 162cb0 call (warp off).
Passive hooks record heap pops (6f163f93: key,index,generation) and every
ordinary relaxation (6f164020 entry: child,parent,caller). No state is altered.

Parts
  1  integer distance 1d58e0 (ECX in, EAX out, plain RET) against floor(sqrt) and a
     Newton model, over every (24a)^2+(24b)^2 with 0<=a,b<=520.
  2  per request: retail g(goal node) vs Dijkstra over the relaxations the search
     itself performed (same representatives), vs an independent base-grid optimum
     (8-neighbour, corner-safe, same integer distance per step, 2x2 for size 2).
  3  equal-key pops and equal-cost relaxation ties; transposed-map controls.
  4  budget sweep: partial endpoint = nearest node by discovery-time squared
     distance (strict <), checked against all created nodes.
"""
import argparse
import hashlib
import heapq
import json
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from acc_research_harness import Retail  # noqa: E402
from verify_acc01_2_witnesses import base_fixtures, fine_of  # noqa: E402

W = 32


def isqrt_model(n):
    """Transcription of 6f1d58e0 (Newton with three seed ranges)."""
    if n <= 0xff:
        s = ((n * 0xaaaaaaab) >> 32 >> 3) + 1
    elif n <= 0xffff:
        s = ((n * 0x51eb851f) >> 32 >> 6) + 0x15
    else:
        hi = (n * 0x39acc69d) >> 32
        s = ((((n - hi) & 0xffffffff) >> 1) + hi) >> 0xe
        s += 0x1bc
    while True:
        q = n // s
        d = s - q
        d = int(d / 2) if d >= 0 else -int(-d / 2)   # CDQ/SUB/SAR on signed difference
        s = (q + s) // 2 if q + s >= 0 else -((-(q + s)) // 2)
        if d == 0:
            return s


def dist(a, b):
    return isqrt_model((24 * abs(a[0] - b[0])) ** 2 + (24 * abs(a[1] - b[1])) ** 2)


def base_grid_optimum(clear, start, goal, size):
    def ok(x, y):
        return all((x + dx, y + dy) in clear for dx in range(size) for dy in range(size))
    if not ok(*start) or not ok(*goal):
        return None
    best = {start: 0}
    queue = [(0, start)]
    while queue:
        g, (x, y) = heapq.heappop(queue)
        if (x, y) == goal:
            return g
        if g != best[(x, y)]:
            continue
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if not dx and not dy:
                    continue
                nx, ny = x + dx, y + dy
                if not ok(nx, ny):
                    continue
                if dx and dy and not (ok(x + dx, y) and ok(x, y + dy)):
                    continue
                cost = g + dist((x, y), (nx, ny))
                if cost < best.get((nx, ny), 1 << 62):
                    best[(nx, ny)] = cost
                    heapq.heappush(queue, (cost, (nx, ny)))
    return None


class Tracer:
    def __init__(self, retail):
        from unicorn import UC_HOOK_CODE
        self.r = retail
        self.pops, self.relax = [], []
        R = retail.R

        def pop(uc, address, size, user):
            ebp = uc.reg_read(R.UC_X86_REG_EBP)
            key, index, generation = retail.read(ebp - 0xc, 3)
            self.pops.append((key, index, generation))

        def relax(uc, address, size, user):
            sp = uc.reg_read(R.UC_X86_REG_ESP)
            ret, child, parent = retail.read(sp, 3)
            px, py = retail.read(retail.nodes + 36 * parent, 2)
            cx, cy = retail.read(retail.nodes + 36 * child, 2)
            pg = retail.read(retail.nodes + 36 * parent + 0x14)[0]
            fresh = retail.read(retail.nodes + 36 * child + 0xc)[0] == 0xffffffff
            old = None if fresh else retail.read(retail.nodes + 36 * child + 0x14)[0]
            cost = dist((px, py), (cx, cy))
            self.relax.append((ret - 5, parent, child, pg + cost, old, cost))
        retail.uc.hook_add(UC_HOOK_CODE, pop, begin=0x6f163f93, end=0x6f163f93)
        retail.uc.hook_add(UC_HOOK_CODE, relax, begin=0x6f164020, end=0x6f164020)

    def clear(self):
        self.pops.clear()
        self.relax.clear()


def build(retail, clean, fine_cells, transpose=False):
    retail.uc.mem_write(retail.cells, clean)
    for storage, side in zip(retail.data, retail.sides):
        retail.uc.mem_write(storage, bytes(side * side * 8))
    for fx, fy in fine_cells:
        if transpose:
            fx, fy = fy, fx
        retail.terrain(fx, fy, 2, 1)
    retail.rebuild()


def analyse(retail, tracer, start, goal, size, budget):
    tracer.clear()
    s = retail.search(0, start, goal, budget=budget, size_input=size)
    nodes = s['nodes']
    src, dst = s['source_node'], s['goal_node']
    row = dict(result=s['result'], work=s['work'], nodes=s['node_count'], route_words=[f'{w:08x}' for w in s['route_words']],
               pops=len(tracer.pops), relaxations=len(tracer.relax))
    # equal-key pops (heap ties) among non-stale pops
    keys = [k for k, i, g in tracer.pops]
    row['equal_key_adjacent_pops'] = sum(1 for a, b in zip(keys, keys[1:]) if a == b)
    # equal-cost relaxation ties: a later relaxation offering exactly the stored g (rejected by g>=stored)
    row['equal_cost_rejections'] = sum(1 for _, p, c, cand, old, cost in tracer.relax if old is not None and cand == old)
    if src < 0 or dst < 0 or s['result'] != 1 or s['work'] == 0:
        return s, row
    # Dijkstra over the search's own relaxations (same representatives)
    adj = {}
    for _, p, c, cand, old, cost in tracer.relax:
        adj.setdefault(p, {})
        if c not in adj[p] or cost < adj[p][c]:
            adj[p][c] = cost
    best = {src: 0}
    queue = [(0, src)]
    while queue:
        g, n = heapq.heappop(queue)
        if g != best[n]:
            continue
        for m, cost in adj.get(n, {}).items():
            if g + cost < best.get(m, 1 << 62):
                best[m] = g + cost
                heapq.heappush(queue, (g + cost, m))
    row['retail_goal_g'] = nodes[dst]['g']
    row['explored_graph_optimum'] = best.get(dst)
    # sum of route representative distances (independent of node g)
    chain, n = [], dst
    while n >= 0:
        chain.append(n)
        n = nodes[n]['parent']
    row['route_nodes'] = [[nodes[i]['x'], nodes[i]['y'], nodes[i]['level']] for i in reversed(chain)]
    row['route_levels'] = sorted({nodes[i]['level'] for i in chain})
    return s, row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--fixture', type=Path)
    args = parser.parse_args()
    retail = Retail(args.binary)
    report = dict(binary_sha256=retail.digest)

    # ---- 1: integer distance -------------------------------------------------------------
    mismatches_floor, model_failures, samples = [], 0, 0
    for a in range(521):
        for b in range(a + 1):
            n = (24 * a) ** 2 + (24 * b) ** 2
            got = retail.run(0x6f1d58e0, n)
            samples += 1
            if got != isqrt_model(n):
                model_failures += 1
            if got != math.isqrt(n):
                mismatches_floor.append([a, b, n, got, math.isqrt(n)])
    if model_failures:
        raise SystemExit(f'isqrt model differs in {model_failures} cases')
    report['part1_isqrt'] = dict(samples=samples, model_failures=0, differs_from_floor_sqrt=len(mismatches_floor),
                                 max_abs_difference=max((abs(m[3] - m[4]) for m in mismatches_floor), default=0),
                                 first_differences=mismatches_floor[:24],
                                 neighbour_costs=dict(cardinal=dist((0, 0), (1, 0)), diagonal=dist((0, 0), (1, 1))))

    # ---- 2/3: requests over producer-built maps ---------------------------------------------
    tracer = Tracer(retail)
    clean = bytes(retail.uc.mem_read(retail.cells, 64 * 64 * 4))
    start, goal = (4.25, 4.75), (27.25, 27.75)
    rows = []
    for name, blocked in base_fixtures():
        fine_cells = fine_of(blocked)
        clear = {(x, y) for x in range(W) for y in range(W) if (x, y) not in blocked}
        for transpose in (False, True):
            build(retail, clean, fine_cells, transpose)
            cl = {(y, x) for x, y in clear} if transpose else clear
            st, gl = ((start[1], start[0]), (goal[1], goal[0])) if transpose else (start, goal)
            for size in (0, 1):
                s, row = analyse(retail, tracer, st, gl, size, 100000)
                row.update(map=name, transposed=transpose, size_input=size,
                           base_grid_optimum=base_grid_optimum(cl, (int(st[0]), int(st[1])), (int(gl[0]), int(gl[1])), size + 1))
                rows.append(row)
    # transposition symmetry: compare each map with its transpose
    symmetry = []
    for a, b in zip(rows[0::4] + rows[1::4], rows[2::4] + rows[3::4]):
        same_cost = a.get('retail_goal_g') == b.get('retail_goal_g')
        mirrored = a.get('route_nodes') is not None and b.get('route_nodes') is not None and \
            [[y, x, l] for x, y, l in a['route_nodes']] == b['route_nodes']
        symmetry.append(dict(map=a['map'], size_input=a['size_input'], result=[a['result'], b['result']],
                             goal_g=[a.get('retail_goal_g'), b.get('retail_goal_g')], same_cost=same_cost, mirrored_route=mirrored,
                             work=[a['work'], b['work']]))
    # asymmetric-cost transposition pairs: does the cheaper orientation's route exist in the other graph?
    asym = []
    for a, b in zip(rows[0::4] + rows[1::4], rows[2::4] + rows[3::4]):
        if a['result'] == b['result'] == 1 and a.get('retail_goal_g') != b.get('retail_goal_g'):
            cheap, dear = (a, b) if a['retail_goal_g'] < b['retail_goal_g'] else (b, a)
            flip = (lambda n: [n[1], n[0], n[2]]) if True else None
            cheap_in_dear_frame = [flip(n) for n in cheap['route_nodes']]
            dear_nodes = {tuple(n) for n in dear['route_nodes']}
            asym.append(dict(map=a['map'], size_input=a['size_input'], cheaper='transposed' if cheap['transposed'] else 'original',
                             costs=[cheap['retail_goal_g'], dear['retail_goal_g']], cheap_route=cheap['route_nodes'], dear_route=dear['route_nodes'],
                             cheap_route_nodes_absent_from_dear_route=sum(1 for n in cheap_in_dear_frame if tuple(n) not in dear_nodes)))
    reached = [r for r in rows if r['result'] == 1 and r.get('retail_goal_g') is not None]
    report['part2_costs'] = dict(
        requests=len(rows), reached=len(reached),
        retail_equals_explored_optimum=sum(1 for r in reached if r['retail_goal_g'] == r['explored_graph_optimum']),
        retail_above_explored_optimum=[dict(map=r['map'], t=r['transposed'], size=r['size_input'], retail=r['retail_goal_g'], explored=r['explored_graph_optimum'])
                                       for r in reached if r['retail_goal_g'] != r['explored_graph_optimum']],
        retail_vs_base_grid=dict(
            equal=sum(1 for r in reached if r['base_grid_optimum'] == r['retail_goal_g']),
            above=sum(1 for r in reached if r['base_grid_optimum'] is not None and r['retail_goal_g'] > r['base_grid_optimum']),
            below=sum(1 for r in reached if r['base_grid_optimum'] is not None and r['retail_goal_g'] < r['base_grid_optimum']),
            reference_unreachable=sum(1 for r in reached if r['base_grid_optimum'] is None),
            max_ratio=max((r['retail_goal_g'] / r['base_grid_optimum'] for r in reached if r['base_grid_optimum']), default=None)),
        rows=rows)
    report['part3_ties'] = dict(
        requests_with_equal_key_pops=sum(1 for r in rows if r['equal_key_adjacent_pops']),
        requests_with_equal_cost_rejections=sum(1 for r in rows if r['equal_cost_rejections']),
        asymmetric_cost_pairs=asym,
        transposition=dict(pairs=len(symmetry), same_cost=sum(1 for s in symmetry if s['same_cost']),
                           mirrored_route=sum(1 for s in symmetry if s['mirrored_route']), rows=symmetry))

    # ---- 4: budget sweep ---------------------------------------------------------------------
    sweep = []
    for name in ('solid_wall', 'gap_1', 'random_37', 'horizontal_15'):
        blocked = dict(base_fixtures())[name]
        build(retail, clean, fine_of(blocked))
        for size in (0, 1):
            tracer.clear()
            full = retail.search(0, start, goal, size_input=size)
            for budget in sorted(set(list(range(0, 12)) + [16, 24, 32, 48, 64, full['work'] - 1, full['work'], full['work'] + 1])):
                if budget < 0:
                    continue
                tracer.clear()
                s = retail.search(0, start, goal, budget=budget, size_input=size)
                nodes = s['nodes']
                gx, gy = int(goal[0]), int(goal[1])
                src = s['source_node']
                order = [src] + [c for _, p, c, cand, old, cost in tracer.relax if old is None]
                rank = {}
                for i, n in enumerate(order):
                    rank.setdefault(n, i)
                d2 = {n: (nodes[n]['x'] - gx) ** 2 + (nodes[n]['y'] - gy) ** 2 for n in rank}
                expect = min(rank, key=lambda n: (d2[n], rank[n])) if nodes else None
                nearest_ok = s['result'] == 1 or (s['nearest_node'] == expect if expect is not None else True)
                # only discovered nodes (source or relaxed at least once); the goal node is created at setup
                f_min = min(rank, key=lambda n: (nodes[n]['g'] + nodes[n]['h'], rank[n])) if nodes else None
                h_min = min(rank, key=lambda n: (nodes[n]['h'], rank[n])) if nodes else None
                sweep.append(dict(map=name, size_input=size, budget=budget, result=s['result'], work=s['work'], nodes=s['node_count'],
                                  nearest=s['nearest_node'], nearest_d2=s['nearest_distance2'], source=src,
                                  nearest_matches_min_d2_first_created=nearest_ok, min_f_node=f_min, min_h_node=h_min,
                                  first_route_point=s['route'][0] if s['route'] else None, adjusted_target=s['adjusted_target']))
                if not nearest_ok:
                    raise SystemExit(('nearest selection differs', name, size, budget, s['nearest_node'], expect))
                if s['result'] == 0 and budget < full['work'] and s['work'] != budget + 1:
                    raise SystemExit(('budget work accounting', name, size, budget, s['work']))
    report['part4_budget'] = dict(rows=sweep, cases=len(sweep),
                                  nearest_differs_from_min_f=sum(1 for r in sweep if r['result'] == 0 and r['nearest'] != r['min_f_node']),
                                  nearest_differs_from_min_h=sum(1 for r in sweep if r['result'] == 0 and r['nearest'] != r['min_h_node']))
    report['scope'] = ('Producer-built ground-flag maps (84-map corpus geometry and transposes) on supplied empty 64x64 fine storage and padded '
                       '41/20/10/5 headers; complete 162cb0 requests, warp 0, lane 0; passive pop/relax hooks.')
    text = json.dumps(report, indent=1, sort_keys=True) + '\n'
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text)
    if args.fixture and args.fixture.exists() and json.loads(args.fixture.read_text()) != json.loads(text):
        raise SystemExit('frozen ACC-04.1 expectation differs')
    p2 = report['part2_costs']
    print('isqrt', report['part1_isqrt']['samples'], 'differs-from-floor', report['part1_isqrt']['differs_from_floor_sqrt'],
          '| reached', p2['reached'], 'explored-optimal', p2['retail_equals_explored_optimum'], 'grid', p2['retail_vs_base_grid'],
          '| transposition', report['part3_ties']['transposition']['same_cost'], report['part3_ties']['transposition']['mirrored_route'],
          '| budget', len(sweep))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
