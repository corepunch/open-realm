#!/usr/bin/env python3
"""MAP-06.x: compare exact per-unit mover commit sequences (named movers) between captures.

usage: sep03_map06_compare_moves.py <reference.jsonl> <other.jsonl> [--from-tick N]
Named movers come from create windows (M, C0..C2) or, for loaded games, from mover position matching
at the first post-load commit (closest saved position). Prints JSON; never edits captures.
"""
import json
import sys


def seqs(path, from_tick=0):
    rows = [json.loads(l) for l in open(path) if l.strip()]
    names, out, loaded = {}, {}, []
    for r in rows:
        if r.get('event') == 'mover-activate' and r.get('name'):
            names[r['mover']] = r['name']
        if r.get('event') == 'mover-commit':
            if r['mover'] in names:
                if r['tick'] >= from_tick:
                    out.setdefault(names[r['mover']], []).append((r['tick'], *r['pos'], *r['vel']))
            elif r.get('loadGeneration', 0) > 0:
                loaded.append(r)
    return out, loaded


def dedup(seq):
    res = []
    for t in seq:
        if not res or res[-1][1:3] != t[1:3]:
            res.append(t)
    return res


def main():
    ref, other = sys.argv[1], sys.argv[2]
    from_tick = int(sys.argv[sys.argv.index('--from-tick') + 1]) if '--from-tick' in sys.argv else 0
    a, _ = seqs(ref, from_tick)
    b, loaded = seqs(other, from_tick)
    if not b and loaded:
        # Loaded game: movers are reconstructed without activation events; match by first committed position.
        by_mover = {}
        for r in loaded:
            by_mover.setdefault(r['mover'], []).append((r['tick'], *r['pos'], *r['vel']))
        for mover, seq in by_mover.items():
            first = seq[0][1:3]
            best = None
            for name, rs in a.items():
                for t in rs:
                    if t[1:3] == first:
                        best = name
                        break
                if best:
                    break
            if best:
                b[best] = seq
    res = {}
    for name in sorted(a):
        x, y = a[name], b.get(name, [])
        dx, dy = dedup(x), dedup(y)
        if dy:
            start = next((i for i, t in enumerate(dx) if t[1:3] == dy[0][1:3]), None)
        else:
            start = None
        cmp_x = dx[start:] if start is not None else dx
        n = min(len(cmp_x), len(dy))
        eq = next((i for i in range(n) if cmp_x[i][1:3] != dy[i][1:3]), n)
        res[name] = dict(ref_commits=len(x), other_commits=len(y), ref_distinct=len(dx), other_distinct=len(dy),
                         aligned_at_ref_distinct_index=start, compared=n, equal_distinct_positions=eq,
                         ref_final=list(dx[-1][1:3]) if dx else None, other_final=list(dy[-1][1:3]) if dy else None,
                         first_difference=None if eq == n else dict(ref=cmp_x[eq], other=dy[eq]))
    print(json.dumps(dict(reference=ref, other=other, from_tick=from_tick, units=res), indent=1))


if __name__ == '__main__':
    main()
