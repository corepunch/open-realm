#!/usr/bin/env python3
"""Keep the frozen reacquisition arena; resize only its inherited shadow member.

The old map's campaign-sized shadow map opens an Invalid shadow map file dialog
in retail. The script, terrain, pathing and object data remain byte-identical.
"""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

SHA = 'fd5c2b525675fe4b5f1f550304a335ab01ff8c2034018cb959aa50c780c58ca1'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base', type=Path, required=True)
    p.add_argument('--tool', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.output.exists():
        p.error('new output required')
    raw = a.base.read_bytes()
    if hashlib.sha256(raw).hexdigest() != SHA:
        raise ValueError('frozen source map differs')
    tool = str(a.tool.resolve())
    names = subprocess.check_output([tool, '-mpq', str(a.base), 'ls']).decode().splitlines()
    hashes = {}
    with tempfile.TemporaryDirectory(prefix='target252-') as tmp:
        root = Path(tmp)
        command = [tool, '-mpq', str(root / 'payload.mpq'), 'pack']
        for i, name in enumerate(names):
            if name == '(listfile)':
                continue
            data = subprocess.check_output([tool, '-mpq', str(a.base), 'cat', name])
            if name.lower() == 'war3map.shd':
                data = bytes(64 * 64)
            hashes[name] = hashlib.sha256(data).hexdigest()
            f = root / str(i)
            f.write_bytes(data)
            command += [str(f), name]
        subprocess.run(command, check=True)
        a.output.write_bytes(raw[:512] + (root / 'payload.mpq').read_bytes())
    a.output.with_suffix('.json').write_text(json.dumps(dict(base_sha256=SHA,
        sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(), member_sha256=hashes), indent=2) + '\n')

if __name__ == '__main__':
    main()
