#!/usr/bin/env python3
"""Build the NUM-03.2 nonfinite coordinate/radius producer arena."""
import argparse, hashlib, json, struct, subprocess, sys, tempfile
from pathlib import Path
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
import map022_make_maps as support

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--scene', type=int, default=-1)
    ap.add_argument('--movement-type',choices=['foot','fly','amph','hover','float'],default='foot')
    args = ap.parse_args()
    if not -1<=args.scene<8:ap.error('scene -1(all) or0..7 required')
    if args.output.exists():
        ap.error('output must be new')
    tool = str(ROOT / 'build/bin/mpqtool')
    with tempfile.TemporaryDirectory(prefix='captain320-') as tmp:
        temp = Path(tmp)
        raw = temp / 'arena.w3m'
        probe=temp/'probe.j'
        source=(HERE/'captain320_probe.j').read_text()
        source=source.replace('@FIRST@',str(max(0,args.scene))).replace('@COUNT@','8'if args.scene<0 else'1')
        probe.write_text(source)
        subprocess.run([sys.executable, str(HERE / 'group032_make_map.py'), '--base', str(args.base),
            '--probe', str(probe), '--preload-output', 'captain320.txt',
            '--task', 'NUM-03.2',
            '--output', str(raw)], check=True)
        meta = json.loads(raw.with_suffix('.json').read_text())
        members = subprocess.check_output([tool, '-mpq', str(raw), 'ls']).decode().splitlines()
        payload = temp / 'packed.mpq'
        cmd = [tool, '-mpq', str(payload), 'pack']
        for i, member in enumerate(members+[r'Scripts\captain320.ai']):
            if member == '(listfile)':
                continue
            data = (HERE/'captain320_probe.ai').read_bytes() if member==r'Scripts\captain320.ai' else subprocess.check_output([tool, '-mpq', str(raw), 'cat', member])
            if member==r'Scripts\captain320.ai':meta['changed_members'][member]=hashlib.sha256(data).hexdigest()
            if member=='war3map.j':
                data=data.replace(b'SetPlayerController( Player(0), MAP_CONTROL_NEUTRAL )',b'SetPlayerController( Player(0), MAP_CONTROL_COMPUTER )')
                meta['changed_members'][member]=hashlib.sha256(data).hexdigest()
            if member == 'war3map.w3u':
                words=[0x7fc00001,0x7fa00001,0xffc00001,0x7f800000,0xff800000,0x7f7fffff,0xff7fffff,0]
                data=struct.pack('<III',2,0,9)
                for cls,word in enumerate(words):
                    code=struct.pack('>I',int.from_bytes(b'hSAA','big')+cls)
                    data+=b'hfoo'+code+struct.pack('<I',5)
                    data+=b'ucol'+struct.pack('<If',2,31.)+code
                    data+=b'umvt'+struct.pack('<I',3)+b'foot\0'+code
                    data+=b'umvh'+struct.pack('<II',2,word)+code
                    data+=b'umis'+struct.pack('<Ii',0,173)+code
                    data+=b'umas'+struct.pack('<Ii',0,389)+code
                code=b'hTAA'
                data+=b'hfoo'+code+struct.pack('<I',5)
                data+=b'ucol'+struct.pack('<If',2,31.)+code
                data+=b'umvt'+struct.pack('<I',3)+args.movement_type.encode()+b'\0'+code
                data+=b'umvh'+struct.pack('<If',2,60. if args.movement_type=='fly' else 0.)+code
                data+=b'umis'+struct.pack('<Ii',0,173)+code
                data+=b'umas'+struct.pack('<Ii',0,389)+code
                meta['changed_members'][member]=hashlib.sha256(data).hexdigest()
            if member == 'war3map.w3e':
                data=bytearray(support.build_w3e(data))
                for n in range(17*17):struct.pack_into('<HHBBB',data,len(data)-17*17*7+n*7,support.SHALLOW_H if args.movement_type=='float' else 8192,8550,0x40 if args.movement_type=='float' else 0,0,2)
                data=bytes(data)
            if member == 'war3map.wpm':data=b'MP3W'+struct.pack('<III',0,64,64)+bytes([0x08 if args.movement_type=='float' else 0x40])*4096
            if member == 'war3map.doo':data=data[:12]+struct.pack('<III',0,0,0)
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
            shadow_size=4096, scene=args.scene, movement_type=args.movement_type, producer_sha256=hashlib.sha256(source.encode()).hexdigest(),
            target_builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        args.output.with_suffix('.json').write_text(json.dumps(meta, indent=2) + '\n')
if __name__ == '__main__':
    main()
