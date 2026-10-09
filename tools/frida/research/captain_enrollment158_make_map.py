#!/usr/bin/env python3
"""Construct the public temporary captain enrollment producer; no runtime writes."""
import argparse, hashlib, importlib.util, json, subprocess, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('captain_builder', HERE/'GROUP-03.4.6.2.1.2_make_map.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base',type=Path,required=True)
    p.add_argument('--tool',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():p.error('output must be new')
    original=a.base.read_bytes()
    if original[:4]!=b'HM3W' or original[512:516]!=b'MPQ\x1a':p.error('requires wrapped campaign map')
    probe=(HERE/'captain_enrollment158_probe.j').read_text()
    ai=(HERE/'captain_enrollment158_probe.ai').read_bytes()
    members=subprocess.check_output([str(a.tool),'-mpq',str(a.base),'ls'],text=True).splitlines()
    changed={}
    with tempfile.TemporaryDirectory() as temp:
        root=Path(temp);payload=root/'payload.mpq'
        command=[str(a.tool),'-mpq',str(payload),'pack']
        for i,name in enumerate(members):
            data=subprocess.check_output([str(a.tool),'-mpq',str(a.base),'cat',name])
            if name=='war3map.j':data=base.mwpm.instrument(data.decode().replace('\r\n','\n'),probe,'captain_home').encode()
            if name=='war3map.w3u':data=base.append_units(data)
            source=root/str(i);source.write_bytes(data);command.extend([str(source),name])
            if name in ('war3map.j','war3map.w3u'):
                changed[name]=hashlib.sha256(data).hexdigest()
                a.output.with_suffix('.'+name.split('.')[-1]).write_bytes(data)
        source=root/'probe.ai';source.write_bytes(ai)
        command.extend([str(source),'Scripts\\wc3_enrollment158.ai'])
        subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
        a.output.write_bytes(original[:512]+payload.read_bytes())
    result=dict(base_sha256=hashlib.sha256(original).hexdigest(),
                map_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),
                probe_sha256=hashlib.sha256(probe.encode()).hexdigest(),
                ai_sha256=hashlib.sha256(ai).hexdigest(),changed_members=changed)
    a.output.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))

if __name__=='__main__':main()
