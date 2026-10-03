#!/usr/bin/env python3
"""Verify nested movement heading pointers, exact arithmetic and repeat determinism."""
import argparse
import ctypes
import hashlib
import json
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1] / 'ghidra'))
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_pathing_numeric import add, multiply, square_root, divide
from generate_wc3_math_tables import reciprocal_table

FIXTURE = Path(__file__).parents[1] / 'ghidra/fixtures/retail-heading-aliases-1.27.json'


def verify(rows, engine, fixture):
    meta = [r for r in rows if r.get('event') == 'metadata']
    if len(meta) != 1 or meta[0].get('source_sha256') != fixture['source_sha256']:
        raise ValueError('heading source/map provenance differs')
    if meta[0].get('sha256') != fixture['target_sha256'] or not meta[0].get('headingEvents'):
        raise ValueError('heading target or observer configuration differs')
    report = verify_motion(rows, engine, 'turn')
    events = [r for r in rows if r.get('event') in ('heading-acos-alias', 'heading-vector-alias', 'heading-error')]
    ending = next(r for r in rows if r.get('event') == 'trace-end')
    for name in ('heading-acos-alias', 'heading-vector-alias', 'heading-error'):
        if ending.get('counts', {}).get(name) != sum(r['event'] == name for r in events):
            raise ValueError('heading alias count differs: ' + name)
    normalized, position, negative, seen = [], 0, 0, set()
    recips = reciprocal_table()
    engine.pathing_acos.argtypes = [ctypes.c_uint32]
    engine.pathing_acos.restype = ctypes.c_uint32
    engine.pathing_vector_heading.argtypes = [ctypes.c_uint32] * 2
    engine.pathing_vector_heading.restype = ctypes.c_uint32
    while position < len(events):
        batch = events[position:position + 3]
        if len(batch) != 3 or [r['event'] for r in batch] != ['heading-acos-alias', 'heading-vector-alias', 'heading-error']:
            raise ValueError('missing or reordered nested heading observations')
        acos, vector, heading = batch
        sequence = heading.get('sequence')
        if type(sequence) is not int or sequence in seen or any(r.get('sequence') != sequence for r in batch):
            raise ValueError('heading sequence linkage differs')
        seen.add(sequence)
        def pointer(row, key):
            value = row.get(key)
            if not isinstance(value, str) or not value.startswith('0x'):
                raise ValueError('invalid heading pointer: ' + key)
            try:
                number = int(value, 16)
            except ValueError as error:
                raise ValueError('invalid heading pointer: ' + key) from error
            if not 0 < number <= 0xffffffff:
                raise ValueError('heading pointer outside x86 domain')
            return number
        sp, dst, src = [pointer(acos, k) for k in ('stackPointer', 'outputPointer', 'inputPointer')]
        if acos.get('caller') != 0x1d4cbf or dst != sp + 24 or src != sp + 8:
            raise ValueError('original distinct Acos operand slots differ')
        if vector.get('caller') != 0x16f67c or vector.get('vector') != heading.get('vector'):
            raise ValueError('original vector-heading producer differs')
        if pointer(vector, 'vectorPointer') != pointer(heading, 'vectorPointer'):
            raise ValueError('vector pointer linkage differs')
        # All four live destinations occupy distinct caller slots; the synthetic oracle separately varies them.
        output = pointer(vector, 'outputPointer')
        length_ptr = pointer(vector, 'lengthPointer')
        if len({output, length_ptr, pointer(heading, 'outputPointer'), pointer(heading, 'headingPointer')}) != 4:
            raise ValueError('live heading destination relationships differ')
        words = [*heading['vector'], vector.get('length'), acos.get('input'), acos.get('output'), vector.get('output')]
        if any(type(w) is not int or not 0 <= w <= 0xffffffff for w in words):
            raise ValueError('invalid heading scalar word')
        x, y, length, quotient, arc, angle = words
        expected_length = square_root(add(multiply(x, x), multiply(y, y)))
        if length != expected_length or quotient != divide(x, length, recips):
            raise ValueError('original quotient producer words differ')
        if arc != engine.pathing_acos(quotient) or angle != engine.pathing_vector_heading(x, y):
            raise ValueError('heading C words differ from original distinct operands')
        normalized.append([x, y, heading['heading'], length, quotient, arc, angle, heading['error'],
                           dst - sp, src - sp])
        negative += bool(quotient & 0x80000000)
        position += 3
    if not normalized or not negative:
        raise ValueError('missing negative-input heading alias witness')
    digest = hashlib.sha256(json.dumps(normalized, separators=(',', ':')).encode()).hexdigest()
    if len(normalized) != fixture['heading_cases'] or digest != fixture['alias_sha256']:
        raise ValueError('frozen heading alias words differ')
    return dict(report, alias_cases=len(normalized), negative_quotients=negative, alias_sha256=digest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, default=FIXTURE)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--repeat', type=Path, required=True)
    parser.add_argument('--engine-library', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.capture.resolve() == args.repeat.resolve():
        parser.error('repeat must be a separate captured run')
    engine = ctypes.CDLL(str(args.engine_library.resolve()))
    for name in ('motion', 'velocity_commit', 'velocity', 'integrate'):
        getattr(engine, 'pathing_' + name).argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    engine.pathing_heading_error.argtypes = [ctypes.c_uint32] * 3
    engine.pathing_heading_error.restype = ctypes.c_uint32
    fixture = json.loads(args.fixture.read_text())
    read = lambda path: [json.loads(line) for line in path.read_text().splitlines()]
    first, repeated = [verify(read(p), engine, fixture) for p in (args.capture, args.repeat)]
    for key in ('alias_sha256', 'decision_sha256', 'heading_sha256', 'velocity_sha256'):
        if first[key] != repeated[key]:
            raise ValueError('repeat differs: ' + key)
    report = dict(first, repeat_equal=True, scope=fixture['scope'],
                  captures=[hashlib.sha256(p.read_bytes()).hexdigest() for p in (args.capture, args.repeat)])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
