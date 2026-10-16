#!/usr/bin/env python3
"""ORDER-01.10/01.18 research map builder.

Wraps the unmodified tools/frida/make_wc3_pathfinding_map.py: it runs the flat
Patrol passage scenario (patrol_lifetime / patrol_queue terrain, controllers and
camera bounds) but substitutes the research probe source for the stock probe.
Nothing in the shared builder is modified; only its probe read is redirected.

  order0110_make_map.py --probe order0110_probe.j --variant a \
      --base runtime/Human02Interlude-original.w3m --output RS-ORDER-01.10-a.w3m
"""
import argparse, hashlib, json, pathlib, sys

HERE = pathlib.Path(__file__).resolve().parent
FRIDA = HERE.parent
STOCK = {'patrol_lifetime': 'wc3_patrol_lifetime_probe.j', 'patrol_queue': 'wc3_patrol_queue_probe.j'}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--probe', required=True, help='research probe file in tools/frida/research')
    ap.add_argument('--terrain', choices=sorted(STOCK), default='patrol_lifetime')
    ap.add_argument('--variant', required=True)
    ap.add_argument('--base', type=pathlib.Path, required=True)
    ap.add_argument('--tool', type=pathlib.Path, default=FRIDA.parents[1] / 'build/bin/mpqtool')
    ap.add_argument('--output', type=pathlib.Path, required=True)
    args = ap.parse_args()
    probe_path = (HERE / args.probe).resolve()
    if probe_path.parent != HERE or not probe_path.is_file():
        ap.error('probe must be a research probe beside this script')
    probe = probe_path.read_text().replace('@VARIANT@', args.variant)
    stock_name = STOCK[args.terrain]
    original = pathlib.Path.read_text

    def read_text(self, *a, **k):
        if self.name == stock_name and self.parent.resolve() == FRIDA:
            return probe
        return original(self, *a, **k)

    pathlib.Path.read_text = read_text
    sys.path.insert(0, str(FRIDA))
    import make_wc3_pathfinding_map as builder
    sys.argv = ['make_wc3_pathfinding_map.py', '--base', str(args.base), '--tool', str(args.tool),
                '--scenario', args.terrain, '--output', str(args.output)]
    builder.main()
    pathlib.Path.read_text = original
    meta = json.loads(args.output.with_suffix('.json').read_text())
    meta['research_probe'] = {'file': probe_path.name, 'sha256': hashlib.sha256(probe_path.read_bytes()).hexdigest(),
                              'variant': args.variant, 'terrain_scenario': args.terrain,
                              'wrapper_sha256': hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
                              'builder_sha256': hashlib.sha256((FRIDA / 'make_wc3_pathfinding_map.py').read_bytes()).hexdigest()}
    j = args.output.with_suffix('.j').read_text()
    if probe.split('\n', 1)[1][:200].strip() and 'udg_PathProbeTimer' not in j:
        raise SystemExit('probe substitution failed')
    args.output.with_suffix('.json').write_text(json.dumps(meta, indent=2) + '\n')
    print(json.dumps(meta['research_probe']), meta['map_sha256'])


if __name__ == '__main__':
    main()
