#!/usr/bin/env python3
"""Verify per-update retail cursor frames against an extracted MDX SEQS chunk."""
import argparse
import json
import struct
from pathlib import Path


def sequences(path):
    data = path.read_bytes()
    if data[:4] != b'MDLX':
        raise ValueError('expected an extracted MDX file')
    offset = 4
    while offset + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, offset)
        offset += 8
        if offset + size > len(data):
            raise ValueError('truncated MDX chunk')
        if tag == b'SEQS':
            if size % 132:
                raise ValueError('invalid SEQS size')
            return [(data[i:i+80].split(b'\0')[0].decode(), *struct.unpack_from('<II', data, i+80))
                    for i in range(offset, offset+size, 132)]
        offset += size
    raise ValueError('missing SEQS')


def verify(rows, seqs):
    if rows[0].get('sampleMs') != 0:
        raise ValueError('record with --sample-ms 0; sparse samples cannot verify clock steps')
    previous = start = None
    checked = restarts = wraps = 0
    seen = set()
    for row in rows:
        event = row.get('event')
        if row.get('type') == 'error' or event == 'trace-failed':
            raise ValueError('trace contains an instrumentation error')
        if event == 'sequence-start':
            start = row
        if event != 'sample':
            continue
        index = row['animIndex']
        name, begin, end = seqs[index]
        seen.add(name)
        base = start['frame'] if start else (previous['animFrame'] if previous and previous['animIndex'] == index else None)
        if base is not None:
            expected = begin + (base - begin + row['step']) % (end - begin) if end > begin else begin
            if expected != row['animFrame']:
                raise ValueError(f"{row['ms']}: {name}: expected {expected}, got {row['animFrame']}")
            checked += 1
        restarts += start is not None
        wraps += row['sequence'] == -2
        previous, start = row, None
    if rows[-1].get('event') != 'trace-end' or not rows[-1].get('draws') or not checked:
        raise ValueError('trace did not finish with observed draws and checked animation frames')
    return dict(checked=checked, restarts=restarts, wraps=wraps, sequences=sorted(seen), draws=rows[-1]['draws'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--trace', type=Path, required=True)
    parser.add_argument('--model', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(verify([json.loads(line) for line in args.trace.read_text().splitlines()], sequences(args.model)), indent=2))


if __name__ == '__main__':
    main()
