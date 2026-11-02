#!/usr/bin/env python3
"""Verify archived target visibility policies and raw fog-pursuit owner states."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/frida/research'))
from target021_analyze import analyze, segment
from target03_analyze import scene_report
from target03_expected import COMPARED, episodes


def fog_rows(rows):
    scene = analyze(rows)['scenes'][0]
    lo, hi = scene['window']
    out = []
    for visit in segment(rows)[0]:
        start = visit['start']
        members = [m for m in start['members'] if m.get('id') == scene['follower']]
        if not members or not lo <= start['c'] <= hi:
            continue
        member = members[0]
        group, path = start['path'], member['path']
        row = [start['c'], start['cd'], start['unseen'], *group['dest'], *group['times'],
               group['cnt'][1], group['idx'][1], *member['pos'], *member['vel'], member['range'],
               *path['dest'], *path['times'], *path['cnt'], *path['idx'], *path['retry'], start['flags'] & 1]
        out.append([x & 0xffffffff for x in row])
    return out


def hidden_policy(rows):
    """Check the actual owner outputs rather than only summarized outcomes."""
    hidden = 0
    for visit in segment(rows)[0]:
        start, end = visit['start'], visit['end']
        queries = [r for r in visit['events'] if r.get('event') == 'vis-query' and r['role'] == 'group-callback']
        if not queries:
            continue
        if len(queries) != 1 or end is None:
            raise ValueError('incomplete visibility owner visit')
        query = queries[0]
        if query['flags'] != 0 or query['mode'] != 4:
            raise ValueError('unexpected group visibility policy')
        samples = [r for r in visit['events'] if r.get('event') == 'sample']
        if len(samples) != 1:
            raise ValueError('missing or duplicate target sampler')
        # The hook records entry/exit of the sampler on every visit, including
        # visits that return the old cached point without reading the target.
        kept = all(r['dest'] == start['path']['dest'] for r in samples)
        if not query['result']:
            if end['unseen'] != start['unseen'] + 1 or not kept:
                raise ValueError('hidden owner sampled or lost its unseen counter')
            if end['cd'] != max(0, start['cd'] - 1):
                raise ValueError('hidden countdown changed cadence')
            hidden += 1
        elif end['unseen'] != 0 or (start['cd'] > 0 and not kept):
            raise ValueError('reacquisition bypassed the countdown')
    return hidden


def verify(frozen, archive, header):
    observations, controls, markers, hidden = 0, 0, 0, 0
    public, repeats = {}, {}
    for name, pin in frozen['captures'].items():
        path = archive / name
        raw = path.read_bytes()
        if len(raw) != pin['bytes'] or hashlib.sha256(raw).hexdigest() != pin['sha256']:
            raise ValueError('capture pin differs')
        rows = [json.loads(line) for line in raw.splitlines()]
        meta = rows[0]
        if meta != pin['metadata'] or meta.get('owned') is not True or meta['sha256'] != frozen['binary_sha256']:
            raise ValueError('capture provenance differs')
        preload = path.with_name(path.stem + '-preload.txt').read_bytes()
        footers = [r for r in rows if r.get('event') == 'preload-file']
        if len(footers) != 1 or not footers[0]['complete'] or footers[0]['sha256'] != pin['preload_sha256']:
            raise ValueError('missing complete public preload')
        if hashlib.sha256(preload).hexdigest() != pin['preload_sha256']:
            raise ValueError('public preload pin differs')
        values = [v for v in re.findall(rb'call Preload\( "([^"\r\n]+)" \)', preload)
                  if v.startswith(meta['prefix'].encode())]
        family = pin['family']
        if len(values) != footers[0]['markers'] or not any(b'label=complete' in v for v in values):
            raise ValueError('incomplete public timeline')
        if family in public and values != public[family]:
            raise ValueError('observer changed public behavior')
        public[family] = values
        markers += len(values)
        if meta['mode'] == 'control':
            if any(r.get('event') in ('module', 'gtick', 'validate', 'vis-query') for r in rows):
                raise ValueError('control contains instrumentation')
            controls += 1
            continue
        if meta['mode'] != 'observe':
            raise ValueError('unknown capture mode')
        ends = [r for r in rows if r.get('event') == 'trace-end']
        if len(ends) != 1 or ends[0].get('installed') is not True:
            raise ValueError('observer did not finish')
        native_markers = [r['value'].encode() for r in rows if r.get('event') == 'marker']
        if native_markers != values:
            raise ValueError('observer missed public markers')
        actual = []
        for scene in analyze(rows)['scenes']:
            report = scene_report(scene, rows)
            actual.append({**{k: report[k] for k in COMPARED}, 'hidden_episodes': episodes(scene['timeline'])})
        if actual != frozen['families'][family]:
            raise ValueError('native policy states differ from frozen retail')
        if family == 'loss' and fog_rows(rows) != frozen['fog_owner_rows']:
            raise ValueError('raw fog owner states differ')
        hidden += hidden_policy(rows)
        repeats[family] = repeats.get(family, 0) + 1
        observations += 1
    if observations != 4 or controls != 2 or repeats != {'loss': 2, 'reacquire': 2}:
        raise ValueError('missing policy repeats or controls')
    raw_header = [[int(word, 16) for word in re.findall(r'0x([0-9a-f]{8})u', line)]
                  for line in header.read_text().splitlines() if line.startswith('    {')]
    if raw_header != frozen['fog_owner_rows']:
        raise ValueError('engine fixture differs from unrounded retail words')
    return dict(passed=True, captures=6, controls=controls, observed_repeats=observations,
                scenes=22, public_markers=markers, hidden_visits=hidden, raw_owner_rows=len(raw_header))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--expected', type=Path, required=True)
    ap.add_argument('--archive', type=Path, required=True)
    ap.add_argument('--header', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    try:
        result = dict(status='live-exact-target-visibility', **verify(json.loads(args.expected.read_text()), args.archive, args.header))
    except (ValueError, KeyError, OSError, IndexError) as error:
        result = dict(status='failed', passed=False, error=str(error))
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
