#!/usr/bin/env python3
"""Add an authored repulsor to the stock critter selected by the public item scene."""
import argparse, hashlib, json, struct, subprocess, tempfile
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--tool', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        ap.error('output must be new')
    tool = str(args.tool.resolve())
    # The frozen stock cast chooses necr. Authored separation is enabled here;
    # neither the producer nor its runtime flags are modified by the observer.
    fields = [('urpo', 0, 1), ('urpp', 0, 0), ('urpg', 0, 3), ('urpr', 0, 2), ('umvs', 0, 0), ('ucol', 2, 16.)]
    unit = b'necr' + bytes(4) + struct.pack('<I', len(fields))
    for field, kind, value in fields:
        unit += field.encode() + struct.pack('<I', kind) + struct.pack('<f' if kind == 2 else '<i', value) + bytes(4)
    unit = struct.pack('<II', 2, 1) + unit + struct.pack('<I', 0)
    members = subprocess.check_output([tool, '-mpq', str(args.base), 'ls']).decode().splitlines()
    with tempfile.TemporaryDirectory(prefix='mechanical201-') as temp:
        root = Path(temp)
        payload = root / 'payload.mpq'
        cmd = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(m for m in members if m != '(listfile)'):
            data = unit if member == 'war3map.w3u' else subprocess.check_output([tool, '-mpq', str(args.base), 'cat', member])
            path = root / str(i)
            path.write_bytes(data)
            cmd += [str(path), member]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        args.output.write_bytes(args.base.read_bytes()[:512] + payload.read_bytes())
    result = dict(base_sha256=hashlib.sha256(args.base.read_bytes()).hexdigest(),
                  map_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
                  unit_sha256=hashlib.sha256(unit).hexdigest(), fields=fields,
                  builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
