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
    return struct.pack('<III',2,0,0)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--mpqtool',type=Path,required=True)
    p.add_argument('--probe',type=Path,default=Path(__file__).with_name('hide338_probe.j'))
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
                # Preserve existing authored rows and change only their uabi field.
                pos=4; tables=[]; remaining=set(a.empty_ability_units)
                for table in range(2):
                    count=struct.unpack_from('<I',data,pos)[0];pos+=4; records=[]
                    for _ in range(count):
                        old,new=data[pos:pos+4],data[pos+4:pos+8];n=struct.unpack_from('<I',data,pos+8)[0];pos+=12; mods=[]
                        match=old.decode('ascii') in remaining and new==b'\0'*4
                        for _ in range(n):
                            start=pos;field=data[pos:pos+4];kind=struct.unpack_from('<I',data,pos+4)[0];pos+=8
                            pos=data.index(0,pos)+1 if kind==3 else pos+4
                            pos+=4
                            if not(match and field==b'uabi'):mods.append(data[start:pos])
                        if match:
                            mods.append(b'uabi'+struct.pack('<I',3)+b'\0'+b'\0'*4);remaining.remove(old.decode('ascii'))
                        records.append(old+new+struct.pack('<I',len(mods))+b''.join(mods))
                    tables.append(records)
                assert pos==len(data)
                for code in sorted(remaining):
                    tables[0].append(code.encode()+b'\0'*4+struct.pack('<I',1)+b'uabi'+struct.pack('<I',3)+b'\0'+b'\0'*4)
                data=data[:4]+b''.join(struct.pack('<I',len(t))+b''.join(t) for t in tables)
            elif name=='war3map.w3a':
                data=ability_objects(a.duration)
            path=directory/str(i)
            path.write_bytes(data)
            command.extend([str(path),name])
            members[name]=sha(data)
        subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
        a.output.write_bytes(raw[:512]+payload.read_bytes())
    a.output.with_suffix('.json').write_text(json.dumps(dict(
        task='TARGET-01.2 B01 Hide338',base_sha256=sha(raw),map_sha256=sha(a.output.read_bytes()),
        members=members,probe_sha256=sha(probe.encode()),
        builder_sha256=sha(Path(__file__).read_bytes())),indent=2)+'\n')


if __name__=='__main__':main()
