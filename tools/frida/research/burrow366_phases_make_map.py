#!/usr/bin/env python3
"""Build a Burrow state and counted invisibility survey from the frozen separation map."""
import argparse
import hashlib
import json
import struct
import subprocess
import tempfile
from pathlib import Path


def sha(data):
    return hashlib.sha256(data).hexdigest()


def ability_objects(duration=None):
    out=struct.pack('<III',2,0,2)
    for alias in [b'Ab61',b'Ab62']:
        mods=[(b'areq',3,0,b'\0'),(b'amcs',0,1,struct.pack('<i',0)),
              (b'acdn',2,1,struct.pack('<f',0)),(b'adur',2,1,struct.pack('<f',0.375)),
              (b'ahdu',2,1,struct.pack('<f',0))]
        out+=b'Abur'+alias+struct.pack('<I',len(mods))
        for field,kind,level,value in mods:
            out+=field+struct.pack('<III',kind,level,0)+value+alias
    return out


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--mpqtool',type=Path,required=True)
    p.add_argument('--probe',type=Path,default=Path(__file__).with_name('burrow366_phases_probe.j'))
    p.add_argument('--empty-ability-units', nargs='*', default=[], help='Original unit rawcodes whose authored ability list is cleared before public UnitAddAbility probes')
    p.add_argument('--duration',type=float,help='Override Myrmidon transition Dur for a separate authored timing control')
    a=p.parse_args()
    if any(len(code)!=4 or not code.isascii() for code in a.empty_ability_units):p.error('unit rawcodes must be four ASCII characters')
    if a.output.exists():p.error('output must be new')
    raw=a.base.read_bytes()
    if sha(raw)!='bb9a45c7ed3dc5ac5920ea1f6f71b6bcc61d9c5956bc07736455ddc1dadaeda4':
        p.error('expected frozen Separation323g map')
    probe=a.probe.read_text()
    globals_text,functions=probe.split('endglobals',1)
    globals_text=globals_text.removeprefix('globals')
    def cat(name):
        return subprocess.check_output([str(a.mpqtool),'-mpq',str(a.base),'cat',name])
    with tempfile.TemporaryDirectory(prefix='morph330-') as directory:
        directory=Path(directory)
        payload=directory/'payload.mpq'
        command=[str(a.mpqtool),'-mpq',str(payload),'pack']
        members={}
        for i,name in enumerate(cat('(listfile)').decode().splitlines()):
            if name=='(listfile)':continue
            data=cat(name)
            if name=='war3map.j':
                s=data.decode()
                start=s.index(' unit udg_S323Unit=null')
                end=s.index('function InitGlobals takes')
                s=s[:start]+globals_text+'\nendglobals\n'+functions+'\n'+s[end:]
                old='SetPlayerController( Player(0), MAP_CONTROL_COMPUTER )'
                assert s.count(old)==1
                s=s.replace(old,'SetPlayerController( Player(0), MAP_CONTROL_USER )')
                data=s.encode()
            elif name=='war3map.w3u' and a.empty_ability_units:
                count=struct.unpack_from('<I',data,4)[0]
                added=b''
                for code in a.empty_ability_units:
                    rawcode=code.encode('ascii')
                    if rawcode in data:raise ValueError('unit already appears in frozen object data: '+code)
                    added+=rawcode+b'\0'*4+struct.pack('<I',5)+b'uabi'+struct.pack('<I',3)+b'\0'+b'\0'*4
                    for field,value in [(b'urpo',2),(b'urpp',17),(b'urpg',17),(b'urpr',17)]:added+=field+struct.pack('<ii',0,value)+b'\0'*4
                data=data[:4]+struct.pack('<I',count+len(a.empty_ability_units))+added+data[8:]
            elif name=='war3map.w3a':
                data=ability_objects(a.duration)
            path=directory/str(i)
            path.write_bytes(data)
            command.extend([str(path),name])
            members[name]=sha(data)
        subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
        a.output.write_bytes(raw[:512]+payload.read_bytes())
    a.output.with_suffix('.json').write_text(json.dumps(dict(
        task='SEP-01.2',base_sha256=sha(raw),map_sha256=sha(a.output.read_bytes()),
        members=members,probe_sha256=sha(probe.encode()),
        builder_sha256=sha(Path(__file__).read_bytes())),indent=2)+'\n')


if __name__=='__main__':main()
