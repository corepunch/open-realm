#!/usr/bin/env python3
"""Build one ORDER-03 research map from the original Human02Interlude copy.

Only war3map.j changes: order03_probe.j is injected through the unchanged
make_wc3_pathfinding_map.instrument() (imported). Run from the worktree root.
"""
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import make_wc3_pathfinding_map as base  # noqa: E402

SCENARIOS = {'forward': 1, 'reverse': 2}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--tool', type=Path, required=True)
    ap.add_argument('--scenario', choices=SCENARIOS, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        ap.error('output must be new')
    original = args.base.read_bytes()
    if original[:4] != b'HM3W' or original[512:516] != b'MPQ\x1a':
        ap.error('requires the original wrapped campaign map')
    probe_path = Path(__file__).with_name('order03_probe.j')
    probe = probe_path.read_text().replace('@RS_SCENARIO@', str(SCENARIOS[args.scenario])).replace('@RS_NAME@', args.scenario)
    tool = str(args.tool.resolve())
    members = subprocess.check_output([tool, '-mpq', str(args.base), 'ls']).decode().splitlines()
    if members.count('war3map.j') != 1:
        ap.error('source must have exactly one war3map.j')
    with tempfile.TemporaryDirectory(prefix='rs-order03-map-') as temp:
        root = Path(temp)
        payload = root / 'payload.mpq'
        command = [tool, '-mpq', str(payload), 'pack']
        script = None
        for i, member in enumerate(members):
            data = subprocess.check_output([tool, '-mpq', str(args.base), 'cat', member])
            if member == 'war3map.j':
                source = data.decode('utf-8').replace('\r\n', '\n')
                data = base.instrument(source, probe, 'open').encode('utf-8')
                script = data
            path = root / str(i)
            path.write_bytes(data)
            command.extend([str(path), member])
        subprocess.run(command, check=True)
        args.output.write_bytes(original[:512] + payload.read_bytes())
    args.output.with_suffix('.j').write_bytes(script)
    h = lambda b: hashlib.sha256(b).hexdigest()
    result = dict(tasks=['ORDER-03.1', 'ORDER-03.2'], scenario=args.scenario, scenario_id=SCENARIOS[args.scenario],
                  base_sha256=h(original), map_sha256=h(args.output.read_bytes()), j_sha256=h(script),
                  probe_sha256=h(probe_path.read_bytes()), builder_sha256=h(Path(__file__).read_bytes()),
                  imported_builder_sha256=h(Path(base.__file__).read_bytes()), changed_members=['war3map.j'],
                  container='rebuilt MPQ with original HM3W header; signature not retained')
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
