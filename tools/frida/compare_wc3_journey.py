#!/usr/bin/env python3
"""Compare an original journey capture with the engine replay of the same probe map.

Inputs are the Frida JSONL written by trace_wc3_pathfinding.py --metadata-events
[--velocity-events] and the rows written by the wc3_journey.replay test
(WC3_JOURNEY_OUT, optionally WC3_JOURNEY_COMMITS). Samples are compared word for word:
current order, GetUnitFacing, GetUnitX and GetUnitY. Commit streams, when both exist,
locate the first divergent motion commit of a subject.

  python3 tools/frida/compare_wc3_journey.py retail.jsonl engine.jsonl \
      [--retail-commits retail.jsonl --engine-commits engine-commits.txt] [--json report.json]
"""
import argparse, collections, json, re, struct, sys

WORDS = ('order', 'facing', 'x', 'y')


def f32(word): return struct.unpack('<f', struct.pack('<I', word & 0xffffffff))[0]


def retail_samples(path):
    cells, markers, commits, complete = collections.defaultdict(dict), [], [], False
    for line in open(path):
        r = json.loads(line); e = r.get('event')
        if e == 'metadata-row': cells[r['parent']][r['child']] = r['word'] & 0xffffffff
        elif e == 'metadata-marker':
            m = re.fullmatch(r'PATHMETA journey tick=(\d+) first=(\d+) count=(\d+)', r['value'])
            if m: markers.append(tuple(map(int, m.groups())))
            complete |= r['value'] == 'PATHMETA complete'
        elif e == 'velocity-commit': commits.append(r)
    samples = {}
    for tick, first, count in markers:
        for row in range(first, first + count):
            c = cells.get(row)
            if c is None or sorted(c) != list(range(6)): raise ValueError(f'incomplete original row {row}')
            if c[0] != tick: raise ValueError(f'original row {row} tick differs')
            samples[(tick, c[1])] = (c[2], c[3], c[4], c[5])
    return samples, complete, commits


def engine_samples(path):
    samples = {}
    for line in open(path):
        r = json.loads(line); samples[(r['tick'], r['case'])] = tuple(r['words'])
    return samples


def compare(retail, engine):
    units = sorted({u for _, u in retail} | {u for _, u in engine})
    ticks = sorted({t for t, _ in retail} & {t for t, _ in engine})
    report = {'ticks': [ticks[0], ticks[-1]] if ticks else None, 'units': {}}
    for u in units:
        first = {k: None for k in WORDS}; worst = 0.0
        for t in ticks:
            a, b = retail.get((t, u)), engine.get((t, u))
            if a is None or b is None: continue
            for i, k in enumerate(WORDS):
                if a[i] != b[i] and first[k] is None: first[k] = t
            worst = max(worst, abs(f32(a[2]) - f32(b[2])), abs(f32(a[3]) - f32(b[3])))
        last = ticks[-1]
        a, b = retail.get((last, u)), engine.get((last, u))
        report['units'][u] = {'first_divergence': first, 'worst_position_error': round(worst, 3),
            'final': {'original': [a[0], round(f32(a[2]), 2), round(f32(a[3]), 2)] if a else None,
                      'engine': [b[0], round(f32(b[2]), 2), round(f32(b[3]), 2)] if b else None}}
    return report


def subject_commits(retail_commits, engine_path, retail):
    """Map movers to subjects through the tick-0 sample and an agreed fine-grid origin."""
    starts = {u: (f32(w[2]), f32(w[3])) for (t, u), w in retail.items() if t == 0}
    rseq, eseq = collections.defaultdict(list), collections.defaultdict(list)
    for r in retail_commits:
        a = r['after']; rseq[r['mover']].append((a[0], a[2], a[3], a[4], a[5], a[7]))
    for line in open(engine_path):
        p = line.split(); eseq[p[0]].append(tuple(int(x, 16) for x in p[1:]))
    def origin(seqs):
        votes = collections.Counter()
        for s in seqs.values():
            fx, fy = f32(s[0][1]) * 32, f32(s[0][2]) * 32
            for x, y in starts.values(): votes[(round(x - fx), round(y - fy))] += 1
        return votes.most_common(1)[0][0]
    def assign(seqs):
        ox, oy = origin(seqs); out = {}
        for key, s in seqs.items():
            x, y = f32(s[0][1]) * 32 + ox, f32(s[0][2]) * 32 + oy
            u = min(starts, key=lambda u: (starts[u][0] - x) ** 2 + (starts[u][1] - y) ** 2)
            if (starts[u][0] - x) ** 2 + (starts[u][1] - y) ** 2 < 1: out[u] = s
        return out
    return assign(rseq), assign(eseq)


def commit_streams(retail_commits, engine_path, retail):
    r, e = subject_commits(retail_commits, engine_path, retail)
    moving = lambda s: next((round(f32(c[0]), 2) for c in s if c[3] or c[4]), None)
    out = {}
    for u in sorted(set(r) & set(e)):
        a, b = r[u], e[u]; k = 0
        while k < min(len(a), len(b)) and a[k] == b[k]: k += 1
        row = {'original_commits': len(a), 'engine_commits': len(b), 'matched_prefix': k,
               'first_moving_clock': [moving(a), moving(b)]}
        if k < min(len(a), len(b)):
            row['original'] = [round(f32(w), 4) for w in a[k]]; row['engine'] = [round(f32(w), 4) for w in b[k]]
            row['previous'] = [round(f32(w), 4) for w in a[k - 1]] if k else None
        out[u] = row
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('retail'); p.add_argument('engine')
    p.add_argument('--engine-commits'); p.add_argument('--json')
    a = p.parse_args()
    retail, complete, commits = retail_samples(a.retail)
    engine = engine_samples(a.engine)
    report = compare(retail, engine); report['original_complete'] = complete
    if a.engine_commits and commits: report['commits'] = commit_streams(commits, a.engine_commits, retail)
    if a.json: open(a.json, 'w').write(json.dumps(report, indent=1) + '\n')
    exact = 0
    for u, row in report['units'].items():
        d = row['first_divergence']; c = report.get('commits', {}).get(u)
        exact += all(v is None for v in d.values())
        print(f"subject {u:2}: first x/y/order/facing divergence {d['x']}/{d['y']}/{d['order']}/{d['facing']} "
              f"worst {row['worst_position_error']} final {row['final']['original']} vs {row['final']['engine']}"
              + (f" | commits {c['matched_prefix']}/{c['original_commits']} moving {c['first_moving_clock']}" if c else ''))
    print(f"{exact}/{len(report['units'])} subjects exact over ticks {report['ticks']}; original complete={complete}")


if __name__ == '__main__':
    main()
