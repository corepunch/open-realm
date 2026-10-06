#!/usr/bin/env python3
"""Verify public final-binding cancellation, roster withdrawal and fresh generations."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary


def digest(value):
    return hashlib.sha256(json.dumps(value, separators=(',', ':')).encode()).hexdigest()


def lifetime_state(rows, operation):
    identities = []; current = {}; last_publication = {}; motion = []; footprints = []
    owners = []
    for r in rows:
        event = r.get('event')
        if event == 'movement-mask-publication' and r['category'] == 202 and r['rawcode'] in (1749240903, 1751543663):
            identity = tuple(r['identity'])
            values = {k: v for k, v in r.items() if k != 'ms'}
            if identity not in identities:
                if r['rawcode'] != (1749240903 if len(identities) % 13 == 0 else 1751543663):
                    raise ValueError('captain lifetime birth order differs')
                identities.append(identity)
                current[r['mover']] = len(identities) - 1
                last_publication[identity] = [values, 1]
            else:
                previous = last_publication[identity]
                if values != previous[0] or previous[1] != 1:
                    raise ValueError('captain lifetime duplicate birth differs')
                previous[1] += 1
        elif event == 'velocity-commit' and r['mover'] in current:
            words = [r['after'][i] for i in (0, 2, 3, 4, 5, 7)]
            if any(type(w) is not int or not 0 <= w <= 0xffffffff for w in words):
                raise ValueError('captain lifetime motion word is not uint32')
            motion.append([current[r['mover']], *words])
        elif event == 'group-footprint-state' and r.get('sharedIdentity'):
            members = r['members']
            if not members or not all(m['resolved'] in current for m in members):
                continue
            if any(m['owner'] != r['identity'] for m in members):
                raise ValueError('captain lifetime shared group has foreign physical member')
            identity = r['sharedIdentity']
            if identity not in owners: owners.append(identity)
            lifetimes = [current[m['resolved']] for m in members]
            if len({i // 13 for i in lifetimes}) != 1:
                raise ValueError('captain lifetime shares retired and fresh movers')
            footprints.append([r['counter'], owners.index(identity),
                               sum(1 << (i % 13) for i in lifetimes), r['sharedRadius'], r['footprint']])
    expected_births = 26 if operation == 'remove' else 13
    if len(identities) != expected_births or any(v[1] != 2 for v in last_publication.values()):
        raise ValueError('captain lifetime birth extent differs')
    if len(owners) < (2 if operation == 'remove' else 1) or len({tuple(v) for v in owners}) != len(owners):
        raise ValueError('captain lifetime requires distinct shared generations')
    publication = [[r['counter'], owners.index(r['identityBefore']), r['before'], r['after'],
                    r['identityBefore'], r['identity']]
                   for r in rows if r.get('event') == 'captain-shared-publish' and r.get('identityBefore') in owners]
    first = [r for r in publication if r[1] == 0]
    if not first or first[-1][2][0] != 0 or first[-1][5] != [-1, -1]:
        raise ValueError('captain lifetime final owner was not collected')
    records = [[r['parent'], r['child'], r['kind'], r['word']]
               for r in rows if r.get('event') == 'metadata-row']
    if len(records) != 546:
        raise ValueError('captain lifetime public record extent differs')
    if any(type(r[3]) is not int or not 0 <= r[3] <= 0xffffffff for r in records):
        raise ValueError('captain lifetime public word is not uint32')
    ticks = (92, 93, 99, 100, 101, 200, 400)
    for index, (parent, child, kind, word) in enumerate(records):
        tick = ticks[index // 78]; unit = (index // 6) % 13
        if parent != tick * 13 + unit or child != index % 6 or kind != ('integer' if child < 3 else 'real') or \
                (child == 0 and word != tick) or (child == 2 and word != ('remove', 'retarget', 'stop').index(operation)):
            raise ValueError('captain lifetime public observation order differs')
    markers = [r['value'] for r in rows if r.get('event') == 'captain-marker']
    if markers.count('PATHCAPTAIN lifetime accepted') != 2 or markers.count('PATHCAPTAIN lifetime roster thirteen') != 2 or \
            'PATHCAPTAIN lifetime retained ' + ('zero' if operation == 'remove' else 'thirteen') not in markers or \
            markers[-1] != 'PATHCAPTAIN lifetime after refill':
        raise ValueError('captain lifetime public recruitment/retention differs')
    roster = [[r['event'], r['counter'], r['counts'], r.get('delta')]
              for r in rows if r.get('event') in ('captain-roster-ranges-begin', 'captain-roster-ranges-end')]
    return dict(births=[list(i) for i in identities], motion=motion, footprints=footprints,
                owners=owners, publication=publication, public_records=records, markers=markers, roster=roster)


def verify_contract(fixture):
    if fixture['whole_retail_pathfinder'] is not False or [v['name'] for v in fixture['variants']] != ['remove', 'retarget', 'stop']:
        raise ValueError('captain lifetime scope differs')
    for v in fixture['variants']:
        state = v['state']; pub = state['publication']; owners = state['owners']
        expected = dict(remove=(7386,591,4),retarget=(4046,12,1),stop=(3549,12,1))[v['name']]
        if (v['engine_commits'],v['shared_footprints'],len(owners)) != expected or \
                len({c['trace_sha256'] for c in v['cases']}) != 2 or len({c['trace'] for c in v['cases']}) != 2:
            raise ValueError('captain lifetime complete extent or independent repeat differs')
        if len(v['cases']) != 2 or len(state['public_records']) != 546 or len(state['motion']) != v['engine_commits'] or \
                len(state['footprints']) != v['shared_footprints'] or len(owners) < (2 if v['name'] == 'remove' else 1) or len({tuple(i) for i in owners}) != len(owners):
            raise ValueError('captain lifetime full extent/repeats differ')
        begins = [r for r in state['roster'] if r[0] == 'captain-roster-ranges-begin']
        ends = [r for r in state['roster'] if r[0] == 'captain-roster-ranges-end']
        expected_delta = [1]*13 + ([-1]*13 + [1]*13 if v['name'] == 'remove' else [])
        expected_counts = list(range(1,14)) + (list(range(12,-1,-1)) + list(range(1,14)) if v['name'] == 'remove' else [])
        if len(begins) != len(expected_delta) or len(ends) != len(expected_delta) or [r[3] for r in begins] != expected_delta or \
                [r[2][1] for r in ends] != expected_counts:
            raise ValueError('captain lifetime signed logical withdrawal/recruitment differs')
        first = [r for r in pub if r[1] == 0]
        if not first or first[-1][0] != 1332 or first[-1][2][0] != 0 or first[-1][5] != [-1,-1] or \
                any(r[2][0] != 2 for r in first[:-1]) or \
                (v['name'] == 'remove' and min(r[0] for r in pub if r[1] == 1) <= first[-1][0]):
            raise ValueError('captain lifetime deferred last-binding collection differs')
        if len(state['births']) != (26 if v['name'] == 'remove' else 13):
            raise ValueError('captain lifetime fresh unit identity extent differs')


def render_header(variants):
    result = '/* Original120 complete public cancellation/refill journeys; lifetime index disambiguates slot reuse. */\n'
    for variant in variants:
        prefix = 'captain_lifetime_' + variant['name']
        for suffix, width, values in [('motion', 7, variant['state']['motion']),
                ('footprints', 4, [[r[0], *r[2:]] for r in variant['state']['footprints']])]:
            result += f'static uint32_t const {prefix}_{suffix}[][{width}]={{\n'
            result += ''.join('    {' + ','.join(str(v) + 'u' for v in row) + '},\n' for row in values) + '};\n\n'
    return result.rstrip() + "\n"


def verify_capture(path, case, variant, engine):
    if hashlib.sha256(path.read_bytes()).hexdigest() != case['trace_sha256'] or path.stat().st_size != case['bytes']:
        raise ValueError('captain lifetime capture hash/extent differs')
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    meta = [r for r in rows if r.get('event') == 'metadata']; end = [r for r in rows if r.get('event') == 'trace-end']
    if len(meta) != 1 or {k: v for k, v in meta[0].items() if k not in ('event', 'pid')} != case['metadata'] or \
            len(end) != 1 or not end[0].get('installed') or any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
        raise ValueError('captain lifetime provenance/completeness differs')
    if len([r for r in rows if r.get('event') == 'metadata-marker' and r['value'] == 'PATHMETA complete']) != 1:
        raise ValueError('captain lifetime public completion missing')
    if end[0]['counts'] != case['footer_counts']:
        raise ValueError('captain lifetime observer totals differ')
    for event, count in case['event_counts'].items():
        if sum(r.get('event') == event for r in rows) != count:
            raise ValueError('captain lifetime observer extent differs: ' + event)
    for event in ('velocity-commit', 'motion-decision', 'arrival-evaluation'):
        if end[0]['counts'][event] != case['event_counts'][event]:
            raise ValueError('captain lifetime numerical observations truncated: ' + event)
    state = lifetime_state(rows, variant['name'])
    if state != variant['state']:
        raise ValueError('captain lifetime original motion/lifecycle differs')
    result = verify_motion(rows, engine, None); result.update(verify_primary(rows, engine, variant))
    result.update(engine_commits=len(state['motion']), shared_footprints=len(state['footprints']))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces', nargs='+', type=Path); p.add_argument('--fixture', required=True, type=Path)
    p.add_argument('--engine-library', required=True, type=Path); p.add_argument('--check-engine-header', required=True, type=Path)
    p.add_argument('--report', required=True, type=Path); a = p.parse_args()
    fixture = json.loads(a.fixture.read_text())
    verify_contract(fixture)
    engine = ctypes.CDLL(str(a.engine_library.resolve())); configure(engine)
    cases = [(v, c) for v in fixture['variants'] for c in v['cases']]
    if len(cases) != 6 or render_header(fixture['variants']) != a.check_engine_header.read_text():
        raise ValueError('captain lifetime repeats/header differ')
    results = [verify_capture(path, case, variant, engine) for path, (variant, case) in zip(a.traces, cases, strict=True)]
    report = dict(passed=True, cases=6, results=results, whole_retail_pathfinder=False, scope=fixture['scope'])
    a.report.write_text(json.dumps(report, indent=2) + '\n'); print(json.dumps(report))


if __name__ == '__main__': main()
