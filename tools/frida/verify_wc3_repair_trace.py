#!/usr/bin/env python3
"""Verify Repair-family public heads and complete owned native Shift captures."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re

from verify_wc3_metadata_trace import normalize

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
IDS = [852024, 852024, 852202, 852161]


def digest(value):
    return hashlib.sha256(json.dumps(value, separators=(',', ':')).encode()).hexdigest()


def records(rows, width):
    exported = [r for r in rows if r.get('event') == 'metadata-row']
    if len(exported) % width:
        raise ValueError('partial Repair record')
    count = len(exported) // width
    if [(r['parent'], r['child']) for r in exported] != [(i, j) for i in range(count) for j in range(width)]:
        raise ValueError('Repair export order/extent differs')
    expected = ['integer'] * 4 + ['real'] * 3 if width == 7 else ['integer'] * 3 + ['real'] * 3
    if any(r['kind'] != expected[r['child']] or type(r['word']) is not int or
           not 0 <= r['word'] <= 0xffffffff for r in exported):
        raise ValueError('Repair export type/word differs')
    values = [[r['word'] for r in exported[i:i + width]] for i in range(0, len(exported), width)]
    labels = []
    for r in rows:
        if r.get('event') != 'metadata-marker' or ' row=' not in r.get('value', ''):
            continue
        m = re.fullmatch(r'PATHMETA (repair|repair_queue) row=(\d+) tick=(\d+) worker=(\d+) label=(\w+)(?: accepted=(\d+) order=(\d+))?', r['value'])
        if not m or int(m[2]) != len(labels):
            raise ValueError('Repair label/record order differs')
        index = int(m[2])
        if index >= count or values[index][:2] != [int(m[3]), int(m[4])]:
            raise ValueError('Repair label tick/worker differs')
        if width == 7 and values[index][2:4] != [int(m[7]), int(m[6])]:
            raise ValueError('Repair public getter/acceptance differs')
        labels.append(m[5])
    if len(labels) != count:
        raise ValueError('missing Repair record label')
    for i in range(4):
        if [v[0] for v, label in zip(values, labels) if v[1] == i and label == 'sample'] != list(range(1, 301)):
            raise ValueError('incomplete Repair worker journey')
    return values, labels


def base_contract(rows):
    values, labels = records(rows, 7)
    if len(values) != 1316:
        raise ValueError('Repair action matrix extent differs')
    actions = {(v[0], v[1], label): v for v, label in zip(values, labels) if label != 'sample'}
    family = ((1, 'repair', 1), (2, 'same_target', 1), (3, 'invalid', 0),
              (6, 'same_target', 1), (16, 'repeat_on', 0), (26, 'one_hp', 1),
              (30, 'repair', 1), (34, 'full', 1), (40, 'repair', 1),
              (44, 'remove_target', 1), (50, 'fresh_target', 1), (70, 'repair', 1),
              (130, 'work_smart', 1), (131, 'work_explicit', 1), (132, 'work_smart_repeat', 1))
    uniform = ((0, 'initial_off', 0, 0), (4, 'move', 851986, 1),
               (5, 'smart', 851971, 1), (7, 'same_smart', 851971, 1),
               (10, 'full', 851971, 1), (12, 'autoon', 0, 1),
               (20, 'autooff', 0, 1), (21, 'repeat_off', 0, 0),
               (22, 'stop', 0, 1), (25, 'move', 851986, 1),
               (55, 'death', 0, 1), (60, 'autooff', 0, 0), (65, 'fresh_worker', 0, 0))
    for i, order in enumerate(IDS):
        for tick, label, accepted in family:
            if actions[(tick, i, label)][2:4] != [order, accepted]:
                raise ValueError('Repair family/work/interruption contract differs')
        for tick, label, head, accepted in uniform:
            if actions[(tick, i, label)][2:4] != [head, accepted]:
                raise ValueError('Repair Smart/toggle/reuse contract differs')
        if actions[(27, i, 'subunit_hp')][2:4] != [0 if i == 3 else order, 0]:
            raise ValueError('Repair subunit-life rejection differs')
        if next(v for v, label in zip(values, labels) if v[:2] == [300, i] and label == 'sample')[2] != 0:
            raise ValueError('Repair natural completion missing')
    motion = sum(r.get('event') == 'velocity-commit' for r in rows)
    if motion != 566:
        raise ValueError('Repair approach observation extent differs')
    return dict(records=len(values), motion_commits=motion, normalized_sha256=digest(normalize(rows)))


def queue_contract(rows):
    values, labels = records(rows, 6)
    clicks = [r for r in rows if r.get('event') == 'player-move-click']
    helpers = [r for r in rows if r.get('event') == 'player-input-helper']
    if len(clicks) != 4 or len(helpers) != 4 or any(r['key'] != 'r' or r['shift'] is not True or
        r['button'] != 1 or r['alt'] is not False for r in clicks):
        raise ValueError('missing native Shift Repair input')
    if any('down/up accepted' not in r['output'] for r in helpers):
        raise ValueError('native Repair input failed')
    for i, order in enumerate(IDS):
        samples = [v for v, label in zip(values, labels) if v[1] == i and label == 'sample']
        changes = []
        for v in samples:
            if not changes or changes[-1][1] != v[2]:
                changes.append([v[0], v[2]])
        expected = [851986, order, 0] if i == 0 else [0, 851986, order, 0]
        if [v[1] for v in changes] != expected:
            raise ValueError('native pending Repair activation/completion differs')
        if next(v for v in samples if v[0] == 10 + i * 70)[2] != 851986:
            raise ValueError('Shift Repair replaced the active Move head')
    return dict(records=len(values), native_inputs=4, normalized_sha256=digest(normalize(rows)))


def verify(raw, fixture, cap):
    if hashlib.sha256(raw).hexdigest() != cap['sha256'] or len(raw) != cap['bytes']:
        raise ValueError('Repair capture hash/length differs')
    rows = [json.loads(s) for s in raw.splitlines()]
    metadata = [r for r in rows if r.get('event') == 'metadata']
    ends = [r for r in rows if r.get('event') == 'trace-end']
    if len(metadata) != 1 or len(ends) != 1 or rows[-1] != ends[0] or not ends[0].get('installed') or any(
            r.get('event') == 'trace-failed' or r.get('type') == 'error' for r in rows):
        raise ValueError('incomplete/error Repair capture')
    if {k: v for k, v in metadata[0].items() if k not in ('event', 'pid')} != cap['metadata']:
        raise ValueError('Repair producer provenance differs')
    if metadata[0]['sha256'] != HASH or metadata[0]['owned'] is not True:
        raise ValueError('Repair requires the owned mapped retail build')
    if sum(r.get('event') == 'metadata-marker' and r['value'] == 'PATHMETA complete' for r in rows) != 1:
        raise ValueError('Repair completion marker missing')
    ticks = [int(r['value'].split()[1][5:]) for r in rows if r.get('event') == 'marker']
    if ticks != list(range(301)):
        raise ValueError('Repair simulation sample extent differs')
    result = base_contract(rows) if cap['kind'] == 'base' else queue_contract(rows)
    if result != cap['result']:
        raise ValueError('frozen Repair record/admission/motion words differ')
    if fixture['whole_retail_pathfinder'] is not False or fixture['repair_rate_parity'] is not False:
        raise ValueError('Repair ownership witness overclaims geometry/numerics')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fixture', type=Path, required=True)
    p.add_argument('--capture', type=Path, action='append')
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    fixture = json.loads(args.fixture.read_text())
    if fixture.get('version') != 1 or [cap['kind'] for cap in fixture['captures']] != ['base', 'base', 'queue', 'queue']:
        p.error('requires two complete base and two native queue captures')
    if fixture['captures'][0]['result'] != fixture['captures'][1]['result']:
        p.error('base ownership/admission/motion repeat differs')
    root = Path(__file__).resolve().parents[2]
    paths = args.capture or [root / cap['archive'] for cap in fixture['captures']]
    if len(paths) != len(fixture['captures']):
        p.error('capture count differs from the frozen matrix')
    results = []
    for path, cap in zip(paths, fixture['captures']):
        raw = path.read_bytes()
        if path.suffix == '.gz':
            raw = gzip.decompress(raw)
        results.append(verify(raw, fixture, cap))
    report = dict(passed=True, captures=len(results), records=sum(r['records'] for r in results),
                  native_inputs=sum(r.get('native_inputs', 0) for r in results),
                  scope=fixture['scope'])
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
