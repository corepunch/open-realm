#!/usr/bin/env python3
"""Verify the complete stationary singleton captain home journey."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary
from verify_wc3_public_pair_trace import canonical
from verify_wc3_selected_point_trace import digest


def recruit_motion(rows):
    movers = {r['mover'] for r in rows if r.get('event') == 'movement-mask-publication'
              and r['rawcode'] == 1751543663 and r['category'] == 202}
    if len(movers) != 1:
        raise ValueError('captain recruit identity/profile differs')
    mover = movers.pop()
    return [[0, *[r['after'][i] for i in (0, 2, 3, 4, 5, 7)]]
            for r in rows if r.get('event') == 'velocity-commit' and r['mover'] == mover]


def producer(rows):
    return dict(markers=[r['value'] for r in rows if r.get('event') == 'captain-marker'],
        calls=[r['name'] for r in rows if r.get('event') == 'captain-native-begin'],
        recruits=[r['words'] for r in rows if r.get('event') == 'captain-native-begin' and r['name'] == 'add'],
        ranges=[[r['value'], r['callerRva']] for r in rows if r.get('event') == 'arrival-range'],
        prepared=[{k: r[k] for k in ('point', 'index', 'count', 'policy', 'bindShared')}
                  for r in rows if r.get('event') == 'captain-prepare-begin'],
        tasks=[{k: r[k] for k in ('destination', 'range', 'eventCode')}
               for r in rows if r.get('event') == 'point-task'],
        samples=[r['value'] for r in rows if r.get('event') == 'marker'])


def verify_producer(spec):
    p = spec['producer']
    expected = ['PATHCAPTAIN home begin', 'PATHCAPTAIN home before recruit', 'PATHCAPTAIN home accepted']
    if spec['name'] == 'init':
        expected += ['PATHCAPTAIN roster before init one', 'PATHCAPTAIN roster after init one']
    recruits = [[1, 1751543663]]
    if spec['name'] == 'full':
        expected = ['PATHCAPTAIN home begin', 'PATHCAPTAIN full create false', 'PATHCAPTAIN full init true',
                    'PATHCAPTAIN home before recruit', 'PATHCAPTAIN home accepted',
                    'PATHCAPTAIN full recruited true', 'PATHCAPTAIN shortage rejected',
                    'PATHCAPTAIN full shortage false', 'PATHCAPTAIN full retry retained true']
        recruits.append([2, 1751543663])
    if p['markers'] != expected or p['recruits'] != recruits:
        raise ValueError('captain home public recruitment/retention differs')
    calls = ['start-wrapper', 'load', 'compile', 'create', 'init', 'add']
    if spec['name'] == 'init': calls.append('init')
    if spec['name'] == 'full': calls += ['add', 'init']
    if p['calls'] != calls:
        raise ValueError('captain native admission order differs')
    if p['prepared'] != [dict(point=[3304194048, 3272605696], index=0, count=0, policy=1, bindShared=1)]:
        raise ValueError('private shared point handoff differs')
    if p['tasks'] != [dict(destination=[-1936, -144], range=0, eventCode=852331)]:
        raise ValueError('captain private point task differs')
    if p['ranges'] != [[1098514432, 375648], [1083899904, 370548],
                       [1086849024, 375648], [1056629064, 377958]]:
        raise ValueError('virtual captain/physical recruit range families differ')
    if len(p['samples']) != 304 or not p['samples'][-1].endswith('order=0'):
        raise ValueError('captain home travel did not naturally complete')


def render_header(fixture):
    return ('/* Literal original stationary captain-home recruit movement; virtual actors excluded. */\n'
            f"enum {{ CAPTAIN_HOME_ADMISSION_COMMITS={fixture['engine_admission_commits']}, CAPTAIN_HOME_HANDOFF_MSEC={fixture['engine_admission_end_msec']} }};\n"
            'static uint32_t const captain_home_motion[][7]={\n' +
            ''.join('    {' + ','.join(str(v)+'u' for v in row) + '},\n'
                    for row in fixture['motion']) + '};\n')


def verify_capture(rows, fixture, case, engine):
    spec = fixture['journeys'][case['journey']]
    verify_producer(spec)
    metadata = [r for r in rows if r.get('event') == 'metadata']
    ends = [r for r in rows if r.get('event') == 'trace-end']
    if len(metadata) != 1 or {k: v for k, v in metadata[0].items() if k not in ('event', 'pid')} != case['metadata']:
        raise ValueError('captain capture provenance differs')
    if len(ends) != 1 or not ends[0].get('installed') or any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
        raise ValueError('captain capture failed/incomplete')
    if producer(rows) != spec['producer'] or recruit_motion(rows) != fixture['motion']:
        raise ValueError('captain literal admission/whole recruit motion differs')
    if digest(canonical(rows)) != fixture['phases_sha256']:
        raise ValueError('captain whole physical/virtual phase words differ')
    if len(fixture['motion']) != 178 or fixture['engine_admission_commits'] != 178:
        raise ValueError('captain reference and engine admission extents differ')
    result = verify_motion(rows, engine, None)
    result.update(verify_primary(rows, engine, fixture))
    result.update(recruit_commits=178, engine_admission_commits=178,
                  whole_engine_parity=True, private_handoff_remains_open=False)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces', nargs='+', type=Path)
    p.add_argument('--fixture', required=True, type=Path)
    p.add_argument('--engine-library', required=True, type=Path)
    p.add_argument('--check-engine-header', type=Path)
    p.add_argument('--report', required=True, type=Path)
    a = p.parse_args()
    fixture = json.loads(a.fixture.read_text())
    engine = ctypes.CDLL(str(a.engine_library.resolve())); configure(engine)
    results = []
    for path, case in zip(a.traces, fixture['cases'], strict=True):
        result = verify_capture([json.loads(l) for l in path.read_text().splitlines()], fixture, case, engine)
        result['trace_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
        results.append(result)
    if a.check_engine_header and a.check_engine_header.read_text() != render_header(fixture):
        raise ValueError('captain C reference differs')
    report = dict(passed=True, cases=len(results), results=results, scope=fixture['scope'],
                  whole_engine_parity=True, private_handoff_remains_open=False)
    for key in ('exact_velocity_commits', 'exact_decisions', 'owner_callbacks', 'recruit_commits', 'engine_admission_commits'):
        report[key] = sum(r[key] for r in results)
    a.report.write_text(json.dumps(report, indent=2)+'\n'); print(json.dumps(report, indent=2))


if __name__ == '__main__': main()
