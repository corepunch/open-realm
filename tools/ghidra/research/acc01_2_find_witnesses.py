#!/usr/bin/env python3
"""ACC-01.2 research helper: bounded randomized search that produced the inputs in
tools/ghidra/fixtures/research/ACC-01.2-witness-maps.json.

Each trial builds a producer-built map (original 04d870/054000 setters, 15d360, and
for 'gate' the 04e360/04e550/04e210 bridges) and runs complete 162cb0 requests in
lane 0, stopping when every target Jcc outcome has been observed. The search is
only a way to find inputs; the accepted evidence is the deterministic replay in
verify_acc01_2_witnesses.py --witness-maps.

  gate   : Random(1234), 600 trials, warp 1  (targets 16528f:fall 165297:taken/fall 1652d8:taken 164200:taken)
  reopen : Random(4321), 1500 trials, warp 0 (target 164200:taken through ordinary 164020)
  events : Random(12), 365 trials, lane 0, both sizes, optional single gate (warp 1): relax/lookup level
           transitions and size-2 clamp combinations missing from the structured scenarios
"""
import argparse
import json
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_acc01_2_witnesses import Runner, fine_of, W  # noqa: E402


def gate(runner):
    targets = {'6f16528f:fall', '6f165297:taken', '6f165297:fall', '6f1652d8:taken', '6f164200:taken'}
    found, rng = {}, random.Random(1234)
    for trial in range(600):
        density = rng.choice([0, 0, .1, .2])
        blocked = {(x, y) for y in range(W) for x in range(W) if rng.random() < density}
        blocked -= {(x, y) for x in range(2, 7) for y in range(2, 7)}
        sx, sy = rng.randrange(W), rng.randrange(W)
        dx, dy = rng.randrange(W), rng.randrange(W)
        blocked -= {(sx, sy), (dx, dy)}
        gx, gy = rng.randrange(W), rng.randrange(W)
        blocked.discard((gx, gy))
        runner.build(dict(name='t', fine=fine_of(blocked), gates=[(1, (sx, sy, 1, 1), (dx, dy), 1)]), 0)
        for size in (0, 1):
            _, cov, _, _ = runner.request(0, (4.25, 4.75), (gx + .25, gy + .75), size, 100000, 1)
            for key in {f'{a:08x}:{o}' for a, o in cov} & targets:
                found.setdefault(key, dict(trial=trial, density=density, blocked=sorted(blocked), source=(sx, sy), dest=(dx, dy), goal=(gx, gy), size=size))
        if len(found) == len(targets):
            break
    return found


def reopen(runner):
    rng = random.Random(4321)
    for trial in range(1500):
        fine_level = rng.random() < .5
        density = rng.choice([.05, .1, .2, .3])
        if fine_level:
            cells = {(x, y) for y in range(64) for x in range(64) if rng.random() < density}
        else:
            cells = set(fine_of({(x, y) for y in range(W) for x in range(W) if rng.random() < density}))
        sx, sy = rng.randrange(W), rng.randrange(W)
        gx, gy = rng.randrange(W), rng.randrange(W)
        cells -= {(2 * sx + a, 2 * sy + b) for a in (0, 1) for b in (0, 1)} | {(2 * gx + a, 2 * gy + b) for a in (0, 1) for b in (0, 1)}
        runner.build(dict(name='t', fine=sorted(cells), gates=[]), 0)
        for size in (0, 1):
            _, cov, _, _ = runner.request(0, (sx + .25, sy + .75), (gx + .25, gy + .75), size, 100000, 0)
            if (0x6f164200, 'taken') in cov:
                return dict(trial=trial, cells=sorted(cells), source=(sx, sy), goal=(gx, gy), size=size)
    return None


SITES = {'6f1631bb': 'baseE', '6f1638fb': 'baseNE', '6f163a5e': 'baseNW', '6f163b3c': 'baseN', '6f1642eb': 'baseSE',
         '6f16333f': 'sideE', '6f1639c6': 'cornerNE', '6f163ab7': 'cornerNW', '6f163c9f': 'sideN', '6f1643a6': 'cornerSE',
         '6f1646ce': 'baseSW', '6f1647a8': 'cornerSW', '6f16484b': 'baseS', '6f1649cf': 'sideS', '6f165020': 'baseW',
         '6f16516f': 'sideW', '6f1653be': 'special'}


def event_targets():
    targets = set()
    for site, name in SITES.items():
        for stored in (1, 2):
            if name.startswith('base') or name == 'special':
                combos = [(0, c) for c in range(4)]
            else:
                combos = [(p, c) for p in (1, 2, 3) for c in range(4)]
            for p, c in combos:
                targets.add(('special' if name == 'special' else 'relax', site, p, c, stored))
    for level in (1, 2, 3):
        for cx in (False, True):
            for cy in (False, True):
                targets.add(('coarse', level, 2, cx, cy))
    return targets


def events(runner, known_path, seed=12, trials=365):
    known = {tuple(json.loads(k)) for k in json.loads(Path(known_path).read_text())['event_witnesses']}
    missing = sorted(t for t in event_targets() if t not in known)
    rng, found = random.Random(seed), {}
    for trial in range(trials):
        mode = rng.random()
        density = rng.choice([.02, .05, .1, .2])
        if mode < .5:
            cells = {(x, y) for y in range(64) for x in range(64) if rng.random() < density}
        else:
            cells = set(fine_of({(x, y) for y in range(W) for x in range(W) if rng.random() < density}))
        s = (rng.randrange(W) + rng.random(), rng.randrange(W) + rng.random())
        g = (rng.randrange(W) + rng.random(), rng.randrange(W) + rng.random())
        gates = []
        if rng.random() < .3:
            sx, sy, dx, dy = [rng.randrange(W) for _ in range(4)]
            gates = [(1, (sx, sy, 1, 1), (dx, dy), 1)]
        cells -= {(int(2 * s[0]) + a, int(2 * s[1]) + b) for a in (-1, 0, 1, 2) for b in (-1, 0, 1, 2)}
        cells -= {(int(2 * g[0]) + a, int(2 * g[1]) + b) for a in (-1, 0, 1, 2) for b in (-1, 0, 1, 2)}
        runner.build(dict(name='t', fine=sorted(cells), gates=gates), 0)
        for size in (0, 1):
            _, _, ev, _ = runner.request(0, s, g, size, 100000, 1 if gates else 0)
            for key in ev:
                if key in missing and json.dumps(key) not in found:
                    found[json.dumps(key)] = dict(trial=trial, cells=sorted(cells), gates=gates, source=s, goal=g, size=size, warp=1 if gates else 0)
    found['_still_missing'] = [list(k) for k in missing if json.dumps(k) not in found]
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--mode', choices=('gate', 'reopen', 'events'), required=True)
    parser.add_argument('--known', type=Path, help='events mode: a witnesses JSON from verify_acc01_2_witnesses.py (structured scenarios)')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    runner = Runner(args.binary)
    if args.mode == 'events':
        result = events(runner, args.known)
        args.out.write_text(json.dumps(result) + '\n')
        print(sorted(k for k in result if not k.startswith('_')), result['_still_missing'])
        return 0
    result = gate(runner) if args.mode == 'gate' else reopen(runner)
    args.out.write_text(json.dumps(result) + '\n')
    print(json.dumps({k: v['trial'] for k, v in result.items()} if args.mode == 'gate' else (result or {}).get('trial')))


if __name__ == '__main__':
    raise SystemExit(main())
