#!/usr/bin/env python3
"""Build the ROUTE-04.1 cache and index arena, including a shadow map sized to its terrain."""
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
    with tempfile.TemporaryDirectory(prefix='route336-') as tmp:
        temp = Path(tmp)
        raw = temp / 'arena.w3m'
        subprocess.run([sys.executable, str(HERE / 'group032_make_map.py'), '--base', str(args.base),
            '--scenario', 'fine_results', '--probe', str(HERE / 'route336_probe.j'), '--preload-output', 'route336.txt',
            '--task', 'ROUTE-04.1',
            '--output', str(raw)], check=True)
        meta = json.loads(raw.with_suffix('.json').read_text())
        members = subprocess.check_output([tool, '-mpq', str(raw), 'ls']).decode().splitlines()
        def unit_objects():
            data = struct.pack('<III', 1, 0, 8)
            for base, prefix in ((b'hgry', b'hF9'), (b'ehip', b'hA9')):
                for i, radius in enumerate((8,24,40,56)):
                    code=prefix+bytes([49+i])
                    data += base+code+struct.pack('<I',2)
                    data += b'ucol'+struct.pack('<If',2,radius)+code
                    data += b'umvs'+struct.pack('<II',0,270)+code
            return data
        def ability_objects():
            data=struct.pack('<III',2,0,4)
            for i in range(4):
                code=b'AF9'+bytes([49+i]); target=b'hA9'+bytes([49+i])
                data += b'Sca1'+code+struct.pack('<I',2)
                data += b'Cha1'+struct.pack('<III',3,1,0)+target+b'\0'+code
                data += b'areq'+struct.pack('<III',3,0,0)+b'\0'+code
            return data
        payload = temp / 'packed.mpq'
        cmd = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members):
            if member == '(listfile)':
                continue
            data = subprocess.check_output([tool, '-mpq', str(raw), 'cat', member])
            if member == 'war3map.shd':
                # The shared passage builder reduces terrain to 16x16 tiles.
                # Its inherited campaign shadow member must also become 64x64.
                data = bytes(64 * 64)
                meta['changed_members'][member] = hashlib.sha256(data).hexdigest()
            if member == 'war3map.w3u':
                data = unit_objects()
            if member == 'war3map.w3a':
                data = ability_objects()
            meta['changed_members'][member] = hashlib.sha256(data).hexdigest()
            path = temp / str(i)
            path.write_bytes(data)
            cmd += [str(path), member]
        if 'war3map.w3a' not in members:
            path=temp/'abilities'; path.write_bytes(ability_objects())
            cmd += [str(path),'war3map.w3a']
            meta['changed_members']['war3map.w3a']=hashlib.sha256(path.read_bytes()).hexdigest()
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        args.output.write_bytes(raw.read_bytes()[:512] + payload.read_bytes())
        meta.update(map_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
            shadow_size=4096,
            target_builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        args.output.with_suffix('.json').write_text(json.dumps(meta, indent=2) + '\n')
if __name__ == '__main__':
    main()
