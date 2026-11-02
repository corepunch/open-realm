#!/usr/bin/env python3
"""Build an ORDER-01.12 Harvest arena: the shared flat passage map plus a 64x64 shadow member."""
import argparse, hashlib, json, subprocess, sys, tempfile
from pathlib import Path
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--tool', type=Path, default=ROOT / 'build/bin/mpqtool')
    ap.add_argument('--probe', type=Path, default=HERE / 'harvest012_probe.j')
    ap.add_argument('--preload-output', default='rs-h012.txt')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        ap.error('output must be new')
    tool = str(args.tool.resolve())
    with tempfile.TemporaryDirectory(prefix='harvest012-') as tmp:
        temp = Path(tmp)
        raw = temp / 'arena.w3m'
        subprocess.run([sys.executable, str(HERE / 'group032_make_map.py'), '--base', str(args.base), '--tool', tool,
            '--probe', str(args.probe), '--preload-output', args.preload_output,
            '--task', 'ORDER-01.12', '--wpm-blocked', '[]', '--output', str(raw)], check=True)
        meta = json.loads(raw.with_suffix('.json').read_text())
        members = subprocess.check_output([tool, '-mpq', str(raw), 'ls']).decode().splitlines()
        payload = temp / 'packed.mpq'
        cmd = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members):
            if member == '(listfile)':
                continue
            data = subprocess.check_output([tool, '-mpq', str(raw), 'cat', member])
            if member == 'war3map.shd':
                # The shared passage builder reduces terrain to 16x16 tiles;
                # its inherited campaign shadow member must become 64x64 too.
                data = bytes(64 * 64)
                meta['changed_members'][member] = hashlib.sha256(data).hexdigest()
            path = temp / str(i)
            path.write_bytes(data)
            cmd += [str(path), member]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        args.output.write_bytes(raw.read_bytes()[:512] + payload.read_bytes())
        meta.update(map_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(), shadow_size=4096,
            target_builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        args.output.with_suffix('.json').write_text(json.dumps(meta, indent=2) + '\n')

if __name__ == '__main__':
    main()
