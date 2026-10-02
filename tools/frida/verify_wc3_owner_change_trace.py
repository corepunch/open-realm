#!/usr/bin/env python3
"""Verify a repeated public ordinary Move cancellation through SetUnitOwner."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion

EVENTS = {'mover-stop', 'scheduler-class', 'motion-decision', 'velocity-commit', 'route-step', 'marker'}


def canonical(rows):
    return [{k: v for k, v in row.items() if k not in ('ms', 'mover', 'path', 'fineObject')}
            for row in rows if row.get('event') in EVENTS]


def verify_lifecycle(rows, fixture):
    metadata = [r for r in rows if r.get('event') == 'metadata']
    ending = [r for r in rows if r.get('event') == 'trace-end']
    if len(metadata) != 1 or {k: v for k, v in metadata[0].items() if k not in ('event', 'pid')} != fixture['metadata']:
        raise ValueError('owner change provenance differs')
    if len(ending) != 1 or not ending[0].get('installed') or any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
        raise ValueError('owner change observer failed/incomplete')
    for event in EVENTS - {'marker'}:
        if sum(r.get('event') == event for r in rows) != ending[0].get('counts', {}).get(event):
            raise ValueError('owner change observer count differs: ' + event)
    actual = canonical(rows)
    if actual != fixture['lifecycle']:
        raise ValueError('owner change lifecycle/raw words differ')
    digest = hashlib.sha256(json.dumps(actual, separators=(',', ':')).encode()).hexdigest()
    if digest != fixture['lifecycle_sha256']:
        raise ValueError('owner change frozen lifecycle digest differs')
    commits = [[r['after'][i] for i in (0, 2, 3, 4, 5, 7)] for r in rows if r.get('event') == 'velocity-commit']
    stops = [r for r in rows if r.get('event') == 'mover-stop' and '0x698d92' in r['stack']]
    if commits != fixture['motion'] or len(stops) != 2 or stops[-1]['after'] != fixture['final_pose'] or stops[-1]['requestedAfter'] != fixture['final_requested']:
        raise ValueError('owner change engine words differ')
    return dict(passed=True, lifecycle_records=len(actual), lifecycle_sha256=digest,
                ownership_stops=2, public_velocity_commits=len(commits), scope=fixture['scope'])


def render_header(fixture):
    result = '/* Frozen original owner_change public motion; see retail-owner-change-1.27.json. */\nstatic uint32_t const owner_change_motion[17][6] = {\n'
    for row in fixture['motion']:
        result += '    {' + ', '.join('0x%08xu' % w for w in row) + '},\n'
    return result + '};\nstatic uint32_t const owner_change_final[8] = {' + ', '.join('0x%08xu' % w for w in fixture['final_pose']) + '};\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--repeat', required=True, type=Path)
    parser.add_argument('--fixture', required=True, type=Path)
    parser.add_argument('--engine-library', required=True, type=Path)
    parser.add_argument('--check-engine-header', type=Path)
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    fixture = json.loads(args.fixture.read_text())
    engine = ctypes.CDLL(str(args.engine_library.resolve())); configure(engine)
    read = lambda path: [json.loads(line) for line in path.read_text().splitlines()]
    results = []
    for path in (args.trace, args.repeat):
        rows = read(path)
        result = verify_lifecycle(rows, fixture)
        result.update(verify_motion(rows, engine, None))
        # Every clock observer must finish; the fixed clock corpus owns numerical cadence parity.
        ending = next(r for r in rows if r.get('event') == 'trace-end')
        for event in ('clock-source-begin', 'clock-source-end', 'clock-advance-begin', 'clock-advance-end', 'clock-owner-begin', 'clock-owner-end'):
            if not sum(r.get('event') == event for r in rows) or sum(r.get('event') == event for r in rows) != ending['counts'].get(event):
                raise ValueError('owner change clock observer incomplete: ' + event)
        results.append(result)
    if results[0] != results[1]:
        raise ValueError('owner change repeat differs')
    if args.check_engine_header and args.check_engine_header.read_text() != render_header(fixture):
        raise ValueError('owner change engine header differs')
    result = dict(results[0], repeated=True, scope=fixture['scope'])
    for key, path in [('trace', args.trace), ('repeat', args.repeat), ('fixture', args.fixture), ('engine_library', args.engine_library)]:
        result[key + '_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    args.report.write_text(json.dumps(result, indent=2) + '\n'); print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
