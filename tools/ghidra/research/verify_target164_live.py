#!/usr/bin/env python3
"""Verify archived retail pursuit words and exact denied-route event ordering."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/frida/research'))
from target164_engine_fixture import owner_rows, render


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def markers(path):
    return re.findall(r'call Preload\( "(T021 [^"\r\n]*)" \)', path.read_text(errors='replace'))


def failure_visits(rows):
    visits = {}
    groups = {}
    generation = 0
    for row in rows:
        if row.get('event') == 'group-new':
            groups[row['group']] = generation
            generation += 1
            continue
        if row.get('event') not in ('route', 'stop-members', 'advance', 'layout', 'regroup'):
            continue
        group = groups[row['group']]
        visits.setdefault((group, row['visit']), []).append(row)
    failed = []
    recovered = 0
    for key, events in visits.items():
        route = next((r for r in events if r['event'] == 'route'), None)
        if route is None:
            # The lite observer emits route successes for only60 visits after
            # the last denial; independent layout/regroup hooks remain active.
            if any(r['event'] == 'stop-members' for r in events):
                raise ValueError('missing denied route outcome')
            continue
        if route['result']:
            if key[1] and any(r['event'] == 'route' and not r['result'] for r in visits.get((key[0], key[1]-1), [])):
                advance = [r for r in events if r['event'] == 'advance']
                layouts = [r for r in events if r['event'] == 'layout']
                if len(advance) != 1 or len(layouts) != 1 or advance[0]['resetMembers'] != 0 or advance[0]['resetCounters'] != 1:
                    raise ValueError('recovery did not retain members and refresh layout')
                recovered += 1
            continue
        stops = [r for r in events if r['event'] == 'stop-members']
        if len(stops) != 1 or any(r['event'] in ('advance', 'layout', 'regroup') for r in events):
            raise ValueError('denied visit did not stop exactly once without regroup')
        if any(r[0] != 0 for r in stops[0]['rows']):
            raise ValueError('denied visit retained requested speed')
        path = route['path']
        if path['accCount'] != 0 or path['accIndex'] != -1 or path['accTime'] != 0:
            raise ValueError('denied route state differs')
        # final is deliberately unwritten on result0; never certify its stack word.
        failed.append([*key, route['counter'], route['dest'], route['ready'],
                       {k: v for k, v in path.items() if k != 'path'},
                       [[*r[:3]] for r in stops[0]['rows']]])
    return failed, recovered


def verify(frozen, archive):
    loaded = {}
    timelines = {}
    for name, pin in frozen['captures'].items():
        path = archive / name
        raw = path.read_bytes()
        if len(raw) != pin['bytes'] or digest(raw) != pin['sha256']:
            raise ValueError('capture pin differs: ' + name)
        rows = [json.loads(line) for line in raw.splitlines()]
        meta = {k: v for k, v in rows[0].items() if k != 'pid'}
        if meta != pin['metadata'] or meta.get('owned') is not True or meta.get('sha256') != frozen['binary_sha256']:
            raise ValueError('capture provenance differs')
        if any(r.get('type') == 'error' or r.get('event') in ('error', 'trace-failed') for r in rows):
            raise ValueError('failed observer')
        footer = [r for r in rows if r.get('event') == 'preload-file']
        if len(footer) != 1 or footer[0].get('complete') is not True:
            raise ValueError('incomplete public probe')
        preload = path.with_name(path.stem + '-preload.txt')
        if digest(preload.read_bytes()) != footer[0]['sha256']:
            raise ValueError('public preload pin differs')
        public = markers(preload)
        if len(public) != footer[0]['markers']:
            raise ValueError('public marker count differs')
        if meta['mode'] == 'observe':
            ends = [r for r in rows if r.get('event') == 'trace-end']
            if len(ends) != 1 or ends[0].get('installed') is not True:
                raise ValueError('observer did not finish')
            if [r['value'] for r in rows if r.get('event') == 'marker'] != public:
                raise ValueError('observer marker stream differs')
        elif meta['mode'] != 'control' or any(r.get('event') not in ('metadata', 'loading-key', 'controller-end', 'preload-file') for r in rows):
            raise ValueError('control was instrumented')
        loaded[name], timelines[name] = rows, public
    for left, right in frozen['controls']:
        if timelines[left] != timelines[right]:
            raise ValueError('observer-free timeline differs')
    approaches = [owner_rows(loaded[name]) for name in frozen['approaches']]
    if approaches[0] != approaches[1]:
        raise ValueError('complete approach words differ between repeats')
    header = ROOT / frozen['header']['path']
    if digest(header.read_bytes()) != frozen['header']['sha256'] or header.read_text() != render(archive / 'TARGET-02.1'):
        raise ValueError('engine fixture differs from native owner words')
    failures = [failure_visits(loaded[name]) for name in frozen['failures']]
    if failures[0] != failures[1] or len(failures[0][0]) != 4166 or failures[0][1] != 723:
        raise ValueError('denial/recovery visits differ between repeats')
    return dict(passed=True, captures=len(loaded), approach_owner_rows=len(approaches[0]),
                denied_visits=len(failures[0][0]), recovered_episodes=failures[0][1], controls=len(frozen['controls']))


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--expected', type=Path, required=True)
    ap.add_argument('--archive', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    try:
        report = dict(status='live-exact-target-delay', **verify(json.loads(args.expected.read_text()), args.archive))
    except (ValueError, KeyError, OSError, AssertionError) as error:
        report = dict(status='failed', passed=False, error=str(error))
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    if not report['passed']:
        raise SystemExit(1)
