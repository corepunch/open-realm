#!/usr/bin/env python3
"""BASE-02.1: copied Human02Interlude map with authored movement-type clones.

Reuses make_wc3_pathfinding_map.instrument() unchanged (imported, not edited).
Changes war3map.j (probe) and appends custom w3u rows (hfoo clones with umvt/umvh/umvf).
"""
import argparse
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import make_wc3_pathfinding_map as base  # noqa: E402

# (rawcode, umvt bytes, umvh, umvf)
CLONES = [
    (b'hM00', b'foot', None, None), (b'hM01', b'horse', None, None),
    (b'hM02', b'fly', 150.0, 40.0), (b'hM03', b'hover', 50.0, None),
    (b'hM04', b'float', None, None), (b'hM05', b'amph', None, None),
    (b'hM06', b'unbuild', None, None), (b'hM07', b'none', None, None),
    (b'hM08', b'', None, None), (b'hM09', b'_', None, None),
    (b'hM10', b'FLY', None, None), (b'hM11', b'Float', None, None),
    (b'hM12', b'foot,fly', None, None), (b'hM13', b'boat', None, None),
    (b'hM14', b'-', None, None),
]


def append_clones(data):
    version = struct.unpack_from('<I', data)[0]
    if version != 1:
        raise ValueError('requires version1 unit modifications')
    cursor, ids = 4, set()
    for table in range(2):
        count_at = cursor
        count = struct.unpack_from('<I', data, cursor)[0]
        cursor += 4
        for _ in range(count):
            old, new, n = struct.unpack_from('<4s4sI', data, cursor)
            cursor += 12
            ids.add(new)
            for _ in range(n):
                field, kind = struct.unpack_from('<4sI', data, cursor)
                cursor += 8
                if kind in (0, 1, 2):
                    cursor += 4
                elif kind == 3:
                    cursor = data.index(b'\0', cursor) + 1
                else:
                    raise ValueError('unsupported modification type')
                cursor += 4
    if cursor != len(data) or any(code in ids for code, *_ in CLONES):
        raise ValueError('trailing data or clone exists')
    out = data[:count_at] + struct.pack('<I', count + len(CLONES)) + data[count_at + 4:]
    for code, movetp, height, floor in CLONES:
        mods = [b'umvt' + struct.pack('<I', 3) + movetp + b'\0' + code]
        if height is not None:
            mods.append(b'umvh' + struct.pack('<If', 2, height) + code)
        if floor is not None:
            mods.append(b'umvf' + struct.pack('<If', 2, floor) + code)
        out += b'hfoo' + code + struct.pack('<I', len(mods)) + b''.join(mods)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--tool', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--probe', type=Path, default=Path(__file__).with_name('base021_probe.j'))
    args = ap.parse_args()
    if args.output.exists():
        ap.error('output must be new')
    original = args.base.read_bytes()
    if original[:4] != b'HM3W' or original[512:516] != b'MPQ\x1a':
        ap.error('requires the original wrapped campaign map')
    probe_path = args.probe
    probe = probe_path.read_text()
    tool = str(args.tool.resolve())
    members = subprocess.check_output([tool, '-mpq', str(args.base), 'ls']).decode().splitlines()
    for m in ('war3map.j', 'war3map.w3u'):
        if members.count(m) != 1:
            ap.error('missing ' + m)
    side = {}
    with tempfile.TemporaryDirectory(prefix='b021-map-') as temp:
        root = Path(temp)
        payload = root / 'payload.mpq'
        command = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members):
            data = subprocess.check_output([tool, '-mpq', str(args.base), 'cat', member])
            if member == 'war3map.j':
                source = data.decode('utf-8').replace('\r\n', '\n')
                data = base.instrument(source, probe, 'open').encode('utf-8')
                side['j'] = data
            if member == 'war3map.w3u':
                data = append_clones(data)
                side['w3u'] = data
            path = root / str(i)
            path.write_bytes(data)
            command.extend([str(path), member])
        subprocess.run(command, check=True)
        args.output.write_bytes(original[:512] + payload.read_bytes())
    for k, v in side.items():
        args.output.with_suffix('.' + k).write_bytes(v)
    h = lambda b: hashlib.sha256(b).hexdigest()
    result = dict(task='BASE-02.1', base_sha256=h(original), map_sha256=h(args.output.read_bytes()),
                  probe_sha256=h(probe_path.read_bytes()), builder_sha256=h(Path(__file__).read_bytes()),
                  imported_builder_sha256=h(Path(base.__file__).read_bytes()),
                  j_sha256=h(side['j']), w3u_sha256=h(side['w3u']), changed_members=['war3map.j', 'war3map.w3u'],
                  clones=[dict(code=c.decode(), umvt=m.decode('latin1'), umvh=hh, umvf=f) for c, m, hh, f in CLONES],
                  container='rebuilt MPQ with original HM3W header; signature not retained')
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
