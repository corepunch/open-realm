#!/usr/bin/env python3
"""Build the bounded prepared-water ramp-input diagnostic from the widget arena.

This produces map-start terrain inputs only, not a complete movement fixture.
"""
import argparse,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]

def build(base,output,tool):
    if output.exists():raise ValueError('fresh map output required')
    members=subprocess.check_output([str(tool),'-mpq',str(base),'ls']).decode().splitlines()
    with tempfile.TemporaryDirectory(prefix='wc3-preparedwater-ramp-') as temp:
        temp=Path(temp);packed=temp/'packed.mpq';args=[str(tool),'-mpq',str(packed),'pack']
        for i,name in enumerate(members):
            if name=='(listfile)':continue
            raw=subprocess.check_output([str(tool),'-mpq',str(base),'cat',name])
            if name=='war3map.w3e':
                if raw[:4]!=b'W3E!':raise ValueError('wrong terrain member')
                ng=struct.unpack_from('<I',raw,13)[0];at=17+ng*4
                nc=struct.unpack_from('<I',raw,at)[0];at+=4+nc*4
                if struct.unpack_from('<II',raw,at)!=(17,17):raise ValueError('wrong arena dimensions')
                raw=bytearray(raw[:at+16])
                for y in range(17):
                    for x in range(17):
                        ramp=(x==4 and 6<=y<=10)or(7<=x<=10 and 6<=y<=10)or(x>=13 and y>=8)
                        raw+=struct.pack('<HHBBB',8192,6144,0x40|(0x10 if ramp else 0),0,2+(y>=8))
            path=temp/str(i);path.write_bytes(raw);args.extend([str(path),name])
        subprocess.run(args,check=True,stdout=subprocess.DEVNULL)
        output.write_bytes(base.read_bytes()[:512]+packed.read_bytes())
    return dict(purpose='bounded producer input only; no movement journey acceptance',
        source_map_sha256=hashlib.sha256(base.read_bytes()).hexdigest(),map_sha256=hashlib.sha256(output.read_bytes()).hexdigest())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--mpqtool',type=Path,default=ROOT/'build/bin/mpqtool');a=p.parse_args()
    meta=build(a.base,a.output,a.mpqtool);a.output.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta))
