#!/usr/bin/env python3
"""Prepare enemy-viewer common Morph availability controls; no retail behavior assertion."""
import argparse,hashlib,json,struct,subprocess,sys,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
PARENT=HERE

def sha(raw):return hashlib.sha256(raw).hexdigest()
def ability_objects():
    codes=[b'AEme',b'Apiv',b'AUsl'];out=struct.pack('<II',2,len(codes))
    for code in codes:
        mods=[(b'aher',0,0,struct.pack('<i',0)),(b'areq',3,0,b'\0'),
              (b'amcs',0,1,struct.pack('<i',0)),(b'acdn',2,1,struct.pack('<f',0)),
              (b'adur',2,1,struct.pack('<f',0.625 if code==b'Apiv'else 6.0 if code==b'AUsl'else 0.375)),
              (b'ahdu',2,1,struct.pack('<f',2.5 if code==b'AEme'else 6.0 if code==b'AUsl'else 0))]
        out+=code+b'\0'*4+struct.pack('<I',len(mods))
        for field,kind,level,value in mods:
            out+=field+struct.pack('<III',kind,level,0)+value+b'\0'*4
    return out+struct.pack('<I',0)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--mpqtool',type=Path,required=True)
    p.add_argument('--alternate-disable',choices=['0','1'],required=True)
    a=p.parse_args()
    if a.output.exists():p.error('fresh output required')
    source=(HERE/'payoff382u_probe.j').read_bytes()
    probe=source.replace(b'integer udg_P382USetting=0',('integer udg_P382USetting='+a.alternate_disable).encode())
    with tempfile.TemporaryDirectory(prefix='payoff382-')as tmp:
        tmp=Path(tmp);probe_path=tmp/'probe.j';probe_path.write_bytes(probe);intermediate=tmp/'input.w3m'
        subprocess.run([sys.executable,str(PARENT/'morph330_make_map.py'),'--base',str(a.base),
            '--output',str(intermediate),'--mpqtool',str(a.mpqtool),'--probe',str(probe_path),'--empty-ability-units','Edem','Edmm','Udre'],check=True,stdout=subprocess.DEVNULL)
        def cat(name):return subprocess.check_output([str(a.mpqtool),'-mpq',str(intermediate),'cat',name])
        names=[n for n in cat('(listfile)').decode().splitlines()if n!='(listfile)']
        if 'war3mapMisc.txt'in names:raise ValueError('base unexpectedly authors Misc; inspect before replacing')
        names.append('war3mapMisc.txt')
        packed=tmp/'payload.mpq';command=[str(a.mpqtool),'-mpq',str(packed),'pack'];members={}
        for i,name in enumerate(names):
            if name=='war3mapMisc.txt':data=('[Misc]\nMorphAlternateDisable='+a.alternate_disable+'\n').encode()
            elif name=='war3map.w3a':data=ability_objects()
            else:data=cat(name)
            path=tmp/str(i);path.write_bytes(data);command.extend([str(path),name]);members[name]=sha(data)
        subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
        a.output.write_bytes(intermediate.read_bytes()[:512]+packed.read_bytes())
    a.output.with_suffix('.json').write_text(json.dumps(dict(task='SEP-01.2',phase='payoff382-metamorphosis-pause-prepared-unobserved',
        alternate_disable=a.alternate_disable,base_sha256=sha(a.base.read_bytes()),map_sha256=sha(a.output.read_bytes()),
        members=members,probe_sha256=sha(probe),probe_source_sha256=sha(source),builder_sha256=sha(Path(__file__).read_bytes()),
        parent_builder_sha256=sha((PARENT/'morph330_make_map.py').read_bytes())),indent=2)+'\n')
if __name__=='__main__':main()
