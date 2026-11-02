#!/usr/bin/env python3
"""Build Holy Light/target-Move comparison with a correctly resized shadow map.

Shared arena encoders retain the original W3E/WPM conventions. The SHD must
have one byte per fine cell; keeping the campaign SHD causes a retail load error.
"""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path
from target021_make_map import build_w3e, build_wpm, build_doo, build_units, build_script, HERE, NF, FLOOR, WALL, WALL_X, WALL_Y
PROBES = {'completion194': ('completion194_probe.j', 'rs-completion194.txt', 1)}

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--tool', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--probe', choices=sorted(PROBES), default='completion194')
    ap.add_argument('--scenes', type=int, help='scene count substituted for @SCENES@')
    ap.add_argument('--variant', type=int, default=0, help='integer substituted for @VARIANT@')
    ap.add_argument('--no-wall', action='store_true')
    args = ap.parse_args()
    if args.output.exists():
        ap.error('output must be a new file')
    probe_name, output, scenes = PROBES[args.probe]
    scenes = args.scenes if args.scenes is not None else scenes
    original = args.base.read_bytes()
    if original[:4] != b'HM3W' or original[512:516] != b'MPQ\x1a':
        ap.error('requires the original 512-byte wrapped campaign map')
    tool = str(args.tool.resolve())
    members = subprocess.check_output([tool, '-mpq', str(args.base), 'ls']).decode().splitlines()
    changed = {}
    with tempfile.TemporaryDirectory(prefix='target021-') as temp:
        root = Path(temp)
        payload = root / 'payload.mpq'
        command = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members):
            data = subprocess.check_output([tool, '-mpq', str(args.base), 'cat', member])
            new = {'war3map.w3e': build_w3e, 'war3map.doo': build_doo, 'war3mapUnits.doo': build_units, 'war3map.shd': lambda _: bytes(NF * NF)}.get(
                member, lambda d: d)(data)
            if member == 'war3map.wpm':
                new = build_wpm(not args.no_wall)
            if member == 'war3map.j':
                new = build_script(data.decode('utf-8').replace('\r\n', '\n'), probe_name, output, scenes,
                                   args.variant).encode('utf-8')
            if new != data:
                changed[member] = hashlib.sha256(new).hexdigest()
                args.output.with_suffix('.' + member.replace('war3map', '').strip('.').replace('.', '_')).write_bytes(new)
            path = root / str(i)
            path.write_bytes(new)
            command.extend([str(path), member])
        subprocess.run(command, check=True)
        args.output.write_bytes(original[:512] + payload.read_bytes())
    result = dict(task='SCHED-02.4 / payoff194', probe=probe_name, scenes=scenes, variant=args.variant,
                  base_sha256=hashlib.sha256(original).hexdigest(),
                  map_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(), members=members,
                  changed_members=changed, preload_output=output,
                  builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  probe_sha256=hashlib.sha256((HERE / probe_name).read_bytes()).hexdigest(),
                  wall=None if args.no_wall else dict(fine_x=[WALL_X.start, WALL_X.stop - 1], fine_y=[WALL_Y.start, WALL_Y.stop - 1], wpm=WALL),
                  floor_wpm=FLOOR, fine_origin=[0.0, 0.0], fine_cell_world=32.0,
                  container='rebuilt MPQ with original HM3W header; signature not retained')
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps({k: result[k] for k in ('map_sha256', 'changed_members')}, indent=1))


if __name__ == '__main__':
    main()
