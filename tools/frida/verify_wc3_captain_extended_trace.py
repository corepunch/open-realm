#!/usr/bin/env python3
"""Verify extended captain travel and the visible, standing private Follow owner."""
import argparse
import ctypes
import hashlib
import json
import re
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_captain_approach_trace import births
from verify_wc3_captain_cancel_trace import digest
from verify_wc3_captain_go_home_trace import continuation
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary


def group_states(rows):
    movers = births(rows)
    actor = next(r['mover'] for r in rows if r.get('event') == 'movement-mask-publication' and r['rawcode'] == 0)
    movers.append(actor)
    result = []
    for r in rows:
        if r.get('event') not in ('pair-group-phase-begin', 'pair-group-phase-end'):
            continue
        members = []
        for m in r['members']:
            if m['mover'] not in movers or m['row'][5] != int(m['mover'], 16):
                raise ValueError('physical member pointer or actor identity differs')
            role = movers.index(m['mover'])
            members.append([role, *m['row'][:5], role, *m['row'][6:], m['pose'], m['moverFlags']])
        result.append([r['event'], r['phase'], r['identity'], r['flags'], r['age'],
                       r['completion'], r['formation'], members])
    return result


def standing_owner(rows):
    movers = births(rows)
    last = next(r for r in reversed(rows) if r.get('event') == 'pair-group-phase-end' and r['phase'] == 'commit')
    if len(last['members']) != 1:
        raise ValueError('terminal private Follow is not a singleton')
    member = last['members'][0]
    role = movers.index(member['mover'])
    gate = next(r for r in reversed(rows) if r.get('event') == 'group-completion' and r['group'] == last['group'])
    visible = next(r for r in reversed(rows) if r.get('event') == 'target-visibility' and r['group'] == last['group'])
    target = next(r for r in reversed(rows) if r.get('event') == 'group-target' and r['group'] == last['group'])
    actor = next(r for r in rows if r.get('event') == 'movement-mask-publication' and r['rawcode'] == 0)
    if not last['flags'] & 1 or last['completion'] != 0 or member['row'][10] != 0x10000 or \
            member['pose'][4:6] != [0, 0] or gate['gateOpen'] is not False or gate['missed'] != 0 or \
            visible['missed'] != 0 or visible['blocked'] != 0 or target['target'] != actor['mover']:
        raise ValueError('visible arrived Follow completion gate differs')
    return dict(role=role, group=last['identity'], flags=last['flags'], member_flags=member['row'][10],
                completion=last['completion'], missed=gate['missed'], target=actor['identity'],
                position=member['pose'][2:4], velocity=member['pose'][4:6], heading=member['pose'][7])


def render_header(c):
    return ('/* Original occupied-home captain journey through120 seconds.\n'
            ' * An arrived private Follow owner remains standing; no forced retirement. */\n'
            'static uint32_t const captain_go_home_complete_motion[][7]={\n' +
            ''.join('    {' + ','.join(str(v) + 'u' for v in row) + '},\n' for row in c['motion']) +
            '};\n\nstatic uint32_t const captain_go_home_complete_footprints[][4]={\n' +
            ''.join('    {' + ','.join(str(v) + 'u' for v in row) + '},\n' for row in c['footprints']) + '};\n')


def verify_contract(f):
    if f['whole_retail_pathfinder'] is not False or f['complete_translation_journey'] is not True or \
            f['natural_task_reclamation'] is not False or f['persistent_follow_remains'] is not True:
        raise ValueError('standing Follow must not be claimed as natural task reclamation')
    if [f[k] for k in ('engine_end_msec', 'engine_commits', 'virtual_commits', 'shared_footprints',
                       'primary_advances', 'owner_callbacks')] != [120050, 10986, 327, 865, 24010, 4000] or len(f['cases']) != 2:
        raise ValueError('extended observation extent differs')
    expected = dict(role=8, group=[1771, 2471], flags=137217, member_flags=0x10000,
                    completion=0, missed=0, target=[1510, 1657], position=[1126530908, 1113911057],
                    velocity=[0, 0], heading=1086225756)
    if f['standing_owner'] != expected:
        raise ValueError('terminal standing owner contract differs')
    for case in f['cases']:
        meta = case['metadata']
        if meta['owned'] is not True or meta['samples'] <= case['velocity_commits'] or \
                meta['source_sha256']['map'] != f['map_sha256'] or not all(meta[k] for k in
                ('taskEvents', 'motionEvents', 'velocityEvents', 'clockEvents', 'captainMembershipEvents')):
            raise ValueError('extended capture is capped, unowned or missing observers')


def verify_capture(rows, f, case, engine):
    meta = [r for r in rows if r.get('event') == 'metadata']
    end = [r for r in rows if r.get('event') == 'trace-end']
    if len(meta) != 1 or {k: v for k, v in meta[0].items() if k not in ('event', 'pid')} != case['metadata'] or \
            len(end) != 1 or not end[0].get('installed') or any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
        raise ValueError('extended capture provenance or completion differs')
    for event, expected in [('marker', 1204), ('clock-owner-end', 4000), ('velocity-commit', case['velocity_commits'])]:
        count = sum(r.get('event') == event for r in rows)
        if count != expected or (event in end[0]['counts'] and end[0]['counts'][event] != count):
            raise ValueError('extended capture observer extent differs: ' + event)
    samples = [re.match(r'PATHTRACE tick=(\d+) label=sample ', r['value']) for r in rows if r.get('event') == 'marker']
    if [int(m[1]) for m in samples if m] != list(range(1, 1201)):
        raise ValueError('extended authored sample sequence differs')
    c = continuation(rows, 0x42f00000, 5024)
    if len(c['motion']) != f['engine_commits'] or sum(r[0] == 13 for r in c['motion']) != f['virtual_commits'] or \
            len(c['footprints']) != f['shared_footprints'] or digest(c['motion']) != f['motion_sha256'] or \
            digest(c['footprints']) != f['footprints_sha256'] or digest(group_states(rows)) != f['group_states_sha256'] or \
            standing_owner(rows) != f['standing_owner']:
        raise ValueError('extended motion, footprint, group state or standing Follow differs')
    if digest(c['motion'][:7933]) != f['baseline_motion_sha256'] or digest(c['footprints'][:812]) != f['baseline_footprints_sha256']:
        raise ValueError('extended capture changed the accepted30-second prefix')
    result = verify_motion(rows, engine, None)
    result.update(verify_primary(rows, engine, f))
    result.update(engine_commits=len(c['motion']), shared_footprints=len(c['footprints']))
    return c, result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces', nargs='+', type=Path)
    p.add_argument('--fixture', required=True, type=Path)
    p.add_argument('--engine-library', required=True, type=Path)
    p.add_argument('--headers-root', required=True, type=Path)
    p.add_argument('--report', required=True, type=Path)
    a = p.parse_args()
    f = json.loads(a.fixture.read_text()); verify_contract(f)
    engine = ctypes.CDLL(str(a.engine_library.resolve())); configure(engine)
    results = []
    for path, case in zip(a.traces, f['cases'], strict=True):
        if path.stat().st_size != case['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest() != case['trace_sha256']:
            raise ValueError('extended capture hash or bytes differ')
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        c, result = verify_capture(rows, f, case, engine)
        header = a.headers_root / 'retail_captain_go_home_complete.h'
        if header.read_text() != render_header(c) or hashlib.sha256(header.read_bytes()).hexdigest() != f['engine_header_sha256']:
            raise ValueError('extended literal engine reference differs')
        results.append(result)
    report = dict(passed=True, cases=len(results), results=results,
                  exact_velocity_commits=sum(r['exact_velocity_commits'] for r in results),
                  owner_callbacks=sum(r['owner_callbacks'] for r in results), engine_commits=f['engine_commits'],
                  shared_footprints=f['shared_footprints'], complete_translation_journey=True,
                  natural_task_reclamation=False, persistent_follow_remains=True, whole_retail_pathfinder=False,
                  scope=f['scope'])
    a.report.write_text(json.dumps(report, indent=2) + '\n'); print(json.dumps(report))


if __name__ == '__main__':
    main()
