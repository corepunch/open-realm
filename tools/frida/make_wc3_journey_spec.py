#!/usr/bin/env python3
"""Generate a seeded long-journey spec for any original map.

Open spawn/destination areas come from the map's war3map.wpm walkability and
war3map.doo object positions. Subjects mix races, collision sizes, speeds and
movement types; the timeline issues cross-map group orders (up to twelve members,
i.e. formations), individual Move/Patrol/Stop, mid-route redirects and flyers.

--headon PAIRS instead pairs Hero-led groups of mixed collision sizes at distant
spots and swaps each pair simultaneously three times, so both groups meet on the
same corridor. Single units follow the Heroes (target Move), and lone units
exchange places with each other. --choke places those pairs on opposite sides
of narrow walkable passages found in war3map.wpm, so they meet inside them.

--converge GROUPS sends Hero-led groups from spread-out spots to one shared
point at the same time, scatters them home, then converges again on a second
point, with a Patrol through the jam and Stop/re-issue orders mid-crowd.

  python3 tools/frida/make_wc3_journey_spec.py --data "$WC3_DATA" \
      --base 'war3.mpq:Maps\\Campaign\\Human05.w3m' --name human05_crossing --seed 5 \
      --output tools/frida/journeys/human05_crossing.json
"""
import argparse, json, random, struct, subprocess, tempfile
from pathlib import Path

HEROES = ['Hpal', 'Obla', 'Udea', 'Ekee', 'Hamg', 'Ofar', 'Ulic', 'Emoo']
LARGE = ['hkni', 'otau', 'uabo', 'hmtm', 'edry', 'ucry']
GROUND = ['hfoo', 'hkni', 'hrif', 'hmtm', 'hsor', 'hpea', 'ogru', 'otau', 'ohun', 'opeo', 'ugho', 'uabo', 'ucry',
          'uaco', 'earc', 'esen', 'edry', 'ewsp', 'Hpal', 'Obla', 'Udea', 'Ekee']
FLYING = ['hgry', 'owyv', 'hgyr', 'ehip', 'ugar']


def member(tool, data, base, name):
    if ':' in base:
        archive, inner = base.split(':', 1)
        blob = subprocess.check_output([tool, '-mpq', str(data / archive), 'cat', inner])
        with tempfile.NamedTemporaryFile(suffix='.w3m') as f:
            f.write(blob); f.flush()
            return subprocess.check_output([tool, '-mpq', f.name, 'cat', name])
    return subprocess.check_output([tool, '-mpq', str(data / base), 'cat', name])


def world(tool, data, base):
    wpm = member(tool, data, base, 'war3map.wpm'); w, h = struct.unpack_from('<II', wpm, 8); cells = wpm[16:16 + w * h]
    w3e = member(tool, data, base, 'war3map.w3e')
    o = 13; o += 4 + 4 * struct.unpack_from('<I', w3e, o)[0]; o += 4 + 4 * struct.unpack_from('<I', w3e, o)[0]
    _, _, ox, oy = struct.unpack_from('<IIff', w3e, o)
    doo = member(tool, data, base, 'war3map.doo'); version, _, count = struct.unpack_from('<III', doo, 4)
    size = 42 if version == 7 else None
    objects = []
    if size:
        for i in range(count):
            x, y = struct.unpack_from('<ff', doo, 16 + i * size + 8); objects.append((x, y))
    return w, h, cells, ox, oy, objects


def open_spots(w, h, cells, ox, oy, objects, radius=5):
    near = set((int((x - ox) / 128), int((y - oy) / 128)) for x, y in objects)
    spots = []
    for cy in range(radius, h - radius, 4):
        for cx in range(radius, w - radius, 4):
            if any(cells[(cy + dy) * w + cx + dx] & 0x0a for dy in range(-radius, radius + 1) for dx in range(-radius, radius + 1)):
                continue
            if any((cx * 32 // 128 + i, cy * 32 // 128 + j) in near for i in (-1, 0, 1) for j in (-1, 0, 1)): continue
            spots.append((ox + cx * 32 + 16.0, oy + cy * 32 + 16.0))
    return spots


def spread(spots, k, rng):
    chosen = [rng.choice(spots)]
    while len(chosen) < k:
        chosen.append(max(spots, key=lambda s: min((s[0] - c[0]) ** 2 + (s[1] - c[1]) ** 2 for c in chosen)))
    return chosen


def clearance(w, h, cells):
    """Chebyshev distance, in pathing cells, from each cell to the nearest unwalkable one."""
    import collections
    far = w + h; dist = [far] * (w * h); queue = collections.deque()
    for i, c in enumerate(cells):
        x, y = i % w, i // w
        if c & 0x02 or x in (0, w - 1) or y in (0, h - 1): dist[i] = 0; queue.append(i)
    while queue:
        i = queue.popleft(); x, y = i % w, i // w
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h and dist[ny * w + nx] > dist[i] + 1:
                    dist[ny * w + nx] = dist[i] + 1; queue.append(ny * w + nx)
    return dist


def chokepoints(w, h, cells, ox, oy, count, rng, reach=24):
    """Pairs of open ends whose straight segment crosses one narrow passage."""
    import math
    dist = clearance(w, h, cells); found = []
    for cy in range(reach, h - reach, 3):
        for cx in range(reach, w - reach, 3):
            if not 2 <= dist[cy * w + cx] <= 4: continue
            for k in range(8):
                dx, dy = math.cos(k * math.pi / 8), math.sin(k * math.pi / 8)
                a = (round(cx - dx * reach), round(cy - dy * reach)); b = (round(cx + dx * reach), round(cy + dy * reach))
                if min(dist[a[1] * w + a[0]], dist[b[1] * w + b[0]]) < 8: continue
                line = [dist[round(cy + dy * t) * w + round(cx + dx * t)] for t in range(-reach, reach + 1)]
                if min(line) < 2 or min(line[reach - 3:reach + 4]) > 4: continue
                found.append(((cx, cy), a, b))
    rng.shuffle(found); chosen = []
    for c, a, b in found:
        if all((c[0] - d[0][0]) ** 2 + (c[1] - d[0][1]) ** 2 > (2 * reach) ** 2 for d in chosen): chosen.append((c, a, b))
        if len(chosen) == count: break
    world = lambda p: (ox + p[0] * 32 + 16.0, oy + p[1] * 32 + 16.0)
    return [(world(a), world(b)) for _, a, b in chosen]


def headon(a, rng, spots, units, timeline, eighth, pairs=None):
    ends = [e for pair in pairs for e in pair] if pairs else spread(spots, a.headon * 2, rng)
    heroes = []
    for pair in range(a.headon):
        sides = (ends[2 * pair], ends[2 * pair + 1]); spans = []
        for side, (sx, sy) in enumerate(sides):
            size = rng.randint(8, min(12, (64 - 8) // (2 * a.headon))); first = len(units); heroes.append(first)
            types = [rng.choice(HEROES)] + [rng.choice(LARGE if rng.random() < 0.4 else GROUND[:18]) for _ in range(size - 1)]
            for i, kind in enumerate(types):
                units.append(dict(type=kind, at=[eighth(sx + (i % 4) * 112.0 - 168), eighth(sy + (i // 4) * 112.0 - 112)],
                                  facing=float(rng.choice([0, 90, 180, 270]))))
            spans.append((first, len(units) - 1))
        for leg in range(3):
            tick = 10 + leg * a.ticks // 3 + pair
            for side, span in enumerate(spans):
                dest = sides[(side + leg + 1) % 2]
                timeline.append(dict(tick=tick, group=list(span), order='move', to=[eighth(dest[0]), eighth(dest[1])]))
    for hero in heroes[:4]:
        (hx, hy) = units[hero]['at']; i = len(units)
        units.append(dict(type=rng.choice(GROUND[:18]), at=[eighth(hx - 256), eighth(hy - 256)], facing=0.0))
        timeline.append(dict(tick=12, unit=i, order='move', target=hero))
    lanes = spread(spots, 4, rng)
    for k in range(4):
        (sx, sy), (dx, dy) = lanes[k], lanes[(k + 2) % 4]; i = len(units)
        units.append(dict(type=rng.choice(LARGE + GROUND[:6]), at=[eighth(sx), eighth(sy)], facing=float(rng.randrange(0, 360, 45))))
        for leg in range(4):
            to = (dx, dy) if leg % 2 == 0 else (sx, sy)
            timeline.append(dict(tick=10 + leg * a.ticks // 4, unit=i, order='move', to=[eighth(to[0]), eighth(to[1])]))


def converge(a, rng, spots, units, timeline, eighth):
    homes = spread(spots, a.converge + 2, rng); meet = homes[-2:]; homes = homes[:-2]
    size = min(12, (64 - 4) // a.converge); spans = []
    for g, (sx, sy) in enumerate(homes):
        first = len(units)
        types = [rng.choice(HEROES)] + [rng.choice(LARGE if rng.random() < 0.35 else GROUND[:18]) for _ in range(rng.randint(size - 3, size) - 1)]
        for i, kind in enumerate(types):
            units.append(dict(type=kind, at=[eighth(sx + (i % 4) * 112.0 - 168), eighth(sy + (i // 4) * 112.0 - 112)],
                              facing=float(rng.choice([0, 90, 180, 270]))))
        spans.append((first, len(units) - 1))
    third = a.ticks // 3
    for g, span in enumerate(spans):
        home = homes[g]
        timeline.append(dict(tick=10 + g, group=list(span), order='move', to=[eighth(meet[0][0]), eighth(meet[0][1])]))
        timeline.append(dict(tick=third + 3 * g, group=list(span), order='move', to=[eighth(home[0]), eighth(home[1])]))
        timeline.append(dict(tick=2 * third + 2 * g, group=list(span), order='move', to=[eighth(meet[1][0]), eighth(meet[1][1])]))
        # Mid-crowd interruptions: one member stops and is re-issued, another patrols through.
        stop = span[0] + 1 + g % max(1, span[1] - span[0])
        timeline.append(dict(tick=third // 2 + 7 * g, unit=stop, immediate='stop'))
        timeline.append(dict(tick=third // 2 + 7 * g + 15, unit=stop, order='move', to=[eighth(meet[0][0]), eighth(meet[0][1])]))
    for k in range(4):
        (sx, sy) = rng.choice(spots); i = len(units)
        units.append(dict(type=rng.choice(GROUND[:6] + LARGE), at=[eighth(sx), eighth(sy)], facing=0.0))
        timeline.append(dict(tick=20 + k, unit=i, order='patrol', to=[eighth(meet[k % 2][0]), eighth(meet[k % 2][1])]))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--data', type=Path, required=True); p.add_argument('--base', required=True)
    p.add_argument('--name', required=True); p.add_argument('--seed', type=int, default=1)
    p.add_argument('--ticks', type=int, default=2400); p.add_argument('--sample-every', type=int, default=2)
    p.add_argument('--groups', type=int, default=3); p.add_argument('--singles', type=int, default=8)
    p.add_argument('--headon', type=int, default=0, metavar='PAIRS', help='Hero-led group pairs that swap places')
    p.add_argument('--choke', action='store_true', help='with --headon: put each pair across a narrow passage')
    p.add_argument('--converge', type=int, default=0, metavar='GROUPS', help='Hero-led groups converging on one point')
    p.add_argument('--tool', type=Path, default=Path('build/bin/mpqtool')); p.add_argument('--output', type=Path, required=True)
    a = p.parse_args(); rng = random.Random(a.seed); tool = str(a.tool.resolve())
    w, h, cells, ox, oy, objects = world(tool, a.data, a.base)
    spots = open_spots(w, h, cells, ox, oy, objects)
    if len(spots) < a.groups * 2 + a.singles: p.error(f'only {len(spots)} open spots')
    anchors = spread(spots, a.groups * 2 + 4, rng)
    eighth = lambda v: round(v * 8) / 8
    units, timeline = [], []
    if a.headon:
        pairs = chokepoints(w, h, cells, ox, oy, a.headon, rng) if a.choke else None
        if a.choke and len(pairs) < a.headon: p.error(f'only {len(pairs)} separated chokepoints')
        headon(a, rng, spots, units, timeline, eighth, pairs)
        a.groups = a.singles = 0
    elif a.converge:
        converge(a, rng, spots, units, timeline, eighth)
        a.groups = a.singles = 0
    for g in range(a.groups):
        size = rng.randint(6, 12); (sx, sy) = anchors[g]; first = len(units)
        for i in range(size):
            units.append(dict(type=rng.choice(GROUND), at=[eighth(sx + (i % 4) * 112.0 - 168), eighth(sy + (i // 4) * 112.0 - 112)],
                              facing=float(rng.choice([0, 90, 180, 270]))))
        dest = anchors[a.groups + g]
        timeline.append(dict(tick=10 + g, group=[first, len(units) - 1], order='move', to=[eighth(dest[0]), eighth(dest[1])]))
        redirect = rng.choice(anchors)
        timeline.append(dict(tick=a.ticks // 3 + 7 * g, group=[first, len(units) - 1], order='move', to=[eighth(redirect[0]), eighth(redirect[1])]))
        back = anchors[g]
        timeline.append(dict(tick=2 * a.ticks // 3 + 5 * g, group=[first, len(units) - 1], order=rng.choice(['move', 'patrol']), to=[eighth(back[0]), eighth(back[1])]))
    for s in range(a.singles):
        (sx, sy) = rng.choice(spots); i = len(units)
        flying = s % 4 == 3
        units.append(dict(type=rng.choice(FLYING if flying else GROUND), at=[eighth(sx), eighth(sy)], facing=float(rng.randrange(0, 360, 45))))
        for tick in sorted(rng.sample(range(10, a.ticks - 50), 3)):
            kind = rng.random()
            if kind < 0.15: timeline.append(dict(tick=tick, unit=i, immediate='stop'))
            else:
                dest = rng.choice(anchors + spots[::max(1, len(spots) // 50)])
                timeline.append(dict(tick=tick, unit=i, order='patrol' if kind > 0.75 else 'move', to=[eighth(dest[0]), eighth(dest[1])]))
    cx, cy = anchors[0]
    spec = dict(name=a.name, base=(dict(mpq=a.base.split(':')[0], member=a.base.split(':', 1)[1]) if ':' in a.base else a.base),
                seed=a.seed, ticks=a.ticks, sample_every=a.sample_every, camera=[eighth(cx), eighth(cy)], units=units,
                timeline=sorted(timeline, key=lambda s: s['tick']))
    a.output.write_text(json.dumps(spec, indent=1) + '\n')
    print(f'{a.name}: {len(units)} subjects, {len(timeline)} orders, {len(spots)} open spots')


if __name__ == '__main__':
    main()
