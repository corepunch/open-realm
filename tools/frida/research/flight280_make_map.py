#!/usr/bin/env python3
"""Build the FLYING-SPELL queued-target arena, including a shadow map sized to its terrain."""
import argparse, hashlib, json, struct, subprocess, sys, tempfile
from pathlib import Path
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--probe', type=Path, default=HERE / 'flight280_probe.j')
    args = ap.parse_args()
    if args.output.exists():
        ap.error('output must be new')
    tool = str(ROOT / 'build/bin/mpqtool')
    with tempfile.TemporaryDirectory(prefix='flight280-') as tmp:
        temp = Path(tmp)
        raw = temp / 'arena.w3m'
        subprocess.run([sys.executable, str(HERE / 'group032_make_map.py'), '--base', str(args.base),
            '--probe', str(args.probe), '--preload-output', 'rs-flight280.txt',
            '--task', 'FLYING-SPELL', '--replace', 'START=rs-flight280-start.txt',
            '--output', str(raw)], check=True)
        meta = json.loads(raw.with_suffix('.json').read_text())
        members = subprocess.check_output([tool, '-mpq', str(raw), 'ls']).decode().splitlines()
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
            if member == 'war3map.w3a':
                if struct.unpack_from('<II', data) != (2, 0):
                    raise ValueError('expected an empty original ability table')
                # Non-stock unit/point ranges share one ordinary flying caster.
                mods = [(b'aran', 2, 1, 0, struct.pack('<f', 333.0)),
                        (b'amcs', 0, 1, 0, struct.pack('<i', 13)),
                        (b'acdn', 2, 1, 0, struct.pack('<f', 7.0)),
                        (b'aher', 0, 0, 0, struct.pack('<i', 0))]
                entries=[]
                for original,alias,extra in [(b'Ainf',b'A280',[(b'atar',3,1,0,b'air,ground,structure,friend,self,vuln,invu\0')]),
                                              (b'AHfs',b'A281',[])]:
                    rows=mods+extra;custom=original+alias+struct.pack('<I',len(rows))
                    for key,kind,level,column,value in rows:
                        custom+=key+struct.pack('<III',kind,level,column)+value+alias
                    entries.append(custom)
                data=struct.pack('<III',2,0,len(entries))+b''.join(entries)
                meta['changed_members'][member] = hashlib.sha256(data).hexdigest()
            path = temp / str(i)
            path.write_bytes(data)
            cmd += [str(path), member]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        args.output.write_bytes(raw.read_bytes()[:512] + payload.read_bytes())
        meta.update(map_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
            shadow_size=4096, spell_range=333.0,
            target_builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        args.output.with_suffix('.json').write_text(json.dumps(meta, indent=2) + '\n')
if __name__ == '__main__':
    main()
