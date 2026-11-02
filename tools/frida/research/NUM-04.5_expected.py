#!/usr/bin/env python3
"""Freeze expected-NUM-04.5.json from NUM-04.5_analyze.py output (analysis-all.json).

Only derived words are frozen: setup flags/seed decision, owner and 45-stream words at startup milestones, the ordered
owner/stream draws from setup through the first separation visit (caller, before, after, value), race results and the
public JASS query results printed by the probe maps, plus observer-free control comparisons.
"""
import argparse, hashlib, json, re
from pathlib import Path


def milestones(c):
    out = {}
    for t in c['timeline']:
        w = t['what']
        if w in ('owner-seed', 'resolve-races', 'jass-main-setcamerabounds', 'first-owner-visit', 'first-mover-update', 'separation-visit',
                 'setup-seed-decision', 'setup-descriptor', 'setup-descriptor-leave', 'streams-reseed', 'SetRandomSeed') and w not in out:
            out[w] = {k: v for k, v in t.items() if k not in ('seq',)}
    return out


def startup_draws(c):
    rows = []
    for t in c['timeline']:
        if t['what'] == 'separation-visit':
            break
        if t['what'] == 'draw':
            rows.append(dict(cls=t['cls'], caller=t['caller'], before=t['before'], after=t['after'], value=t['value'], visit=t['visit']))
    return rows


def probe_values(lines):
    keep = []
    for s in lines or []:
        if re.search(r'label=(race|query|streams|reseed|complete|start_)', s):
            keep.append(s)
    return keep


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--analysis', type=Path, required=True)
    ap.add_argument('--streams-oracle', type=Path, required=True, help='NUM-04.6 oracle fixture (war3 table)')
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    an = json.loads(a.analysis.read_text())
    war3 = json.loads(a.streams_oracle.read_text())['reseed'][0]
    assert war3['seed'] == 0x77617233
    caps = {}
    for name, c in an['captures'].items():
        caps[name] = dict(capture_sha256=c['capture_sha256'], locked=c['locked'], flags=c['flags'], lobby_seed=c['lobby_seed'], game_seed=c['game_seed'],
                          race_draws=c['race_draws'], checks=c['checks'], mismatch_count=c['mismatch_count'], milestones=milestones(c),
                          startup_draws=startup_draws(c), draw_callers=c['draw_callers'])
    pre = {k: probe_values(v) for k, v in an['preload'].items() if v is not None}
    out = dict(task='NUM-04.5', binary_sha256='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236',
               analysis_sha256=hashlib.sha256(a.analysis.read_bytes()).hexdigest(), analyzer_sha256=an['analyzer_sha256'],
               rule=dict(lock_flag=0x8000, locked_seed=0x77617233, boot_owner_seed=0x69707365,
                         owner_after_locked_seed=[2002874931, 738765888], race_draw='owner Next>>30 +1 for each of 12 slots whose preference has 0x20',
                         streams_after_seed='693710: local=Seed(seed); stream[i]=Seed(Next(local)) for i=0..44'),
               war3_streams=war3['streams'], captures=caps, preload_probe_lines=pre,
               preload_comparisons=an['preload_comparisons'])
    a.out.write_text(json.dumps(out, indent=1) + '\n')
    print(hashlib.sha256(a.out.read_bytes()).hexdigest(), a.out)


if __name__ == '__main__':
    main()
