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
    ap.add_argument('--operation', choices=['all','create'], default='all')
    ap.add_argument('--scenes', default=','.join(map(str, range(24))), help='public scene indices, comma separated')
    args = ap.parse_args()
    scenes=[int(x) for x in args.scenes.split(',')]
    if not scenes or len(set(scenes))!=len(scenes) or any(x<0 or x>=24 for x in scenes):
        ap.error('scenes must be unique indices in0..23')
    if args.output.exists():
        ap.error('output must be new')
    tool = str(ROOT / 'build/bin/mpqtool')
    with tempfile.TemporaryDirectory(prefix='nonfinite313-') as tmp:
        temp = Path(tmp)
        raw = temp / 'arena.w3m'
        probe=temp/'probe.j'
        source=(HERE/'nonfinite313_probe.j').read_text()
        choices='\n'.join((' if' if i==0 else ' elseif')+f' udg_N313Tick=={20+i} then\n  set udg_N313Scene={scene}' for i,scene in enumerate(scenes))+'\n endif'
        source=source.replace(' if udg_N313Tick>=20 and udg_N313Tick<=43 then\n  set udg_N313Scene=udg_N313Tick-20',choices+'\n if udg_N313Tick>=20 and udg_N313Tick<='+str(19+len(scenes))+' then')
        source=source.replace('udg_N313Tick==44','udg_N313Tick=='+str(20+len(scenes)))
        if args.operation=='create':
            begin=source.index(' call SetUnitX')
            end=source.index(' call RemoveUnit',begin)
            source=source[:begin]+""" call RemoveUnit(udg_N313Unit)
 set udg_N313Unit=CreateUnit(Player(0),'hOAA'+udg_N313Scene,udg_N313Source,udg_N313Source,0)
 call SetUnitAcquireRange(udg_N313Unit,0)
 call N313Mark("created-source")
"""+source[end:]
        probe.write_text(source)
        subprocess.run([sys.executable, str(HERE / 'group032_make_map.py'), '--base', str(args.base),
            '--probe', str(probe), '--preload-output', 'nonfinite313.txt',
            '--task', 'NUM-03.2',
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
                words=[0x7fc00001,0x7fa00001,0xffc00001,0x7f800000,0xff800000,0x7f7fffff,0xff7fffff,0]
                data=struct.pack('<III',2,0,24)
                for family in range(3):
                    for cls,word in enumerate(words):
                        code=struct.pack('>I',int.from_bytes(b'hOAA','big')+family*8+cls)
                        radius=word if family!=1 else 0x41f80000
                        height=word if family else 0
                        data+=b'hfoo'+code+struct.pack('<I',3)
                        data+=b'ucol'+struct.pack('<II',2,radius)+code
                        data+=b'umvt'+struct.pack('<I',3)+b'foot\0'+code
                        # Authored umvh is unreal2 (BASE-02.1). Preserve the raw sNaN word.
                        data+=b'umvh'+struct.pack('<II',2,height)+code
                meta['changed_members'][member]=hashlib.sha256(data).hexdigest()
            if member == 'war3map.w3e':
                data=bytearray(support.build_w3e(data))
                for n in range(17*17):struct.pack_into('<HHBBB',data,len(data)-17*17*7+n*7,8192,8550,0,0,2)
                data=bytes(data)
            if member == 'war3map.wpm':data=b'MP3W'+struct.pack('<III',0,64,64)+bytes([0x40])*4096
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
            shadow_size=4096, scenes=scenes, operation=args.operation, producer_sha256=hashlib.sha256(source.encode()).hexdigest(),
            target_builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        args.output.with_suffix('.json').write_text(json.dumps(meta, indent=2) + '\n')
if __name__ == '__main__':
    main()
