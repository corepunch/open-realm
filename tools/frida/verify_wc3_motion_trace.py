#!/usr/bin/env python3
"""Replay raw retail motion decisions through OpenRealm's production C arithmetic."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from analyze_pathfinding_trace import analyze

FIELDS = ('speed', 'heading', 'error', 'increment', 'turn', 'window', 'stop')


def verify(rows, engine, scenario):
    summary = analyze(rows, scenario)
    if summary['violations']:
        raise ValueError('; '.join(summary['violations']))
    metadata = next(r for r in rows if r.get('event') == 'metadata')
    ending = next(r for r in rows if r.get('event') == 'trace-end')
    decisions = [r for r in rows if r.get('event') == 'motion-decision']
    count = ending.get('counts', {}).get('motion-decision', 0)
    if not metadata.get('motionEvents') or not decisions or count != len(decisions):
        raise ValueError('missing, truncated, or inconsistent motion decisions')
    normalized, movers = [], {}
    stopped = 0
    for index, row in enumerate(decisions):
        keys = (*FIELDS, 'nextSpeed', 'nextHeading')
        if any(type(row.get(k)) is not int or not 0 <= row[k] <= 0xffffffff for k in keys):
            raise ValueError(f'decision {index}: invalid raw words')
        words = (ctypes.c_uint32 * 7)(*[row[k] for k in FIELDS])
        engine.pathing_motion(words)
        expected = [row['nextSpeed'], row['nextHeading']]
        if list(words)[:2] != expected:
            raise ValueError(f'decision {index}: C {[hex(w) for w in words[:2]]} != retail {[hex(w) for w in expected]}')
        if list(words)[2:] != [row[k] for k in FIELDS[2:]]:
            raise ValueError(f'decision {index}: input words changed')
        mover = movers.setdefault(row['mover'], len(movers))
        normalized.append([mover, *[row[k] for k in keys]])
        stopped += row['nextSpeed'] == 0
    digest = hashlib.sha256(json.dumps(normalized, separators=(',', ':')).encode()).hexdigest()
    result = dict(passed=True, exact_decisions=count, stopped_decisions=stopped,
                movers=len(movers), decision_sha256=digest,
                scope='Complete captured speed/heading helper decisions; excludes velocity trig and position integration')
    if metadata.get('headingEvents'):
        headings = [r for r in rows if r.get('event') == 'heading-error']
        if not headings or ending.get('counts',{}).get('heading-error') != len(headings):
            raise ValueError('missing, truncated, or inconsistent heading errors')
        normalized = []
        for index,row in enumerate(headings):
            vector = row.get('vector')
            if not isinstance(vector,list) or len(vector)!=2:
                raise ValueError(f'heading {index}: invalid vector')
            words = [*vector,row.get('heading'),row.get('error')]
            if any(type(w) is not int or not 0<=w<=0xffffffff for w in words):
                raise ValueError(f'heading {index}: invalid raw words')
            if engine.pathing_heading_error(*words[:3]) != words[3]:
                raise ValueError(f'heading {index}: C error differs from retail')
            normalized.append(words)
        result.update(exact_heading_errors=len(headings),
            heading_sha256=hashlib.sha256(json.dumps(normalized,separators=(',',':')).encode()).hexdigest())
    if metadata.get('velocityEvents'):
        commits = [r for r in rows if r.get('event') == 'velocity-commit']
        if not commits or ending.get('counts', {}).get('velocity-commit') != len(commits):
            raise ValueError('missing, truncated, or inconsistent velocity commits')
        normalized = []
        for index, row in enumerate(commits):
            def words(key, count):
                value = row.get(key)
                if not isinstance(value,list) or len(value)!=count or any(type(w) is not int or not 0<=w<=0xffffffff for w in value):
                    raise ValueError(f'commit {index}: invalid {key} words')
                return value
            before, after, clock = words('before',8), words('after',8), words('clock',3)
            if any(type(row.get(k)) is not int or not 0<=row[k]<=0xffffffff for k in ('speed','heading')):
                raise ValueError(f'commit {index}: invalid speed/heading words')
            velocity = (ctypes.c_uint32*6)(*before[4:6],row['speed'],row['heading'],*before[6:8])
            engine.pathing_velocity_commit(velocity)
            state = (ctypes.c_uint32*11)(*before[2:6],*before[:2],*clock,0,0)
            engine.pathing_integrate(state)
            if list(velocity)[:2]!=after[4:6] or list(state)[:2]!=after[2:4] or list(state)[4:6]!=after[:2]:
                raise ValueError(f'commit {index}: C velocity/position/clock differs from retail')
            if velocity[5]!=after[7]:
                raise ValueError(f'commit {index}: C facing differs from retail')
            if after[6]!=before[6]:
                raise ValueError(f'commit {index}: maximum changed')
            mover = movers.setdefault(row['mover'],len(movers))
            normalized.append([mover,row['speed'],row['heading'],*before,*clock,*after])
        result.update(exact_velocity_commits=len(commits),exact_position_commits=len(commits),exact_facing_commits=len(commits),
            velocity_sha256=hashlib.sha256(json.dumps(normalized,separators=(',',':')).encode()).hexdigest(),
            scope='Complete captured scalar decisions, velocity and position/clock commits; facing commits included; whole engine cadence remains excluded')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--engine-library', required=True, type=Path)
    parser.add_argument('--scenario', required=True, choices=('open', 'turn', 'stock_turn', 'pathing_toggle'))
    parser.add_argument('--compare', type=Path, help='repeat capture; compare normalized raw decision sequence')
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    engine = ctypes.CDLL(str(args.engine_library.resolve()))
    engine.pathing_motion.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    engine.pathing_velocity_commit.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    engine.pathing_velocity.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    engine.pathing_integrate.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    engine.pathing_heading_error.argtypes = [ctypes.c_uint32]*3
    engine.pathing_heading_error.restype = ctypes.c_uint32
    read = lambda path: [json.loads(line) for line in path.read_text().splitlines()]
    result = verify(read(args.trace), engine, args.scenario)
    if args.compare:
        repeated = verify(read(args.compare), engine, args.scenario)
        if result['decision_sha256'] != repeated['decision_sha256']:
            raise ValueError('repeat differs in normalized motion inputs/outputs')
        if result.get('velocity_sha256') != repeated.get('velocity_sha256'):
            raise ValueError('repeat differs in normalized velocity inputs/outputs')
        if result.get('heading_sha256') != repeated.get('heading_sha256'):
            raise ValueError('repeat differs in normalized heading inputs/outputs')
        result['repeat_equal'] = True
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    result.update(trace_sha256=digest(args.trace), engine_library_sha256=digest(args.engine_library))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
