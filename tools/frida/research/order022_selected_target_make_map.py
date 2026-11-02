#!/usr/bin/env python3
"""Build the ORDER-02.2 UI packet / owner-change / interception arena.

Derived from queue263_make_map.py: the shared passage builder supplies the flat
64x64 Human02Interlude terrain; this builder only adds one authored ANcl clone
'AQ22' (6 s casting time, 8 s follow-through, no ability disable) to war3map.w3a.
"""
import argparse, hashlib, json, struct, subprocess, sys, tempfile
from pathlib import Path
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

# ANcl DataA follow-through, DataB no target, DataC visible, DataE no disable,
# DataF base order; generic acas casting time distinguishes the windup.
MODS = [(b'Ncl1', 2, 1, 1, struct.pack('<f', 8.0)), (b'Ncl2', 0, 1, 2, struct.pack('<i', 0)),
        (b'Ncl3', 0, 1, 3, struct.pack('<i', 1)), (b'Ncl4', 2, 1, 4, struct.pack('<f', 0.0)),
        (b'Ncl5', 0, 1, 5, struct.pack('<i', 0)), (b'Ncl6', 3, 1, 6, b'channel\0'),
        (b'acas', 2, 1, 0, struct.pack('<f', 6.0)), (b'amcs', 0, 1, 0, struct.pack('<i', 0)),
        (b'acdn', 2, 1, 0, struct.pack('<f', 0.0)), (b'aher', 0, 0, 0, struct.pack('<i', 0))]


def w3a_with_channel(data):
    version, originals = struct.unpack_from('<II', data)
    if version != 2 or originals != 0:
        raise ValueError('expected the shared builder version2 custom-only ability table')
    count_at = 8
    row = b'ANcl' + b'AQ22' + struct.pack('<I', len(MODS))
    for field, kind, level, pointer, value in MODS:
        row += field + struct.pack('<III', kind, level, pointer) + value + b'AQ22'
    if b'AQ22' in data:
        raise ValueError('AQ22 already authored')
    count = struct.unpack_from('<I', data, count_at)[0]
    return data[:count_at] + struct.pack('<I', count + 1) + data[count_at + 4:] + row


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--variant', default='selected-target', choices=('selected-target',))
    args = ap.parse_args()
    if args.output.exists():
        ap.error('output must be new')
    tool = str(ROOT / 'build/bin/mpqtool')
    with tempfile.TemporaryDirectory(prefix='order022-') as tmp:
        temp = Path(tmp)
        raw = temp / 'arena.w3m'
        probe = temp / 'order022_selected_target_probe.j'
        probe.write_text((HERE / 'order022_selected_target_probe.j').read_text().replace('@VARIANT@', args.variant))
        subprocess.run([sys.executable, str(HERE / 'group032_make_map.py'), '--base', str(args.base),
                        '--probe', str(probe), '--preload-output', 'order022-' + args.variant + '.txt',
                        '--task', 'ORDER-02.2', '--output', str(raw)], check=True)
        meta = json.loads(raw.with_suffix('.json').read_text())
        members = subprocess.check_output([tool, '-mpq', str(raw), 'ls']).decode().splitlines()
        payload = temp / 'packed.mpq'
        cmd = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members):
            if member == '(listfile)':
                continue
            data = subprocess.check_output([tool, '-mpq', str(raw), 'cat', member])
            if member == 'war3map.w3e':
                data = bytearray(data)
                cliff_at = 17 + struct.unpack_from('<I', data, 13)[0] * 4
                size_at = cliff_at + 4 + struct.unpack_from('<I', data, cliff_at)[0] * 4
                struct.pack_into('<ff', data, size_at + 8, 0, 0)
                data = bytes(data)
                meta['fine_origin'] = [0, 0]
            if member == 'war3map.shd':
                data = bytes(64 * 64)  # shared builder terrain is 16x16 tiles
            if member == 'war3map.w3a':
                data = w3a_with_channel(data)
            if member in ('war3map.w3e', 'war3map.shd', 'war3map.w3a'):
                meta['changed_members'][member] = hashlib.sha256(data).hexdigest()
            path = temp / str(i)
            path.write_bytes(data)
            cmd += [str(path), member]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        args.output.write_bytes(raw.read_bytes()[:512] + payload.read_bytes())
        meta.update(map_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(), variant=args.variant,
                    probe_template_sha256=hashlib.sha256((HERE / 'order022_selected_target_probe.j').read_bytes()).hexdigest(),
                    target_builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        args.output.with_suffix('.json').write_text(json.dumps(meta, indent=2) + '\n')


if __name__ == '__main__':
    main()
