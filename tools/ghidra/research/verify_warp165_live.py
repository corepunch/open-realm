#!/usr/bin/env python3
"""Strictly verify archived FORM-04.2 warp lifetimes and classification."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/frida/research'))
from form042_analyze import analyze
from form042_expected import strip


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def transitions(rows):
    current, visits, warped_paths, owners = {}, {}, {}, {}
    warps = retained_commits = 0
    for row in rows:
        event = row.get('event')
        if event == 'tick':
            key = (row['group'], *row['identity'])
            current[row['group']] = key
            visits.setdefault(key, []).append([])
            for member in row['members']:
                if 'path' in member:
                    owners[member['path']] = key
        elif event in ('layout', 'advance', 'regroup', 'commit'):
            key = current[row['group']]
            visits[key][-1].append(event)
            if event == 'commit' and row['path'] in warped_paths:
                # Restrict to the original physical owner. A later order can
                # request a new marker-free route through this same path.
                if key == warped_paths[row['path']]:
                    if not int(row['mflags'], 16) & 0x80000:
                        raise ValueError('warp cleared the member marker')
                    retained_commits += 1
        elif event == 'warp':
            before, after = row['before'], row['after']
            if row['result'] != 1 or row['caller'] != '165c57':
                raise ValueError('unexpected portal consumer')
            if not int(before['flags'], 16) & 0x1000000:
                raise ValueError('portal route has no marker')
            for field in ('path', 'fineCount', 'accCount', 'flags', 'dest', 'adjusted'):
                if before[field] != after[field]:
                    raise ValueError('warp replaced retained route state')
            if after['fineIndex'] != -1 or after['accIndex'] != 0:
                raise ValueError('warp did not invalidate only the indices')
            key = owners.get(before['path'])
            if key is None:
                raise ValueError('warp lacks a physical member owner')
            warped_paths[before['path']] = key
            visits[key][-1].append('warp')
            warps += 1
    for values in visits.values():
        for events in values:
            if 'warp' in events and any(e in events for e in ('layout', 'advance', 'regroup')):
                raise ValueError('warp rebuilt the physical formation')
    if warps != 4 or not retained_commits:
        raise ValueError('incomplete gate journey')
    return retained_commits


def verify(frozen, archive):
    observed = controls = retained = 0
    normalized = []
    for name, pin in frozen['captures'].items():
        path = archive / name
        raw = path.read_bytes()
        if len(raw) != pin['bytes'] or digest(raw) != pin['sha256']:
            raise ValueError('capture pin differs')
        rows = [json.loads(line) for line in raw.splitlines()]
        meta = rows[0]
        if meta != pin['metadata'] or meta.get('owned') is not True or meta.get('sha256') != frozen['binary_sha256']:
            raise ValueError('capture provenance differs')
        preload = path.with_name(path.stem + '-preload.txt').read_bytes()
        footer = [r for r in rows if r.get('event') == 'preload-file']
        if len(footer) != 1 or not footer[0]['complete'] or digest(preload) != pin['preload_sha256'] or footer[0]['sha256'] != pin['preload_sha256']:
            raise ValueError('public probe did not complete')
        markers = re.findall(rb'call Preload\( "(F05 [^"\r\n]*)" \)', preload)
        if len(markers) != footer[0]['markers'] or not any(b'tick=500 label=complete variant=d' in m for m in markers):
            raise ValueError('incomplete public marker timeline')
        if meta['mode'] == 'observe':
            ends = [r for r in rows if r.get('event') == 'trace-end']
            if len(ends) != 1 or ends[0].get('installed') is not True:
                raise ValueError('observer did not finish')
            actual = strip(analyze(path))
            if actual != pin['normalized']:
                raise ValueError('frozen native transitions differ')
            first = actual['groups'][0]
            if [s[0] for s in first['cooldown_seeds']] != [3, 69, 135]:
                raise ValueError('marker classification cadence differs')
            if len(first['layouts']) != 1 or first['resets'] or len(first['advances']) != 1:
                raise ValueError('portal unexpectedly regrouped')
            if first['bit80000'] != {'4': [[2, 116]], '2': [[3, 176]], '3': [[3, 188]], '5': [[3, 93]]}:
                raise ValueError('member marker lifetime differs')
            retained += transitions(rows)
            normalized.append(actual)
            observed += 1
        elif meta['mode'] == 'control':
            if any(r.get('event') in ('module', 'tick', 'warp', 'commit') for r in rows):
                raise ValueError('control contains instrumentation')
            controls += 1
        else:
            raise ValueError('unknown capture mode')
    if observed != 3 or controls != 1:
        raise ValueError('missing repeats or public control')
    for actual in normalized[1:]:
        if actual['groups'][0] != normalized[0]['groups'][0] or actual['warps'] != normalized[0]['warps']:
            raise ValueError('first gate journey did not repeat exactly')
    return dict(passed=True, captures=4, controls=controls, observed_repeats=observed,
                warp_transitions=12, retained_marker_commits=retained)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--expected', type=Path, required=True)
    ap.add_argument('--archive', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    try:
        result = dict(status='live-exact-warp-markers', **verify(json.loads(args.expected.read_text()), args.archive))
    except (ValueError, KeyError, OSError, StopIteration) as error:
        result = dict(status='failed', passed=False, error=str(error))
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
