#!/usr/bin/env python3
"""GROUP-03.2 / FORM-01.3 / FORM-05 research map builder (new file; shared builders are not modified).

Runs the unmodified tools/frida/make_wc3_pathfinding_map.py for an existing flat formation scenario
(default formation_policy: 64x64 flat fine grid, rank clones hF00..hF03, Chaos AF03), then repacks that
map with war3map.j produced by the same builder's instrument() from the base campaign script and a
research probe (default group032_probe.j). Optional --wpm-blocked replaces war3map.wpm through the
builder's own passage_map_member() with the given blocked base cells (2x2 fine cells each).
"""
import argparse, hashlib, importlib.util, json, subprocess, sys, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def builder():
    spec = importlib.util.spec_from_file_location('wc3map', HERE.parent / 'make_wc3_pathfinding_map.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--tool', type=Path, default=ROOT / 'build/bin/mpqtool')
    ap.add_argument('--scenario', default='formation_policy')
    ap.add_argument('--probe', type=Path, default=HERE / 'group032_probe.j')
    ap.add_argument('--preload-output', required=True, help='PreloadGenEnd file name (replaces @OUTPUT@)')
    ap.add_argument('--wpm-blocked', default=None, help='JSON list of [x,y] blocked base cells (32-world-unit)')
    ap.add_argument('--task', default='GROUP-03.2')
    ap.add_argument('--replace', action='append', default=[], help='KEY=VALUE probe placeholder (@KEY@) replacement')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        ap.error('output must be a new file')
    mod = builder()
    tool = str(args.tool.resolve())
    original = args.base.read_bytes()
    probe = args.probe.read_text().replace('@OUTPUT@', args.preload_output)
    for item in args.replace:
        key, value = item.split('=', 1)
        probe = probe.replace('@' + key + '@', value)
    with tempfile.TemporaryDirectory(prefix='group032-') as temp:
        root = Path(temp)
        stage = root / 'stage.w3m'
        subprocess.run([sys.executable, str(HERE.parent / 'make_wc3_pathfinding_map.py'), '--base', str(args.base), '--tool', tool,
                        '--scenario', args.scenario, '--output', str(stage)], check=True, stdout=subprocess.DEVNULL)
        stage_meta = json.loads(stage.with_suffix('.json').read_text())
        members = [m for m in subprocess.check_output([tool, '-mpq', str(stage), 'ls']).decode().splitlines() if m != '(listfile)']
        source = subprocess.check_output([tool, '-mpq', str(args.base), 'cat', 'war3map.j']).decode('utf-8').replace('\r\n', '\n')
        script = mod.instrument(source, probe, args.scenario).encode('utf-8')
        blocked = json.loads(args.wpm_blocked) if args.wpm_blocked else None
        payload = root / 'payload.mpq'
        command = [tool, '-mpq', str(payload), 'pack']
        changed = {}
        for i, member in enumerate(members):
            data = subprocess.check_output([tool, '-mpq', str(stage), 'cat', member])
            if member == 'war3map.j':
                data = script
            if member == 'war3map.wpm' and blocked is not None:
                data = mod.passage_map_member('war3map.wpm', data, [tuple(c) for c in blocked])
            if member in ('war3map.j', 'war3map.wpm'):
                changed[member] = hashlib.sha256(data).hexdigest()
                args.output.with_suffix('.' + member.split('.')[-1]).write_bytes(data)
            p = root / str(i)
            p.write_bytes(data)
            command += [str(p), member]
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
        args.output.write_bytes(original[:512] + payload.read_bytes())
    result = dict(task=args.task, base_sha256=hashlib.sha256(original).hexdigest(), scenario=args.scenario,
                  stage_map_sha256=stage_meta['map_sha256'], map_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
                  members=members, changed_members=changed, wpm_blocked=blocked, preload_output=args.preload_output,
                  builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  shared_builder_sha256=hashlib.sha256((HERE.parent / 'make_wc3_pathfinding_map.py').read_bytes()).hexdigest(),
                  probe=args.probe.name, replacements=args.replace, probe_sha256=hashlib.sha256(args.probe.read_bytes()).hexdigest(),
                  fine_origin=[0.0, 0.0], fine_cell_world=32.0,
                  container='rebuilt MPQ with original HM3W header; signature not retained')
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps({k: result[k] for k in ('map_sha256', 'changed_members')}, indent=1))


if __name__ == '__main__':
    main()
