#!/usr/bin/env python3
"""Build the GROUP-03.2 target-availability arena, including a shadow map sized to its terrain."""
import argparse, hashlib, json, struct, subprocess, sys, tempfile
from pathlib import Path
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        ap.error('output must be new')
    tool = str(ROOT / 'build/bin/mpqtool')
    with tempfile.TemporaryDirectory(prefix='subscribe262-') as tmp:
        temp = Path(tmp)
        raw = temp / 'arena.w3m'
        probe=temp/'subscribe262_probe.j'
        probe.write_text((HERE/'subscribe262_probe.j').read_text())
        subprocess.run([sys.executable, str(HERE / 'group032_make_map.py'), '--base', str(args.base),
            '--probe', str(probe), '--preload-output', 'subscribe262.txt',
            '--task', 'GROUP-03.2',
            '--output', str(raw)], check=True)
        meta = json.loads(raw.with_suffix('.json').read_text())
        members = subprocess.check_output([tool, '-mpq', str(raw), 'ls']).decode().splitlines()
        payload = temp / 'packed.mpq'
        cmd = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members):
            if member == '(listfile)':
                continue
            data = subprocess.check_output([tool, '-mpq', str(raw), 'cat', member])
            if member == 'war3map.w3e':
                data = bytearray(data)
                cliff_at = 17 + struct.unpack_from('<I', data, 13)[0] * 4
                size_at = cliff_at + 4 + struct.unpack_from('<I', data, cliff_at)[0] * 4
                struct.pack_into('<ff', data, size_at + 8, 0, 0)
                data = bytes(data)
                meta['fine_origin'] = [0, 0]
                meta['changed_members'][member] = hashlib.sha256(data).hexdigest()
            if member == 'war3map.shd':
                # The shared passage builder reduces terrain to 16x16 tiles.
                # Its inherited campaign shadow member must also become 64x64.
                data = bytes(64 * 64)
                meta['changed_members'][member] = hashlib.sha256(data).hexdigest()
            path = temp / str(i)
            path.write_bytes(data)
            cmd += [str(path), member]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        args.output.write_bytes(raw.read_bytes()[:512] + payload.read_bytes())
        meta.update(map_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
            shadow_size=4096, probe_template_sha256=hashlib.sha256((HERE/'subscribe262_probe.j').read_bytes()).hexdigest(),
            target_builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        args.output.with_suffix('.json').write_text(json.dumps(meta, indent=2) + '\n')
if __name__ == '__main__':
    main()
