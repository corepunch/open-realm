#!/usr/bin/env python3
"""FORM-04.2 frozen expectation builder (research tool, new file).

Input: form042_analyze.py reports. Each named set lists repeated captures (report[index] entries). Probe ticks are
removed (click phase); owner-visit numbers, words, units and event order are kept. The first capture of each set is
frozen; every further capture must be equal (`repeat_equal`) or its differing keys are listed. Forced-state sets are
kept separate from public sets by name (prefix `forced_`).
"""
import argparse, hashlib, json
from pathlib import Path


def strip(x):
    groups = []
    for g in x['groups']:
        groups.append(dict(
            identity=g['group'].split('#')[1], visits=g['visits'],
            route=[[r[0]] + r[2:] for r in g['route']], failures=[[f[0]] + f[2:] for f in g['failures']],
            stops=[[s[0], s[2]] for s in g['stops']], cooldown_seeds=[[s[0], s[2]] for s in g['cooldown_seeds']],
            bit80000=g['bit80000'], layouts=[[l[0], l[2], l[3]] for l in g['layouts']],
            advances=[[a[0], a[2], a[3]] for a in g['advances']], resets=[[r[0], r[2]] for r in g['resets']]))
    return dict(groups=groups, warps=[dict(unit=w['unit'], caller=w['caller'], before=w['before'], after=w['after']) for w in x['warps']])


def keys(a, b, path=''):
    if type(a) != type(b):
        return [path]
    if isinstance(a, dict):
        return [k for key in sorted(set(a) | set(b)) for k in keys(a.get(key), b.get(key), path + '/' + str(key))]
    if isinstance(a, list):
        if len(a) != len(b):
            return [path + ' (len %d/%d)' % (len(a), len(b))]
        return [k for i, (p, q) in enumerate(zip(a, b)) for k in keys(p, q, path + '/' + str(i))]
    return [] if a == b else [path]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--set', action='append', required=True, help='NAME=report.json:index[,report.json:index...]')
    ap.add_argument('--extra', action='append', default=[], help='NAME=file (embedded by digest)')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    exp = dict(task='FORM-04.2', sets={}, extras={}, sources={})
    for spec in args.set:
        name, items = spec.split('=', 1)
        caps = []
        for it in items.split(','):
            f, i = it.rsplit(':', 1)
            p = Path(f)
            exp['sources'][p.name] = hashlib.sha256(p.read_bytes()).hexdigest()
            rep = json.loads(p.read_text())[int(i)]
            caps.append((rep['capture'], rep['sha256'], strip(rep)))
        first = caps[0][2]
        diffs = [sorted(set(keys(first, c[2]))) for c in caps[1:]]
        exp['sets'][name] = dict(captures=[[c[0], c[1]] for c in caps], repeat_equal=[not d for d in diffs], repeat_differences=diffs,
                                 expected=first)
    for spec in args.extra:
        name, f = spec.split('=', 1)
        p = Path(f)
        exp['extras'][name] = dict(file=p.name, sha256=hashlib.sha256(p.read_bytes()).hexdigest())
    args.output.write_text(json.dumps(exp, indent=1) + '\n')
    print(hashlib.sha256(args.output.read_bytes()).hexdigest())
    for n, s in exp['sets'].items():
        print(n, s['repeat_equal'], [d[:12] for d in s['repeat_differences']])


if __name__ == '__main__':
    main()
