#!/usr/bin/env python3
"""Build the FOOT-04.2 water/cliff/bridge placement and provider lifetime arena."""
import argparse, hashlib, json, struct, subprocess, sys, tempfile
from pathlib import Path
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
import map022_make_maps as support

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        ap.error('output must be new')
    tool = str(ROOT / 'build/bin/mpqtool')
    with tempfile.TemporaryDirectory(prefix='bridge311-') as tmp:
        temp = Path(tmp)
        raw = temp / 'arena.w3m'
        subprocess.run([sys.executable, str(HERE / 'group032_make_map.py'), '--base', str(args.base),
            '--probe', str(HERE / 'bridge311_probe.j'), '--preload-output', 'bridge311.txt',
            '--task', 'FOOT-04.2',
            '--output', str(raw)], check=True)
        meta = json.loads(raw.with_suffix('.json').read_text())
        members = subprocess.check_output([tool, '-mpq', str(raw), 'ls']).decode().splitlines()
        payload = temp / 'packed.mpq'
        cmd = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members):
            if member == '(listfile)':
                continue
            data = subprocess.check_output([tool, '-mpq', str(raw), 'cat', member])
            if member == 'war3map.w3u':
                data=struct.pack('<III',2,0,20)
                for mode,kind in enumerate(['foot','fly','amph','hover','float']):
                    for cls,radius in enumerate([8.,24.,40.,56.]):
                        code=('hQ0'+chr(65+mode*4+cls)).encode()
                        data+=b'hfoo'+code+struct.pack('<I',3)
                        data+=b'ucol'+struct.pack('<If',2,radius)+code
                        data+=b'umvt'+struct.pack('<I',3)+kind.encode()+b'\0'+code
                        data+=b'umvh'+struct.pack('<If',2,60. if mode==1 else 0.)+code
                meta['changed_members'][member]=hashlib.sha256(data).hexdigest()
            if member == 'war3map.w3e':data=support.build_w3e(data)
            if member == 'war3map.wpm':data=support.build_wpm('authored')
            if member == 'war3map.doo':data=support.build_doo(data,0.,b'LT06')
            if member == 'war3map.shd':
                # The arena terrain has64x64 fine cells; match the shadow dimensions.
                data = bytes(64 * 64)
                meta['changed_members'][member] = hashlib.sha256(data).hexdigest()
            if member in ('war3map.w3e','war3map.wpm','war3map.doo'):
                meta['changed_members'][member]=hashlib.sha256(data).hexdigest()
            path = temp / str(i)
            path.write_bytes(data)
            cmd += [str(path), member]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        args.output.write_bytes(raw.read_bytes()[:512] + payload.read_bytes())
        meta.update(map_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
            shadow_size=4096,
            target_builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        args.output.with_suffix('.json').write_text(json.dumps(meta, indent=2) + '\n')
if __name__ == '__main__':
    main()
