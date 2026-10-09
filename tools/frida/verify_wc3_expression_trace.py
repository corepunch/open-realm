#!/usr/bin/env python3
"""Certify compiled chained expressions through public natives and a whole Move."""
import argparse
import collections
import ctypes
import hashlib
import json
import re
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary
from verify_wc3_selected_queued_trace import canonical, digest, motion_words, owner_order
from verify_wc3_target_overlap_trace import overlap_lifecycle


def expressions(rows):
    return [{k: v for k, v in r.items() if k != 'ms'} for r in rows
            if r.get('event') in ('numeric-marker', 'numeric-native', 'expression-marker')]


def timers(rows):
    # Public handle ordinals are allocator identities, not expression results.
    return [{k: r[k] for k in ('timeout', 'periodic', 'handler')} for r in rows
            if r.get('event') == 'expression-timer-start']


def verify_contract(s):
    cases = s['expressions']
    if len(cases) != 27 or len({c['id'] for c in cases}) != 27:
        raise ValueError('expression cases incomplete or duplicated')
    marks = [re.fullmatch(r'PATHTRACE tick=(\d+) label=(\w+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)', m)
             for m in s['markers']]
    if (len(marks) != 304 or any(m is None for m in marks) or
        [int(m[1]) for m in marks if m[2] == 'sample'] != list(range(1, 301)) or
        [(int(m[1]), m[2], int(m[5])) for m in marks if 'expression_move' in m[2]] !=
        [(10, 'before_expression_move', 0), (10, 'after_expression_move', 851986)]):
        raise ValueError('expression-authored order and complete timer timeline differ')
    expected = []
    for c in cases:
        expected.append(dict(event='numeric-marker', value=f'PATHNUM case={c["id"]} native={c["native"]}'))
        if c['id'] in ('int_operands', 'real_operands', 'mixed_operands'):
            kind = 'integer' if c['id'] == 'int_operands' else 'real'
            expected += [dict(event='expression-marker', value=f'PATHEXPR operand={n} type={kind}')
                         for n in range(1, 5 if c['id'] == 'mixed_operands' else 4)]
        expected.append(dict(event='numeric-native', case=c['id'], native=c['native'], input=c['input'], output=c['output']))
        expected.append(dict(event='numeric-marker', value=f'PATHNUM done={c["id"]}'))
    if s['expression_sequence'] != expected:
        raise ValueError('compiled expression values or operand execution order differ')
    by_id = {c['id']: c for c in cases}
    for name, word in dict(int_sub=85, int_sub_add=95, int_add_sub=105, int_div=10,
                           int_div_mul=8, int_precedence=0, int_right_parens=95,
                           int_left_parens=85, int_negative=0xffffff8d).items():
        if by_id[name]['input'] != word:
            raise ValueError('left-associated integer expression differs: ' + name)
    if (s['destination'] != [1616, 1712] or s['timers'] !=
        [dict(timeout=0x3dccccce, periodic=1, handler=1),
         dict(timeout=1123024896, periodic=0, handler=15),
         dict(timeout=1008981771, periodic=0, handler=17)] or len(s['motion']) != 432):
        raise ValueError('expression timer/native destination/whole motion extent differs')


def render_header(s):
    result = ('#ifndef BZ_RETAIL_EXPRESSIONS_H\n#define BZ_RETAIL_EXPRESSIONS_H\n'
              '/* Literal original compiled-expression inputs/results and whole movement. */\n'
              'static struct { cstring_t expression; uint32_t input, output, operands; bool integer; } const expression82_cases[]={\n')
    for c in s['expressions']:
        count = 1234 if c['id'] == 'mixed_operands' else 123 if c['id'] in ('int_operands', 'real_operands') else 0
        result += '    {' + json.dumps(c['expression']) + f',0x{c["input"]:08x}u,0x{c["output"]:08x}u,{count}u,' + ('true' if c['native'] == 'I2R' else 'false') + '},\n'
    result += '};\nstatic uint32_t const expression82_motion[][7]={\n'
    return result + ''.join('    {' + ','.join(str(v) + 'u' for v in row) + '},\n' for row in s['motion']) + '};\n#endif\n'


def verify(rows, s, case, engine):
    verify_contract(s)
    meta = [r for r in rows if r.get('event') == 'metadata']
    ends = [r for r in rows if r.get('event') == 'trace-end']
    if len(meta) != 1 or {k: v for k, v in meta[0].items() if k not in ('event', 'pid')} != case['metadata']:
        raise ValueError('expression capture provenance differs')
    if len(ends) != 1 or not ends[0].get('installed') or any(r.get('event') == 'trace-failed' or r.get('type') == 'error' for r in rows):
        raise ValueError('expression capture incomplete/failed')
    counts = collections.Counter(r.get('event') for r in rows)
    for event, count in case['event_counts'].items():
        if counts[event] != count:
            raise ValueError('expression observer extent differs: ' + event)
    # Some observers deliberately record only bounded samples of a larger call count.
    # Retain both extents; never mistake sampled spatial/replan rows for all calls.
    if ends[0]['counts'] != case['observer_counts']:
        raise ValueError('expression complete observer call counters differ')
    if (expressions(rows) != s['expression_sequence'] or timers(rows) != s['timers'] or
        digest(canonical(rows)) != s['phases_sha256'] or motion_words(rows) != s['motion'] or
        owner_order(rows) != s['owner_order'] or overlap_lifecycle(rows) != s['lifecycle'] or
        [r['value'] for r in rows if r.get('event') == 'marker'] != s['markers'] or
        [r['destination'] for r in rows if r.get('event') == 'point-task'] != [s['destination']]):
        raise ValueError('expression producer/decisions/outcome differ')
    loaded = [r for r in rows if r.get('event') == 'map-load-complete']
    if len(loaded) != 1 or (loaded[0]['width'], loaded[0]['height']) != (64, 64) or any(loaded[0]['cells']) or digest(loaded[0]['hierarchy']) != s['hierarchy_sha256']:
        raise ValueError('expression real empty terrain loader differs')
    result = verify_motion(rows, engine, None)
    result.update(verify_primary(rows, engine, s))
    result.update(passed=True, compiled_expressions=27, operand_calls=10,
                  motion_sha256=digest(s['motion']))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces', nargs='+', type=Path)
    p.add_argument('--fixture', required=True, type=Path)
    p.add_argument('--engine-library', required=True, type=Path)
    p.add_argument('--check-engine-header', type=Path)
    p.add_argument('--report', required=True, type=Path)
    a = p.parse_args(); s = json.loads(a.fixture.read_text())
    engine = ctypes.CDLL(str(a.engine_library.resolve())); configure(engine)
    engine.pathing_heading_error.argtypes = [ctypes.c_uint32] * 3
    engine.pathing_heading_error.restype = ctypes.c_uint32
    results = []
    for path, case in zip(a.traces, s['cases'], strict=True):
        result = verify([json.loads(l) for l in path.read_text().splitlines()], s, case, engine)
        result['trace_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest(); results.append(result)
    if a.check_engine_header and a.check_engine_header.read_text() != render_header(s):
        raise ValueError('expression engine literal header differs')
    report = dict(passed=True, cases=len(results), results=results, scope=s['scope'])
    for key in ('compiled_expressions', 'operand_calls', 'exact_velocity_commits', 'exact_decisions', 'owner_callbacks'):
        report[key] = sum(r[key] for r in results)
    a.report.write_text(json.dumps(report, indent=2) + '\n'); print(json.dumps(report, indent=2))


if __name__ == '__main__': main()
