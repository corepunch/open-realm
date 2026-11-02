#!/usr/bin/env python3
"""Add a non-stock, integer-authored separation policy to the removal scene."""
import argparse, hashlib, json, struct, subprocess, tempfile
from pathlib import Path
import sep_research_map as sep


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base', type=Path, required=True)
    p.add_argument('--tool', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.output.exists():
        p.error('output must be new')
    tool = str(a.tool.resolve())
    sep.UNITS = {'hREM': dict(urpo=2, urpp=17, urpg=17, urpr=17)}
    members = subprocess.check_output([tool, '-mpq', str(a.base), 'ls']).decode().splitlines()
    with tempfile.TemporaryDirectory(prefix='removal204-') as temp:
        root = Path(temp)
        payload = root / 'payload.mpq'
        cmd = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(m for m in members if m != '(listfile)'):
            data = subprocess.check_output([tool, '-mpq', str(a.base), 'cat', member])
            if member == 'war3map.w3u':
                # Unit modification tables have the same row grammar in v1/v2.
                # The shared writer validates v1; retain the container's version.
                version = struct.unpack_from('<I', data)[0]
                if version not in (1, 2):
                    raise ValueError('unsupported unit modification version')
                data = data[:4] + sep.w3u_rows(struct.pack('<I', 1) + data[4:])[4:]
                unit_sha = hashlib.sha256(data).hexdigest()
            path = root / str(i)
            path.write_bytes(data)
            cmd += [str(path), member]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        a.output.write_bytes(a.base.read_bytes()[:512] + payload.read_bytes())
    result = dict(base_sha256=hashlib.sha256(a.base.read_bytes()).hexdigest(),
                  map_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),
                  unit_sha256=unit_sha, units=sep.UNITS,
                  builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  schema_builder_sha256=hashlib.sha256(Path(sep.__file__).read_bytes()).hexdigest())
    a.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
