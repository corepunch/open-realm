#!/usr/bin/env python3
"""Derive the human-control cargo probe from frozen Separation323g.
Only config's Player(0) controller changes; all gameplay and object data remain.
"""
import argparse, hashlib, json, subprocess, tempfile
from pathlib import Path

def sha(data):
    return hashlib.sha256(data).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--mpqtool',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():p.error('output must be new')
    raw=a.base.read_bytes()
    if sha(raw)!='bb9a45c7ed3dc5ac5920ea1f6f71b6bcc61d9c5956bc07736455ddc1dadaeda4':
        p.error('expected frozen Separation323g map')
    def cat(name):return subprocess.check_output([str(a.mpqtool),'-mpq',str(a.base),'cat',name])
    with tempfile.TemporaryDirectory(prefix='cargo329-') as directory:
        directory=Path(directory);payload=directory/'payload.mpq'
        command=[str(a.mpqtool),'-mpq',str(payload),'pack'];members={}
        for i,name in enumerate(cat('(listfile)').decode().splitlines()):
            if name=='(listfile)':continue
            data=cat(name)
            if name=='war3map.j':
                old=b'SetPlayerController( Player(0), MAP_CONTROL_COMPUTER )'
                assert data.count(old)==1
                data=data.replace(old,b'SetPlayerController( Player(0), MAP_CONTROL_USER )')
            path=directory/str(i);path.write_bytes(data)
            command.extend([str(path),name]);members[name]=sha(data)
        subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
        a.output.write_bytes(raw[:512]+payload.read_bytes())
    a.output.with_suffix('.json').write_text(json.dumps(dict(base_sha256=sha(raw),map_sha256=sha(a.output.read_bytes()),members=members),indent=2)+'\n')

if __name__=='__main__':main()
