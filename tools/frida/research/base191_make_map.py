#!/usr/bin/env python3
"""Add independent public JASS point orders to the frozen mixed13 Captain map."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path
from schedule190_make_map import BASE_SHA


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('base','tool','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();original=a.base.read_bytes()
    if hashlib.sha256(original).hexdigest()!=BASE_SHA:p.error('requires frozen mixed13 Captain map')
    if a.output.exists():p.error('output must be new')
    tool=str(a.tool.resolve())
    members=[m for m in subprocess.check_output([tool,'-mpq',str(a.base),'ls']).decode().splitlines()if not m.endswith('/')and m!='(listfile)']+['Scripts\\wc3_captain_probe.ai']
    changed={}
    with tempfile.TemporaryDirectory()as d:
        root=Path(d);payload=root/'payload.mpq';command=[tool,'-mpq',str(payload),'pack']
        for i,member in enumerate(members):
            data=subprocess.check_output([tool,'-mpq',str(a.base),'cat',member])
            if member=='war3map.j':
                text=data.decode();anchor='    local integer crowdType = \'hfoo\'';assert text.count(anchor)==1
                text=text.replace(anchor,anchor+'\n    local unit entry191 = null',1)
                anchor='    call PreloadGenStart()';assert text.count(anchor)==1
                text=text.replace(anchor,anchor+'''
    call Preload("PATHTRACE entry191=begin-setup")
    set entry191=CreateUnit(Player(0),'hfoo',4000.0,-2000.0,0.0)
    call SetUnitMoveSpeed(entry191,100.0)
    if IssuePointOrder(entry191,"move",4400.0,-2000.0) then
        call Preload("PATHTRACE entry191=string accepted=1 order="+I2S(GetUnitCurrentOrder(entry191)))
    endif
    if IssuePointOrderById(entry191,OrderId("move"),4500.0,-2000.0) then
        call Preload("PATHTRACE entry191=id accepted=1 order="+I2S(GetUnitCurrentOrder(entry191)))
    endif''',1)
                text=text.replace('pathtrace-captain_home.txt','rs-base191.txt');data=text.encode()
                changed[member]=hashlib.sha256(data).hexdigest();a.output.with_suffix('.j').write_bytes(data)
            f=root/str(i);f.write_bytes(data);command.extend([str(f),member])
        subprocess.run(command,check=True);a.output.write_bytes(original[:512]+payload.read_bytes())
    meta=dict(base_sha256=BASE_SHA,map_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),changed_members=changed,task='BASE-01.2',preload='rs-base191.txt')
    a.output.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta))


if __name__=='__main__':main()
