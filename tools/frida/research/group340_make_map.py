#!/usr/bin/env python3
"""Replace the Group338 probe with public control-effect ranking scenes."""
import argparse,hashlib,json,subprocess,tempfile,struct
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--probe',type=Path,default=HERE/'group340_probe.j')
    args=parser.parse_args()
    if args.output.exists():parser.error('output must be new')
    tool=ROOT/'build/bin/mpqtool'
    old=(HERE/'group338_probe.j').read_text().replace(
        ' local integer i=0\n call FogEnable',
        ' local integer i=0\n call SetGameDifficulty(MAP_DIFFICULTY_HARD)\n call FogEnable')
    probe=args.probe.read_text()
    changed={}
    with tempfile.TemporaryDirectory(prefix='group340-') as directory:
        temp=Path(directory);payload=temp/'map.mpq'
        command=[str(tool),'-mpq',str(payload),'pack']
        for index,name in enumerate(subprocess.check_output([str(tool),'-mpq',str(args.base),'ls']).decode().splitlines()):
            if name=='(listfile)':continue
            data=subprocess.check_output([str(tool),'-mpq',str(args.base),'cat',name])
            if name=='war3map.w3u':
                assert struct.unpack_from('<III',data)==(2,0,4)
                data=data[:8]+struct.pack('<I',8)+data[12:]
                rows=[]
                for code,minimum in [(b'hM40',120.),(b'hM41',0.),(b'hM42',600.)]:
                    rows.append((code,[(b'uaen',0,1),(b'ua1r',0,500),(b'ua1g',3,'ground,air'),(b'ua1w',3,'normal'),(b'uamn',2,minimum),(b'ua1b',0,10)]))
                rows.append((b'hF40',[(b'umvt',3,'fly'),(b'umvh',2,150.)]))
                for code,mods in rows:
                    data+=b'hfoo'+code+struct.pack('<I',len(mods))
                    for field,kind,value in mods:
                        data+=field+struct.pack('<I',kind)
                        data+=value.encode()+b'\0' if kind==3 else struct.pack('<f' if kind==2 else '<I',value)
                        data+=code
                changed[name]=hashlib.sha256(data).hexdigest()
            if name=='war3map.j':
                script=data.decode();old_functions=old[old.index('function G338Mark'):]
                assert script.count(old_functions)==1
                script=script.replace(old_functions,probe[probe.index('function G338Mark'):])
                assert script.count(' integer udg_G338Tick=0')==1
                script=script.replace(' integer udg_G338Tick=0',' unit udg_G338Caster=null\n integer udg_G338Tick=0')
                data=script.encode();changed[name]=hashlib.sha256(data).hexdigest()
            path=temp/str(index);path.write_bytes(data);command.extend([str(path),name])
        subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
        args.output.write_bytes(args.base.read_bytes()[:512]+payload.read_bytes())
    sources={path.name:hashlib.sha256(path.read_bytes()).hexdigest()for path in
        (Path(__file__),args.probe,HERE/'group338_probe.j')}
    meta=dict(base_sha256=hashlib.sha256(args.base.read_bytes()).hexdigest(),
        map_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),changed_members=changed,sources=sources)
    args.output.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n')
    print(meta['map_sha256'])

if __name__=='__main__':main()
