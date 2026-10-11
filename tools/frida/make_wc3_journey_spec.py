#!/usr/bin/env python3
"""Generate a seeded long-journey spec for any original map.

Open spawn/destination areas come from the map's war3map.wpm walkability and
war3map.doo object positions. Subjects mix races, collision sizes, speeds and
movement types; the timeline issues cross-map group orders (up to twelve members,
i.e. formations), individual Move/Patrol/Stop, mid-route redirects and flyers.

  python3 tools/frida/make_wc3_journey_spec.py --data "$WC3_DATA" \
      --base 'war3.mpq:Maps\\Campaign\\Human05.w3m' --name human05_crossing --seed 5 \
      --output tools/frida/journeys/human05_crossing.json
"""
import argparse, json, random, struct, subprocess, tempfile
from pathlib import Path

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


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--data', type=Path, required=True); p.add_argument('--base', required=True)
    p.add_argument('--name', required=True); p.add_argument('--seed', type=int, default=1)
    p.add_argument('--ticks', type=int, default=2400); p.add_argument('--sample-every', type=int, default=2)
    p.add_argument('--groups', type=int, default=3); p.add_argument('--singles', type=int, default=8)
    p.add_argument('--tool', type=Path, default=Path('build/bin/mpqtool')); p.add_argument('--output', type=Path, required=True)
    a = p.parse_args(); rng = random.Random(a.seed); tool = str(a.tool.resolve())
    w, h, cells, ox, oy, objects = world(tool, a.data, a.base)
    spots = open_spots(w, h, cells, ox, oy, objects)
    if len(spots) < a.groups * 2 + a.singles: p.error(f'only {len(spots)} open spots')
    anchors = spread(spots, a.groups * 2 + 4, rng)
    eighth = lambda v: round(v * 8) / 8
    units, timeline = [], []
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
