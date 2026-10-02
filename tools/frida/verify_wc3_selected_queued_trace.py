#!/usr/bin/env python3
"""Verify original Shift admission, staggered cohort acquisition and exact motion."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_public_pair_trace import canonical
from verify_wc3_selected_point_trace import digest, producer

REQUEST_EVENTS = {'move-request-created', 'move-request-candidate',
                  'move-request-bind-candidate', 'move-request-activate',
                  'move-group-bind', 'player-order-queued',
                  'move-previous-cohort-search', 'move-previous-cohort-candidate'}


def motion_words(rows):
    return [[r['member'], *[r['after'][i] for i in (0, 2, 3, 4, 5, 7)]]
            for r in canonical(rows) if r['event'] == 'velocity-commit']


def request_rows(rows):
    return [{k: v for k, v in r.items() if k != 'ms'}
            for r in rows if r.get('event') in REQUEST_EVENTS]


def owner_order(rows):
    groups = []
    result = []
    for r in rows:
        if r.get('event') not in ('pair-group-phase-begin', 'pair-group-phase-end'):
            continue
        key = (r['group'], tuple(r['identity']))
        if key not in groups:
            groups.append(key)
        result.append([groups.index(key), r['event'], r['phase']])
    return result


def verify_neighbors(rows, point):
    searches = [r for r in rows if r.get('event') == 'move-previous-cohort-search']
    if (len(searches) != 2 or [r['result'] for r in searches] != [0, 1] or
            searches[0]['output'] != '0x0' or searches[1]['output'] == '0x0' or
            searches[0]['before']['previous'] != searches[1]['before']['previous'] or
            searches[0]['before']['category'] != searches[1]['before']['category'] or
            any(r['point'] != point for r in searches)):
        raise ValueError('staggered previous-request cohort acquisition differs')
    candidates = [r for r in rows if r.get('event') == 'move-previous-cohort-candidate']
    accepted = [r for r in candidates if r['accepted']]
    if (len(candidates) != 4 or len(accepted) != 1 or accepted[0]['result'] != 0 or
            accepted[0]['candidate']['unit'] == accepted[0]['source']['unit'] or
            accepted[0]['candidate']['previous'] != accepted[0]['source']['previous'] or
            accepted[0]['request'] != searches[1]['output']):
        raise ValueError('queued cohort neighbor match differs')
    return searches


def verify_lifecycle(rows, case, fixture):
    metadata = [r for r in rows if r.get('event') == 'metadata']
    ends = [r for r in rows if r.get('event') == 'trace-end']
    if len(metadata) != 1 or {k: v for k, v in metadata[0].items()
                             if k not in ('event', 'pid')} != case['metadata']:
        raise ValueError('queued point provenance differs')
    if (len(ends) != 1 or not ends[0].get('installed') or
            any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows)):
        raise ValueError('queued point observer incomplete/failed')
    if not metadata[0]['pointInput'].get('shift'):
        raise ValueError('queued point input omitted Shift')
    actions = [r for r in rows if r.get('event') == 'player-point-action-begin']
    if len(actions) != 1 or (actions[0]['entry'], actions[0]['player'],
                            actions[0]['flags'], actions[0]['order']) != (0x6b9f70, 3, 9, 851986):
        raise ValueError('queued point packet is not selected ground Shift Move')
    helpers = [r for r in rows if r.get('event') == 'player-input-helper']
    clicks = [r for r in rows if r.get('event') == 'player-move-click']
    if (len(helpers) != 1 or 'down/up accepted' not in helpers[0]['output'] or
            helpers[0]['sha256'] != case['metadata']['source_sha256']['wc3-ui-input.exe'] or
            len(clicks) != 1 or not clicks[0].get('shift') or
            clicks[0]['pixel'] != case['metadata']['pointInput']['pixel']):
        raise ValueError('owned native Shift input witness differs')
    for event in ('clock-source-begin', 'clock-source-end', 'clock-advance-begin',
                  'clock-advance-end', 'clock-owner-begin', 'clock-owner-end'):
        count = sum(r.get('event') == event for r in rows)
        if not count or count != ends[0]['counts'].get(event):
            raise ValueError('queued clock observer count differs: ' + event)
    if producer(rows) != case['producer']:
        raise ValueError('queued point attach/admit/publication differs')
    publications = [r for r in rows if r.get('event') == 'player-order-publish']
    if len(publications) != 2 or any(r['flags'] != 9 or r['fallback'] for r in publications):
        raise ValueError('queued point publication policy differs')
    for r in rows:
        if r.get('event') == 'player-order-queued' and r['countBefore'] == 1:
            if r['countAfter'] != 2 or r['before'] != r['after']:
                raise ValueError('Shift replaced the current order instead of appending')
    verify_neighbors(rows, actions[0]['point'])
    if digest(request_rows(rows)) != case['request_sha256']:
        raise ValueError('queued request/member/ready/activation words differ')
    phases = canonical(rows)
    if digest(phases) != case['phases_sha256'] or owner_order(rows) != fixture['owner_order']:
        raise ValueError('queued cohort owner order or motion phases differ')
    words = motion_words(rows)
    if words != fixture['engine_motion']:
        raise ValueError('queued point motion words differ')
    for event, expected in {'motion-decision': 506, 'velocity-commit': 510,
                            'arrival-evaluation': 510, 'task-arrival': 4}.items():
        if sum(r.get('event') == event for r in rows) != expected or ends[0]['counts'].get(event) != expected:
            raise ValueError('queued observer count differs: ' + event)
    arrivals = [r for r in rows if r.get('event') == 'task-arrival']
    if any(r['after']['orderHead'] != [-1, -1] for r in arrivals[-2:]):
        raise ValueError('queued point final user orders did not finish naturally')
    return dict(passed=True, queued_selected_members=2, natural_arrivals=4,
                group_owner_passes=261, request_cohorts=3,
                motion_sha256=digest(words))


def render_header(fixture):
    out = '/* Original 1.27 scene50: supplied input timing; normal engine frames own all motion. */\n'
    out += 'static uint32_t const selected_queued_inputs[2][4]={\n'
    for case in fixture['cases']:
        r = next(r for r in case['producer'] if r['event'] == 'player-point-action-begin')
        out += '    {' + ','.join(str(v) + 'u' for v in [r['clock'][0], r['counter'], *r['point']]) + '},\n'
    out += '};\nstatic uint32_t const selected_queued_motion[510][7]={\n'
    for row in fixture['engine_motion']:
        out += '    {' + ','.join(str(v) + 'u' for v in row) + '},\n'
    return out + '};\n'


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
    engine = ctypes.CDLL(str(args.engine_library.resolve()))
    configure(engine)
    results = []
    for path, case in zip((args.trace, args.repeat), fixture['cases'], strict=True):
        rows = [json.loads(s) for s in path.read_text().splitlines()]
        result = verify_lifecycle(rows, case, fixture)
        result.update(verify_motion(rows, engine, None))
        results.append(result)
    if results[0]['motion_sha256'] != results[1]['motion_sha256']:
        raise ValueError('queued motion does not repeat exactly')
    if args.check_engine_header and args.check_engine_header.read_text() != render_header(fixture):
        raise ValueError('queued point engine header differs')
    report = dict(passed=True, repeated=True, queued_selected_members=2, natural_arrivals=8,
                  request_cohorts=6, group_owner_passes=522,
                  exact_decisions=sum(r['exact_decisions'] for r in results),
                  exact_velocity_commits=sum(r['exact_velocity_commits'] for r in results),
                  motion_sha256=results[0]['motion_sha256'], scope=fixture['scope'])
    for key, path in [('trace', args.trace), ('repeat', args.repeat), ('fixture', args.fixture),
                      ('engine_library', args.engine_library)]:
        report[key + '_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
