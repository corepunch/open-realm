#!/usr/bin/env python3
"""Repair the inherited shadow size of a frozen TARGET-02.1 pursuit map.

Only war3map.shd changes. Terrain, pathing, script, units and public probe
producers retain their exact original bytes; the original map is immutable.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        ap.error('new output required')
    root = Path(__file__).resolve().parents[3]
    tool = str(root / 'build/bin/mpqtool')
    members = subprocess.check_output([tool, '-mpq', str(args.base), 'ls']).decode().splitlines()
    terrain = subprocess.check_output([tool, '-mpq', str(args.base), 'cat', 'war3map.w3e'])
    if terrain[:4] != b'W3E!':
        ap.error('terrain required')
    cliff_at = 17 + struct.unpack_from('<I', terrain, 13)[0] * 4
    size_at = cliff_at + 4 + struct.unpack_from('<I', terrain, cliff_at)[0] * 4
    width, height = struct.unpack_from('<II', terrain, size_at)
    if (width, height) != (17, 17):
        ap.error('the frozen64x64 pursuit arena is required')
    changed = {}
    retained = {}
    with tempfile.TemporaryDirectory(prefix='target356-') as temp:
        directory = Path(temp)
        packed = directory / 'payload.mpq'
        command = [tool, '-mpq', str(packed), 'pack']
        for index, member in enumerate(members):
            if member == '(listfile)':
                continue
            data = subprocess.check_output([tool, '-mpq', str(args.base), 'cat', member])
            if member == 'war3map.shd':
                data = bytes((width - 1) * (height - 1) * 16)
                changed[member] = hashlib.sha256(data).hexdigest()
            else:
                retained[member] = hashlib.sha256(data).hexdigest()
            path = directory / str(index)
            path.write_bytes(data)
            command += [str(path), member]
        if list(changed) != ['war3map.shd']:
            ap.error('original shadow member required')
        subprocess.run(command, check=True)
        args.output.write_bytes(args.base.read_bytes()[:512] + packed.read_bytes())
    args.output.with_suffix('.json').write_text(json.dumps(dict(
        base_sha256=hashlib.sha256(args.base.read_bytes()).hexdigest(),
        map_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
        builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        changed=changed, retained=retained, shadow_size=4096), indent=2) + '\n')


if __name__ == '__main__':
    main()
