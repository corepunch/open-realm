#!/usr/bin/env python3
"""ACC-01.2 research: producer-built witnesses for adaptive expander branches.

Every map is produced by original code only: fine terrain setter 04d870->054000
(lane-specific flag: ground 02, amphibious 80, floating 40, flight 04), full
hierarchy producer 15d360, Way Gate source bridge 04e360, record producers
04e210 (active) and 04e550 (destination). Requests are complete original 162cb0
calls in the matching lane, stored sizes 1 and 2. Supplied: empty 64x64 fine
storage, padded 41/20/10/5 headers, node/heap/route storage (as in ACC-02.1/03.2).

Passive observation only: Jcc outcomes (block hook), relax call sites with
parent/child node levels, lookup call sites with query level/result, coarse
representative clamps. The frozen expectation stores, per scenario request,
result/work/node count/route words and a node-table digest; plus the first
witness of every observed Jcc outcome and semantic event, per lane.
"""
import argparse
import hashlib
import json
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from acc_research_harness import Retail, load_jccs  # noqa: E402

LANE_FLAG = {0: 2, 2: 0x80, 4: 0x40, 6: 4}
W = 32  # fine-covered base cells per axis in the 64x64 fine map
START, GOAL = (4.25, 4.75), (27.25, 27.75)


def base_fixtures():
    """The 84-map ordinary corpus geometry of verify_wc3_pathing_adaptive.py (base-cell blocked sets)."""
    out = [('open', set()), ('solid_wall', {(16, y) for y in range(W)})]
    out += [(f'gap_{g}', {(16, y) for y in range(W) if not 14 <= y < 14 + g}) for g in range(1, 7)]
    for c in (8, 9, 15, 17, 23, 24):
        out.append((f'vertical_{c}', {(c, y) for y in range(W)}))
        out.append((f'horizontal_{c}', {(x, c) for x in range(W)}))
    rng = random.Random(12717085)
    for index in range(64):
        density = (.08, .16, .24, .32)[index % 4]
        blocked = {(x, y) for y in range(W) for x in range(W) if rng.random() < density}
        for cx, cy in ((4, 4), (27, 27)):
            blocked.difference_update((x, y) for x in range(cx - 2, cx + 3) for y in range(cy - 2, cy + 3))
        out.append((f'random_{index}', blocked))
    return out


def fine_of(base_cells):
    return sorted({(2 * x + dx, 2 * y + dy) for x, y in base_cells for dx in (0, 1) for dy in (0, 1)})


def scenarios(witness_maps=None):
    """Each scenario: name, fine blocked cells, gate ops, requests (source, goal, size, budget, warp)."""
    out = []
    if witness_maps:
        for m in json.loads(Path(witness_maps).read_text())['maps']:
            fine = [tuple(c) for c in m['fine']] if m['fine'] is not None else fine_of({tuple(c) for c in m['blocked_base']})
            gates = [(g[0], tuple(g[1]), tuple(g[2]), g[3]) for g in m['gates']]
            out.append(dict(name=f"found/{m['name']}", fine=fine, gates=gates,
                            requests=[(tuple(m['source']), tuple(m['goal']), size, 100000, warp) for size in m['sizes'] for warp in m['warps']]))
    std_requests = [(START, GOAL, s, b, 0) for s in (0, 1) for b in (100000, 8)]
    for name, blocked in base_fixtures():
        out.append(dict(name=f'std/{name}', fine=fine_of(blocked), gates=[], requests=std_requests))
    # mixed base cells: single fine cells blocked (base class2), deterministic seeds
    for seed in range(16):
        rng = random.Random(70000 + seed)
        density = (.05, .10, .15, .20)[seed % 4]
        cells = {(x, y) for y in range(2 * W) for x in range(2 * W) if rng.random() < density}
        cells -= {(x, y) for x in range(6, 12) for y in range(6, 12)}
        cells -= {(x, y) for x in range(52, 58) for y in range(52, 58)}
        out.append(dict(name=f'fine-random/{seed}', fine=sorted(cells), gates=[], requests=std_requests))
    # low map edges: sources/goals on base row/column 0 and 1
    edge_pairs = [((0.25, 0.75), GOAL), ((0.5, 12.5), (20.5, 0.5)), ((12.5, 0.5), (0.5, 20.5)), ((1.5, 1.5), (0.5, 30.5)),
                  ((30.5, 0.5), (0.5, 0.25)), ((0.5, 30.5), (30.5, 1.5))]
    edge_requests = [(s, g, size, b, 0) for s, g in edge_pairs for size in (0, 1) for b in (100000, 8)]
    out.append(dict(name='edge/open', fine=[], gates=[], requests=edge_requests))
    for seed in range(8):
        rng = random.Random(80000 + seed)
        blocked = {(x, y) for y in range(W) for x in range(W) if rng.random() < .18 and (x < 6 or y < 6)}
        blocked -= {(0, 0), (1, 1), (0, 12), (12, 0), (20, 0), (0, 20), (30, 0), (0, 30), (30, 1), (1, 1)}
        out.append(dict(name=f'edge/random_{seed}', fine=fine_of(blocked), gates=[], requests=edge_requests))
    out.append(dict(name='edge/strip', fine=fine_of({(1, y) for y in range(2, W) if y % 5}), gates=[], requests=edge_requests))
    # low-edge size-2 boundary predicates: only a marked clear cell can make the
    # level-1 block mixed (ACC-01.1 R-marker); markers at (9,1) and (1,9), records inactive
    out.append(dict(name='edge/marker-low', fine=[], gates=[(1, (9, 1, 1, 1), (9, 1), 0), (2, (1, 9, 1, 1), (1, 9), 0)],
                    requests=[((1.5, 1.5), GOAL, size, b, warp) for size in (0, 1) for b in (100000, 8) for warp in (0, 1)]))
    # coarse SE corner from a level-3 node descending to level 0 at size 2: the level-1 block at the
    # corner is fully clear in class (occupancy 163370), so only a marker at (9,9) can make it mixed
    out.append(dict(name='edge/marker-corner', fine=[], gates=[(3, (9, 9, 1, 1), (9, 9), 0)],
                    requests=[((1.5, 1.5), GOAL, size, b, warp) for size in (0, 1) for b in (100000, 8) for warp in (0, 1)]))
    # setup shortcuts and degenerate endpoints
    setup = [((4.25, 4.75), (4.25, 27.75)), ((4.25, 4.75), (27.25, 4.75)), ((4.25, 4.75), (4.75, 4.25)), ((1.25, 1.75), (6.5, 6.5)),
             ((16.5, 16.5), (4.25, 4.75)), ((4.25, 4.75), (16.5, 16.5)), ((4.25, 4.75), (60.5, 60.5))]
    out.append(dict(name='setup/wall', fine=fine_of({(16, 16)} | {(16, y) for y in range(W) if y != 3}), gates=[],
                    requests=[(s, g, size, b, 0) for s, g in setup for size in (0, 1) for b in (100000, 8)]))
    # gates: (marker, box base rect (x,y,w,h), destination base cell, active)
    gate_cases = {
        'open-far': [(1, (6, 6, 1, 1), (25, 25), 1)],
        'dest-blocked': [(1, (6, 6, 1, 1), (20, 20), 1)],
        'dest-same-x': [(1, (6, 6, 1, 1), (6, 22), 1)],
        'inactive': [(1, (6, 6, 1, 1), (25, 25), 0)],
        'self': [(1, (6, 6, 1, 1), (6, 6), 1)],
        'chain': [(1, (6, 6, 1, 1), (14, 4), 1), (2, (14, 4, 1, 1), (26, 26), 1)],
        'wide-source': [(3, (3, 8, 4, 2), (24, 10), 1)],
        'near-dest': [(1, (12, 12, 1, 1), (5, 6), 1)],
        'walled': [(1, (5, 12, 1, 1), (26, 20), 1)],
    }
    gate_fine = {'dest-blocked': fine_of({(20, 20)}), 'walled': fine_of({(16, y) for y in range(W) if y not in (1,)} | {(x, 16) for x in range(16, W)})}
    for name, gates in gate_cases.items():
        out.append(dict(name=f'gate/{name}', fine=gate_fine.get(name, []), gates=gates,
                        requests=[(START, GOAL, size, b, warp) for size in (0, 1) for b in (100000, 40, 8) for warp in (0, 1)]))
    return out


class Runner:
    def __init__(self, binary):
        self.retail = Retail(binary)
        self.retail.enable_coverage()
        self.retail.enable_events()
        self.clean_fine = bytes(self.retail.uc.mem_read(self.retail.cells, 64 * 64 * 4))

    def reset(self):
        r = self.retail
        r.uc.mem_write(r.cells, self.clean_fine)
        for storage, side in zip(r.data, r.sides):
            r.uc.mem_write(storage, bytes(side * side * 8))
        r.uc.mem_write(r.special, bytes(256 * 12))
        r.write(r.game + 0x54, 0)

    def build(self, scenario, lane):
        r = self.retail
        self.reset()
        flag = LANE_FLAG[lane]
        for fx, fy in scenario['fine']:
            r.terrain(fx, fy, flag, 1)
        r.rebuild()
        for marker, (bx, by, w, h), dest, active in scenario['gates']:
            r.publish_gate(marker, (64 * bx + 1, 64 * by + 1, 64 * (bx + w - 1) + 31, 64 * (by + h - 1) + 31))
            r.gate_destination(marker, (64 * dest[0] + 32, 64 * dest[1] + 32))
            r.gate_active(marker, active)

    def request(self, lane, source, goal, size, budget, warp):
        r = self.retail
        before_cov, before_ev = dict(r.coverage), dict(r.events)
        s = r.search(lane, source, goal, budget=budget, size_input=size, warp=warp)
        cov = {k for k, v in r.coverage.items() if v != before_cov.get(k, 0)}
        ev = {k for k, v in r.events.items() if v != before_ev.get(k, 0)}
        node_words = b''.join(struct.pack('<9I', *r.read(r.nodes + 36 * i, 9)) for i in range(s['node_count']))
        return s, cov, ev, hashlib.sha256(node_words).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--only', help='substring filter on scenario names (debug)')
    parser.add_argument('--witness-maps', type=Path, help='tools/ghidra/fixtures/research/ACC-01.2-witness-maps.json')
    parser.add_argument('--inventory', type=Path, help='ACC-01.1 inventory JSON: detail every reachable outcome absent from the accepted corpora')
    args = parser.parse_args()
    runner = Runner(args.binary)
    jccs = load_jccs()
    witness_jcc, witness_event = {}, {}
    rows = []
    for scenario in scenarios(args.witness_maps):
        if args.only and args.only not in scenario['name']:
            continue
        for lane in (0, 2, 4, 6):
            runner.build(scenario, lane)
            for index, (source, goal, size, budget, warp) in enumerate(scenario['requests']):
                s, cov, ev, digest = runner.request(lane, source, goal, size, budget, warp)
                ref = f"{scenario['name']}#{index}"
                rows.append(dict(ref=ref, lane=lane, source=list(source), goal=list(goal), size_input=size, budget=budget, warp=warp,
                                 result=s['result'], work=s['work'], nodes=s['node_count'], warp_count=s['warp_count'],
                                 route_words=[f'{w:08x}' for w in s['route_words']], node_sha256=digest))
                family = 'markers' if scenario['gates'] else scenario['name'].split('/')[0]
                for (va, outcome) in cov:
                    entry = witness_jcc.setdefault(f'{va:08x}:{outcome}', {})
                    entry.setdefault(str(lane), dict(ref=ref, size_input=size, warp=warp, budget=budget))
                    entry.setdefault('families', [])
                    if family not in entry['families']:
                        entry['families'].append(family)
                for key in ev:
                    witness_event.setdefault(json.dumps(key), {}).setdefault(str(lane), dict(ref=ref, size_input=size, warp=warp, budget=budget))
        print(scenario['name'], len(rows), 'requests;', len(witness_jcc), 'Jcc outcomes', flush=True)
    details = []
    if args.inventory:
        inventory = json.loads(args.inventory.read_text())
        wanted = [f"{r['va']}:{r['outcome']}" for r in inventory['outcomes']
                  if not r['existing_corpora'] and r['reach'] in ('R', 'R-low', 'R-gate', 'R-marker')]
        by_name = {sc['name']: sc for sc in scenarios(args.witness_maps)}
        for key in wanted:
            entry = witness_jcc.get(key)
            if not entry:
                details.append(dict(outcome=key, witness=None))
                continue
            first = entry['0'] if '0' in entry else next(v for k, v in entry.items() if k != 'families')
            lane = int(next(k for k in ('0', '2', '4', '6') if k in entry))
            name, index = first['ref'].split('#')
            scenario = by_name[name]
            runner.build(scenario, lane)
            source, goal, size, budget, warp = scenario['requests'][int(index)]
            s, cov, ev, digest = runner.request(lane, source, goal, size, budget, warp)
            if tuple(int(v, 16) if i == 0 else v for i, v in enumerate(key.split(':'))) not in cov:
                raise SystemExit(f'detail replay lost {key}')
            details.append(dict(outcome=key, lane=lane, ref=first['ref'], source=list(source), goal=list(goal), size_input=size, budget=budget,
                                warp=warp, fine_blocked=len(scenario['fine']), gates=[list(g) for g in scenario['gates']],
                                result=s['result'], work=s['work'], node_count=s['node_count'], warp_count=s['warp_count'],
                                route=s['route'], route_words=[f'{w:08x}' for w in s['route_words']],
                                nodes=[[n['x'], n['y'], n['level'], n['g'], n['h'], n['parent'], n['state'], n['source_marker'], n['incoming']] for n in s['nodes']]))
    from acc01_2_find_witnesses import event_targets, SITES
    observed = {tuple(json.loads(k)) for k in witness_event}
    transitions = {}
    for key in sorted(event_targets(), key=str):
        if key[0] == 'coarse':
            name = f'coarse level{key[1]} size2 clamp x={key[3]} y={key[4]}'
        else:
            name = f'{SITES[key[1]]} parent{key[2]}->child{key[3]} size{key[4]}'
        transitions[name] = 'observed' if key in observed else 'not observed'
    report = dict(
        binary_sha256=runner.retail.digest,
        transition_targets=len(transitions), transitions_observed=sum(v == 'observed' for v in transitions.values()),
        transitions=transitions,
        details_fields='nodes: x,y,level,g,h,parent,state(0 fresh/1 open/2 closed),source marker byte20,incoming byte23',
        details=details,
        scope='Producer-built (04d870/054000, 15d360, 04e360, 04e210, 04e550) maps; complete 162cb0 requests in the matching lane, sizes 1/2. '
              'Supplied empty 64x64 fine storage and padded 41/20/10/5 headers. Passive Jcc/event observation.',
        requests=len(rows), jcc_outcomes_observed=len(witness_jcc), jcc_outcomes_total=2 * len(jccs),
        jcc_witnesses=dict(sorted(witness_jcc.items())), event_witnesses=dict(sorted(witness_event.items())), rows=rows)
    text = json.dumps(report, indent=1) + '\n'
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text)
    if args.fixture and args.fixture.exists():
        frozen = json.loads(args.fixture.read_text())
        if frozen != json.loads(text):
            raise SystemExit('frozen ACC-01.2 expectation differs')
    print(f"{len(rows)} requests; {len(witness_jcc)} of {2 * len(jccs)} Jcc outcomes; {len(witness_event)} event keys")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
