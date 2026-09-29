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
    return dict(passed=True, exact_decisions=count, stopped_decisions=stopped,
                movers=len(movers), decision_sha256=digest,
                scope='Complete captured speed/heading helper decisions; excludes velocity trig and position integration')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--engine-library', required=True, type=Path)
    parser.add_argument('--scenario', required=True, choices=('open', 'turn'))
    parser.add_argument('--compare', type=Path, help='repeat capture; compare normalized raw decision sequence')
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    engine = ctypes.CDLL(str(args.engine_library.resolve()))
    engine.pathing_motion.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    read = lambda path: [json.loads(line) for line in path.read_text().splitlines()]
    result = verify(read(args.trace), engine, args.scenario)
    if args.compare:
        repeated = verify(read(args.compare), engine, args.scenario)
        if result['decision_sha256'] != repeated['decision_sha256']:
            raise ValueError('repeat differs in normalized motion inputs/outputs')
        result['repeat_equal'] = True
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    result.update(trace_sha256=digest(args.trace), engine_library_sha256=digest(args.engine_library))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
