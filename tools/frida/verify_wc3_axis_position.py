#!/usr/bin/env python3
"""Verify public axis-position calls, predicted world queries and native pose writes."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re
from verify_wc3_arrival_trace import configure, words
from verify_wc3_motion_trace import verify as verify_motion

KINDS = ('position-marker', 'position-native', 'position-query', 'position-commit')
CASES = [f'{state}_{axis}_{mode}' for state in ('moving', 'idle') for mode in ('same', 'shift') for axis in ('x', 'y')]


def normalized(rows):
    return [{k: v for k, v in r.items() if k not in ('ms', 'unit', 'mover')}
            for r in rows if r.get('event') in KINDS]


def digest(rows):
    return hashlib.sha256(json.dumps(normalized(rows), separators=(',', ':')).encode()).hexdigest()


def verify(rows, engine, fixture):
    meta = [r for r in rows if r.get('event') == 'metadata']
    end = [r for r in rows if r.get('event') == 'trace-end']
    if any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
        raise ValueError('position observer error')
    if len(meta) != 1 or meta[0].get('sha256') != fixture['binary_sha256'] or meta[0].get('source_sha256') != fixture['source_sha256']:
        raise ValueError('position source/target differs')
    if not all(meta[0].get(k) for k in ('owned', 'motionEvents', 'velocityEvents', 'taskEvents')):
        raise ValueError('position observer options missing')
    if len(end) != 1 or not end[0].get('installed'):
        raise ValueError('position completion missing')
    for kind, count in (('position-native', 40), ('position-query', 40), ('position-commit', 8)):
        if sum(r.get('event') == kind for r in rows) != count or end[0].get('counts', {}).get(kind) != count:
            raise ValueError('position missing/truncated ' + kind)
    if digest(rows) != fixture['position_sha256']:
        raise ValueError('position producer sequence differs')
    marks = [re.fullmatch(r'PATHTRACE tick=(\d+) label=(\w+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)', r.get('value', ''))
             for r in rows if r.get('event') == 'marker']
    if any(m is None for m in marks): raise ValueError('position malformed sample')
    labels = [m[2] for m in marks]
    if any(labels.count(k) != 1 for k in ('start_axis_position', 'order_accepted', 'axis_stop_accepted', 'complete')) or any(k.endswith('rejected') for k in labels):
        raise ValueError('position admission/Stop/completion differs')
    if [int(m[1]) for m in marks if m[2] == 'sample'] != list(range(1, 301)):
        raise ValueError('position samples incomplete')
    active = None; seen = []
    for r in rows:
        if r.get('event') not in KINDS: continue
        if r['event'] == 'position-marker':
            match = re.fullmatch(r'PATHPOSE (case|done)=([a-z0-9_]+)', r.get('value', ''))
            if not match: raise ValueError('position malformed producer marker')
            if match[1] == 'case':
                if active is not None: raise ValueError('position nested producer case')
                active = match[2]; seen.append(active)
            else:
                if active != match[2]: raise ValueError('position case completion differs')
                active = None
        elif r.get('case') != active or active is None:
            raise ValueError('position row outside its producer case')
    if active is not None or seen != CASES:
        raise ValueError('position producer order differs')
    native = [r for r in rows if r.get('event') == 'position-native']
    actors = {(r.get('unit'), r.get('handle'), r.get('rawcode')) for r in native}
    if len(actors) != 1 or not next(iter(actors))[0] or not next(iter(actors))[1] or next(iter(actors))[2] != fixture['rawcode']:
        raise ValueError('position resolved actor differs')
    actor = next(iter(actors))[0]
    sources = [r for r in rows if r.get('event') in ('position-query', 'position-commit')]
    movers = {r.get('mover') for r in sources}
    if len(movers) != 1 or next(iter(movers)) in (None, '0x0') or any(r.get('unit') != actor for r in sources):
        raise ValueError('position bridge actor changed')
    engine.pathing_position_bridge.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
    for case in CASES:
        calls = [r for r in native if r['case'] == case]
        setter = 'SetUnitX' if '_x_' in case else 'SetUnitY'
        if [r['name'] for r in calls] != ['GetUnitX', 'GetUnitY', setter, 'GetUnitX', 'GetUnitY']:
            raise ValueError('position native call order differs')
        case_rows = [r for r in sources if r['case'] == case]
        queries = [r for r in case_rows if r['event'] == 'position-query']
        commits = [r for r in case_rows if r['event'] == 'position-commit']
        if len(queries) != 5 or len(commits) != 1 or commits[0].get('notify') != 1 or commits[0].get('native') != setter:
            raise ValueError('position bridge traversal differs')
        commit = commits[0]
        axis = 0 if setter == 'SetUnitX' else 1
        if commit['input'][axis] != calls[2].get('input') or commit['input'][2] != 0:
            raise ValueError('position setter input differs')
        for r in case_rows:
            before = words(r.get('before'), 8); after = words(r.get('after'), 8)
            clock = words(r.get('clock'), 3); origin = words(r.get('origin'), 2)
            point = words(r.get('input') if r['event'] == 'position-commit' else r.get('output'), 3)
            inp = before + clock + origin + point[:2]
            out = (ctypes.c_uint32 * 12)(); arg = (ctypes.c_uint32 * 15)(*inp)
            engine.pathing_position_bridge(arg, out)
            if list(arg) != inp: raise ValueError('position C mutated input')
            if r['event'] == 'position-query':
                if after != before or list(out)[10:12] != point[:2] or point[2] != 0:
                    raise ValueError('position predicted world query differs')
            else:
                if list(out)[:8] != after or before[4:] != after[4:] or queries[3]['before'] != after or list(out)[8:10] != queries[3]['output'][:2]:
                    raise ValueError('position committed pose/velocity/facing differs')
        if [r['output'][0 if 'X' in r['native'] else 1] for r in (queries[0], queries[1], queries[3], queries[4])] != [r['output'] for r in (calls[0], calls[1], calls[3], calls[4])]:
            raise ValueError('position public getter differs from bridge query')
    result = verify_motion(rows, engine, None)
    result.update(binary_sha256=fixture['binary_sha256'], public_position_calls=40, position_query_cases=40,
                  position_commits=8, kept_velocity_facing=8, position_sha256=digest(rows),
                  scope='Actual public X/Y same-word and fractional setters during Move and after Stop; original bridge prediction/write matches C including captured elapsed. Engine owner-clock production remains NUM-02.3.')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path); parser.add_argument('--compare', type=Path)
    parser.add_argument('--fixture', type=Path, required=True); parser.add_argument('--engine-library', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args(); engine = ctypes.CDLL(str(args.engine_library.resolve())); configure(engine)
    fixture = json.loads(args.fixture.read_text())
    def capture(path): return verify([json.loads(s) for s in path.read_text().splitlines()], engine, fixture)
    result = capture(args.trace)
    if args.compare:
        other = capture(args.compare)
        if any(result[k] != other[k] for k in ('position_sha256', 'decision_sha256', 'velocity_sha256')):
            raise ValueError('position repeat differs')
        result['repeated'] = True
    args.report.write_text(json.dumps(result, indent=2) + '\n'); print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
