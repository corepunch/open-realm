#!/usr/bin/env python3
"""Test paused refresh and independent Sleep clocks."""
import argparse,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def sha(b):return hashlib.sha256(b).hexdigest()
def abilities():
    alias=b'A36r';mods=[(b'areq',3,0,b'\0'),(b'amcs',0,1,struct.pack('<i',0)),(b'acdn',2,1,struct.pack('<f',0)),(b'adur',2,1,struct.pack('<f',3)),(b'ahdu',2,1,struct.pack('<f',3)),(b'Usl1',2,1,struct.pack('<f',.5))]
    out=struct.pack('<III',2,0,1)+b'AUsl'+alias+struct.pack('<I',len(mods))
    for field,kind,level,value in mods:out+=field+struct.pack('<III',kind,level,0)+value+alias
    return out

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():p.error('fresh output required')
    tool=ROOT/'build/bin/mpqtool';probe=HERE/'group367_refresh_probe.j'
    with tempfile.TemporaryDirectory(prefix='group367-refresh-')as d:
        d=Path(d);middle=d/'probe.w3m'
        subprocess.run(['python3',str(HERE/'group341_make_map.py'),'--base',str(a.base),'--output',str(middle),'--probe',str(probe)],check=True,stdout=subprocess.DEVNULL)
        names=subprocess.check_output([str(tool),'-mpq',str(middle),'ls']).decode().splitlines()
        if 'war3map.w3a'not in names:names.append('war3map.w3a')
        payload=d/'payload.mpq';command=[str(tool),'-mpq',str(payload),'pack'];members={}
        for i,name in enumerate(names):
            if name=='(listfile)':continue
            data=abilities()if name=='war3map.w3a'else subprocess.check_output([str(tool),'-mpq',str(middle),'cat',name])
            path=d/str(i);path.write_bytes(data);command.extend([str(path),name]);members[name]=sha(data)
        subprocess.run(command,check=True,stdout=subprocess.DEVNULL);a.output.write_bytes(a.base.read_bytes()[:512]+payload.read_bytes())
    a.output.with_suffix('.json').write_text(json.dumps(dict(task='GROUP-03.2',base_sha256=sha(a.base.read_bytes()),map_sha256=sha(a.output.read_bytes()),members=members,probe_sha256=sha(probe.read_bytes()),builder_sha256=sha(Path(__file__).read_bytes())),indent=2)+'\n')
if __name__=='__main__':main()
