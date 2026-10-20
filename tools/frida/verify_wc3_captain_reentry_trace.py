#!/usr/bin/env python3
"""Verify the complete stationary, alive mixed13 captain journey and logical ranges."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_captain_approach_trace import verify_capture as verify_approach
from verify_wc3_captain_departure_trace import membership_state, verify_contract as verify_departure
from verify_wc3_captain_shared_trace import shared_state, verify_contract as verify_shared


def logical_deadlines(state):
    inner = outer = 0
    result = []
    for clock, _, birth, mode, delta, _, _, after in state['counters']:
        bit = 1 << birth
        if mode:
            inner = inner | bit if delta > 0 else inner & ~bit
        else:
            outer = outer | bit if delta > 0 else outer & ~bit
        msec = round(ctypes.c_float.from_buffer_copy(clock[0].to_bytes(4, 'little')).value * 1000)
        row = [msec, clock[0], inner, outer, after[3], after[4]]
        if inner.bit_count() != after[3] or outer.bit_count() != after[4]:
            raise ValueError('logical membership disagrees with native captain counts')
        if result and result[-1][0] == msec:
            result[-1] = row
        else:
            result.append(row)
    return result


def references(f, parent):
    refs = []
    for key in ('departure', 'shared'):
        path = parent / f[key + '_fixture']
        if hashlib.sha256(path.read_bytes()).hexdigest() != f[key + '_sha256']:
            raise ValueError('captain reentry reference fixture changed')
        refs.append(json.loads(path.read_text()))
    return refs


def verify_contract(f, departure, shared):
    verify_departure(departure)
    verify_shared(shared)
    if f['complete_engine_journey'] is not True or f['whole_retail_pathfinder'] is not False or \
            f['engine_commits'] != 5462 or f['engine_end_msec'] != 31000 or \
            f['shared_footprints'] != 353 or f['shared_generations'] != 2:
        raise ValueError('captain reentry scope/extent differs')
    if f['cases'] != departure['cases'] or f['logical_deadlines'] != logical_deadlines(departure['membership_state']):
        raise ValueError('captain reentry source or logical deadline differs')
    if len(f['logical_deadlines']) != 9 or f['logical_deadlines'][-1] != [16000, 0x417fffff, 8191, 8191, 13, 13]:
        raise ValueError('captain second all-entered deadline differs')


def render_header(f, shared):
    return ('/* Complete original mixed13 shared footprints across both owner generations. */\n'
            'static uint32_t const captain_thirteen_reentry_footprints[][4]={\n' +
            ''.join('    {' + ','.join(str(v) + 'u' for v in [r[0], r[2], r[3], r[4]]) + '},\n'
                    for r in shared['shared_state']['footprints']) + '};\n\n' +
            '/* Logical inner/outer membership after each complete native range deadline. */\n'
            'static uint32_t const captain_thirteen_reentry_membership[][6]={\n' +
            ''.join('    {' + ','.join(str(v) + 'u' for v in r) + '},\n' for r in f['logical_deadlines']) + '};\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces', nargs='+', type=Path)
    p.add_argument('--fixture', required=True, type=Path)
    p.add_argument('--engine-library', required=True, type=Path)
    p.add_argument('--check-engine-header', type=Path)
    p.add_argument('--report', required=True, type=Path)
    a = p.parse_args()
    f = json.loads(a.fixture.read_text())
    departure, shared = references(f, a.fixture.parent)
    verify_contract(f, departure, shared)
    approach_path = a.fixture.parent / departure['approach_fixture']
    if hashlib.sha256(approach_path.read_bytes()).hexdigest() != departure['approach_sha256']:
        raise ValueError('captain complete motion fixture changed')
    approach = json.loads(approach_path.read_text())
    engine = ctypes.CDLL(str(a.engine_library.resolve()))
    configure(engine)
    results = []
    for path, case in zip(a.traces, f['cases'], strict=True):
        if hashlib.sha256(path.read_bytes()).hexdigest() != case['trace_sha256'] or path.stat().st_size != case['bytes']:
            raise ValueError('captain reentry capture hash/extent differs')
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        result = verify_approach(rows, approach, case, engine)
        membership = membership_state(rows)
        if membership != departure['membership_state'] or shared_state(rows) != shared['shared_state'] or \
                logical_deadlines(membership) != f['logical_deadlines']:
            raise ValueError('captain full membership/shared state differs')
        result.update(engine_commits=5462, shared_footprints=353, logical_deadlines=9, shared_generations=2)
        results.append(result)
    if a.check_engine_header and a.check_engine_header.read_text() != render_header(f, shared):
        raise ValueError('captain complete footprint/membership engine reference differs')
    report = dict(passed=True, cases=len(results), results=results, scope=f['scope'],
                  complete_engine_journey=True, whole_retail_pathfinder=False, engine_commits=5462,
                  engine_end_msec=31000, shared_footprints=353, shared_generations=2, logical_deadlines=9)
    a.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
