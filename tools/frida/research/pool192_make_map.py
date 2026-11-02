#!/usr/bin/env python3
"""Build an isolated 129-unit public point-order/release burst on the shared flat arena."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path
from target021_make_map import build_w3e, build_wpm, build_doo, build_units, build_script, NF, HERE


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for arg in ('base', 'tool', 'output'): p.add_argument('--' + arg, type=Path, required=True)
    a = p.parse_args()
    if a.output.exists(): p.error('output must be new')
    original = a.base.read_bytes()
    if original[:4] != b'HM3W' or original[512:516] != b'MPQ\x1a': p.error('requires original wrapped campaign map')
    tool = str(a.tool.resolve())
    members = subprocess.check_output([tool, '-mpq', str(a.base), 'ls']).decode().splitlines()
    changed = {}
    with tempfile.TemporaryDirectory() as d:
        root = Path(d); payload = root / 'payload.mpq'; command = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members):
            data = subprocess.check_output([tool, '-mpq', str(a.base), 'cat', member])
            new = {'war3map.w3e': build_w3e, 'war3map.doo': build_doo, 'war3mapUnits.doo': build_units,
                   'war3map.shd': lambda _: bytes(NF * NF)}.get(member, lambda d: d)(data)
            if member == 'war3map.wpm': new = build_wpm(False)
            if member == 'war3map.j':
                new = build_script(data.decode().replace('\r\n', '\n'), 'pool192_probe.j', 'rs-pool192.txt', 1, 0).encode()
            if new != data: changed[member] = hashlib.sha256(new).hexdigest()
            file = root / str(i); file.write_bytes(new); command.extend([str(file), member])
        subprocess.run(command, check=True)
        a.output.write_bytes(original[:512] + payload.read_bytes())
    meta = dict(task='ORDER-04.4', base_sha256=hashlib.sha256(original).hexdigest(),
        map_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(), changed_members=changed,
        builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        probe_sha256=hashlib.sha256((HERE / 'pool192_probe.j').read_bytes()).hexdigest(),
        units=129, native_point_orders=258, preload='rs-pool192.txt')
    a.output.with_suffix('.json').write_text(json.dumps(meta, indent=2) + '\n')
    print(json.dumps(meta))


if __name__ == '__main__': main()
